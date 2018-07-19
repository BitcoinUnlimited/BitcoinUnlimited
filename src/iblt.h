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
#ifndef CIblt_H
#define CIblt_H

#include "serialize.h"

#include <inttypes.h>
#include <set>
#include <vector>

//
// Invertible Bloom Lookup Table implementation
// References:
//
// "What's the Difference? Efficient Set Reconciliation
// without Prior Context" by Eppstein, Goodrich, Uyeda and
// Varghese
//
// "Invertible Bloom Lookup Tables" by Goodrich and
// Mitzenmacher
//

class HashTableEntry
{
public:
    int32_t count;
    uint64_t keySum;
    uint32_t keyCheck;
    std::vector<uint8_t> valueSum;

    HashTableEntry() : count(0), keySum(0), keyCheck(0) {}
    bool isPure() const;
    bool empty() const;
    void addValue(const std::vector<uint8_t> &v);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(count);
        READWRITE(keySum);
        READWRITE(keyCheck);
        READWRITE(valueSum);
    }
};

class CIblt
{
public:
    CIblt();
    CIblt(size_t _expectedNumEntries);
    CIblt(const CIblt &other);
    virtual ~CIblt();

    void reset();
    size_t size();
    void resize(size_t _expectedNumEntries);
    void insert(uint64_t k, const std::vector<uint8_t> &v);
    void erase(uint64_t k, const std::vector<uint8_t> &v);

    // Returns true if a result is definitely found or not
    // found. If not found, result will be empty.
    // Returns false if overloaded and we don't know whether or
    // not k is in the table.
    bool get(uint64_t k, std::vector<uint8_t> &result) const;
    uint8_t getNHash() { return n_hash; }
    // Adds entries to the given sets:
    //  positive is all entries that were inserted
    //  negative is all entreis that were erased but never added (or
    //   if the IBLT = A-B, all entries in B that are not in A)
    // Returns true if all entries could be decoded, false otherwise.
    bool listEntries(std::set<std::pair<uint64_t, std::vector<uint8_t> > > &positive,
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > &negative) const;


    // Subtract two IBLTs
    CIblt operator-(const CIblt &other) const;

    // For debugging:
    std::string DumpTable() const;

    static size_t OptimalNHash(size_t expectedNumEntries);
    static float OptimalOverhead(size_t expectedNumEntries);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(n_hash);
        if (ser_action.ForRead() && n_hash == 0)
        {
            throw std::ios_base::failure("Number of IBLT hash functions needs to be > 0");
        }
        READWRITE(is_modified);
        READWRITE(hashTable);
    }

    inline bool isModified() { return is_modified; }
private:
    void _insert(int plusOrMinus, uint64_t k, const std::vector<uint8_t> &v);

    uint8_t n_hash;
    bool is_modified;

    std::vector<HashTableEntry> hashTable;
};

#endif /* CIblt_H */
