/*
Copyright (c) 2018 The Bitcoin Unlimited developers
Copyright (c) 2014 Gavin Andresen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "iblt.h"
#include "hash.h"
#include <cassert>
#include <iostream>
#include <list>
#include <sstream>
#include <utility>

static const size_t N_HASH = 3;
static const size_t N_HASHCHECK = 11;

template <typename T>
std::vector<uint8_t> ToVec(T number)
{
    std::vector<uint8_t> v(sizeof(T));
    for (size_t i = 0; i < sizeof(T); i++)
    {
        v.at(i) = (number >> i * 8) & 0xff;
    }
    return v;
}


bool HashTableEntry::isPure() const
{
    if (count == 1 || count == -1)
    {
        uint32_t check = MurmurHash3(N_HASHCHECK, ToVec(keySum));
        return (keyCheck == check);
    }
    return false;
}

bool HashTableEntry::empty() const { return (count == 0 && keySum == 0 && keyCheck == 0); }
void HashTableEntry::addValue(const std::vector<uint8_t> v)
{
    if (v.empty())
    {
        return;
    }
    if (valueSum.size() < v.size())
    {
        valueSum.resize(v.size());
    }
    for (size_t i = 0; i < v.size(); i++)
    {
        valueSum[i] ^= v[i];
    }
}

CIblt::CIblt()
{
    valueSize = 0;
    is_modified = false;
}

CIblt::CIblt(size_t _expectedNumEntries, size_t _valueSize) : is_modified(false)
{
    CIblt::resize(_expectedNumEntries, _valueSize);
}

CIblt::CIblt(const CIblt &other) : is_modified(false)
{
    valueSize = other.valueSize;
    hashTable = other.hashTable;
}

CIblt::~CIblt() {}
void CIblt::reset()
{
    size_t size = this->size();
    hashTable.clear();
    hashTable.resize(size);
}

size_t CIblt::size() { return hashTable.size(); }
void CIblt::resize(size_t _expectedNumEntries, size_t _valueSize)
{
    assert(is_modified == false);

    CIblt::valueSize = _valueSize;

    // 1.5x expectedNumEntries gives very low probability of
    // decoding failure
    size_t nEntries = _expectedNumEntries + _expectedNumEntries / 2;
    // ... make nEntries exactly divisible by N_HASH
    while (N_HASH * (nEntries / N_HASH) != nEntries)
        ++nEntries;
    hashTable.resize(nEntries);
}

void CIblt::_insert(int plusOrMinus, uint64_t k, const std::vector<uint8_t> v)
{
    assert(v.size() == valueSize);

    std::vector<uint8_t> kvec = ToVec(k);

    size_t bucketsPerHash = hashTable.size() / N_HASH;
    for (size_t i = 0; i < N_HASH; i++)
    {
        size_t startEntry = i * bucketsPerHash;

        uint32_t h = MurmurHash3(i, kvec);
        HashTableEntry &entry = hashTable.at(startEntry + (h % bucketsPerHash));
        entry.count += plusOrMinus;
        entry.keySum ^= k;
        entry.keyCheck ^= MurmurHash3(N_HASHCHECK, kvec);
        if (entry.empty())
        {
            entry.valueSum.clear();
        }
        else
        {
            entry.addValue(v);
        }
    }

    is_modified = true;
}

void CIblt::insert(uint64_t k, const std::vector<uint8_t> v) { _insert(1, k, v); }
void CIblt::erase(uint64_t k, const std::vector<uint8_t> v) { _insert(-1, k, v); }
bool CIblt::get(uint64_t k, std::vector<uint8_t> &result) const
{
    result.clear();

    std::vector<uint8_t> kvec = ToVec(k);

    size_t bucketsPerHash = hashTable.size() / N_HASH;
    for (size_t i = 0; i < N_HASH; i++)
    {
        size_t startEntry = i * bucketsPerHash;

        uint32_t h = MurmurHash3(i, kvec);
        const HashTableEntry &entry = hashTable.at(startEntry + (h % bucketsPerHash));

        if (entry.empty())
        {
            // Definitely not in table. Leave
            // result empty, return true.
            return true;
        }
        else if (entry.isPure())
        {
            if (entry.keySum == k)
            {
                // Found!
                result.assign(entry.valueSum.begin(), entry.valueSum.end());
                return true;
            }
            else
            {
                // Definitely not in table.
                return true;
            }
        }
    }

    // Don't know if k is in table or not; "peel" the IBLT to try to find
    // it:
    CIblt peeled = *this;
    size_t nErased = 0;
    for (size_t i = 0; i < peeled.hashTable.size(); i++)
    {
        HashTableEntry &entry = peeled.hashTable.at(i);
        if (entry.isPure())
        {
            if (entry.keySum == k)
            {
                // Found!
                result.assign(entry.valueSum.begin(), entry.valueSum.end());
                return true;
            }
            ++nErased;
            peeled._insert(-entry.count, entry.keySum, entry.valueSum);
        }
    }
    if (nErased > 0)
    {
        // Recurse with smaller IBLT
        return peeled.get(k, result);
    }
    return false;
}

bool CIblt::listEntries(std::set<std::pair<uint64_t, std::vector<uint8_t> > > &positive,
    std::set<std::pair<uint64_t, std::vector<uint8_t> > > &negative) const
{
    CIblt peeled = *this;

    size_t nErased = 0;
    do
    {
        nErased = 0;
        for (size_t i = 0; i < peeled.hashTable.size(); i++)
        {
            HashTableEntry &entry = peeled.hashTable.at(i);
            if (entry.isPure())
            {
                if (entry.count == 1)
                {
                    positive.insert(std::make_pair(entry.keySum, entry.valueSum));
                }
                else
                {
                    negative.insert(std::make_pair(entry.keySum, entry.valueSum));
                }
                peeled._insert(-entry.count, entry.keySum, entry.valueSum);
                ++nErased;
            }
        }
    } while (nErased > 0);

    // If any buckets for one of the hash functions is not empty,
    // then we didn't peel them all:
    for (size_t i = 0; i < peeled.hashTable.size() / N_HASH; i++)
    {
        if (peeled.hashTable.at(i).empty() != true)
            return false;
    }
    return true;
}

CIblt CIblt::operator-(const CIblt &other) const
{
    // IBLT's must be same params/size:
    assert(valueSize == other.valueSize);
    assert(hashTable.size() == other.hashTable.size());

    CIblt result(*this);
    for (size_t i = 0; i < hashTable.size(); i++)
    {
        HashTableEntry &e1 = result.hashTable.at(i);
        const HashTableEntry &e2 = other.hashTable.at(i);
        e1.count -= e2.count;
        e1.keySum ^= e2.keySum;
        e1.keyCheck ^= e2.keyCheck;
        if (e1.empty())
        {
            e1.valueSum.clear();
        }
        else
        {
            e1.addValue(e2.valueSum);
        }
    }

    return result;
}

// For debugging during development:
std::string CIblt::DumpTable() const
{
    std::ostringstream result;

    result << "count keySum keyCheckMatch\n";
    for (size_t i = 0; i < hashTable.size(); i++)
    {
        const HashTableEntry &entry = hashTable.at(i);
        result << entry.count << " " << entry.keySum << " ";
        result << (MurmurHash3(N_HASHCHECK, ToVec(entry.keySum)) == entry.keyCheck ? "true" : "false");
        result << "\n";
    }

    return result.str();
}
