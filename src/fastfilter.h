// Copyright (c) 2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FAST_FILTER_H
#define BITCOIN_FAST_FILTER_H

#include "random.h"
#include "serialize.h"
#include <vector>

class COutPoint;
class uint256;

#define FILTER_SIZE (512*1024)
#define FILTER_BYTES (FILTER_SIZE/8)
#define REL_PRIME 27061
/**
 * FastFilter is a probabilistic filter.  The filter can answer whether an element
 * definitely is NOT in the set, but only that an element is LIKELY in the set.
 * This is similar to a Bloom filter, but much faster.
 *
 * This filter expects that the input elements have a random distribution (i.e. hashes)
 *
 * This class is thread-safe in the sense that simultaneous calls to insert and contains will not crash,
 * but "inserts" may be lost.  However, if you are using this class as an in-ram filter before doing a more expensive
 * operation, a lost insert may be acceptable.
 */
class CFastFilter
{
protected:
    std::vector<unsigned char> vData;
    unsigned int grabFrom;

public:
    CFastFilter()
    {
        vData.resize(FILTER_BYTES);
        // by sampling from a different part of the uint256, we make it harder for an attacker to generate collisions
        grabFrom = (insecure_rand() % 7)*4;  // should be 4 byte aligned for speed
    }
    void insert(const uint256 &hash)
    {
        const unsigned char* mem = hash.begin();
        const uint32_t *pos = (const uint32_t*) &(mem[grabFrom]);
        uint32_t idx = (*pos) & (FILTER_SIZE - 1);
        vData[idx >> 3] |= (1 << (idx & 7));
    }
    bool contains(const uint256 &hash) const
    {
        const unsigned char* mem = hash.begin();
        const uint32_t *pos = (const uint32_t*) &(mem[grabFrom]);
        uint32_t idx = (*pos) & (FILTER_SIZE - 1);
        return vData[idx >> 3] & (1 << (idx & 7));
    }

    //bool contains(const COutPoint& outpoint) const;
    //void insert(const COutPoint& outpoint);

    void reset()
    {
        vData.clear();
    }
};



class CRollingFastFilter:public CFastFilter
{
    unsigned int erase;
public:
    CRollingFastFilter()
    {
        erase = insecure_rand()%FILTER_BYTES;
    }
    void insert(const uint256 &hash)
    {
        // By clearing 128 entries each time, the filter will never have a false positive rate > 1%
        erase += REL_PRIME;
        erase &= (FILTER_BYTES-1);
        // the loc var and repeated & is for thread safety
        unsigned int loc = erase & (FILTER_BYTES-1);
        for (int j=0;j<16;j++)  // todo: optimize
        {
          vData[loc] = 0;
          loc++;
          loc &= (FILTER_BYTES-1);
        }

        CFastFilter::insert(hash);
    }
};

#endif
