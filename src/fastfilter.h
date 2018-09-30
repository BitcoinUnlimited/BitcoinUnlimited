// Copyright (c) 2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FAST_FILTER_H
#define BITCOIN_FAST_FILTER_H

#include "random.h"
#include "serialize.h"
#include <vector>

class uint256;

// Statically evaluated expression to return whether a number is a power of 2
constexpr bool isPow2(unsigned int num) { return num && !(num & (num - 1)); }
/**
 * FastFilter is a probabilistic filter.  The filter can answer whether an element
 * definitely is NOT in the set, but only that an element is LIKELY in the set.
 * This is similar to a Bloom filter, but much faster.
 *
 * This filter expects that the input elements have a random distribution (i.e. hashes), and so does not hash the
 * input again.  This is how it gains the majority of its performance improvement.
 *
 * This class can be used anywhere a Bloom filter is used so long as the input data is random.
 *
 * If NUM_HASH_FNS is 16 and FILTER_SIZE is >= 64k all bits in the uint256 input data will be used to set bits in
 * the filter.  If these fields are set to lower numbers, fewer bits may be used (although in the NUM_HASH_FNS case
 * execution will be faster).  Therefore, if this structure is used in an application that accepts externally created
 * uint256 numbers that are sensitive to deliberately constructed collisions, be sure to keep NUM_HASH_FNS high enough
 * that the creation of collisions in the used bits is not feasible.
 *
 * Note also that the input bits are used without obfuscation or mixing so if an attacker can control some input bits
 * the attacker can cause collisions in some of the fast filter entries for his inputs.  This will cause higher false
 * positive rates for these transactions.  For example, if the attacker can control 32 bits of the input, he can
 * effectively reduce the number of hash functions in the fast filter by 2 because he has engineered a guaranteed
 * collision for the two functions that use those bits.
 *
 * This class is thread-safe in the sense that simultaneous calls to member functions will not crash,
 * but "inserts" may be lost.  However, if you are using this class as an in-ram filter before doing a more expensive
 * operation, a lost insert may be acceptable.
 *
 * FILTER_SIZE must be a power of 2, and NUM_HASH_FNS may range from 2 to 16 inclusive.  Since hashes are calculated
 * in pairs of 2, odd values of NUM_HASH_FNS are rounded down.
 */
template <unsigned int FILTER_SIZE, unsigned int NUM_HASH_FNS = 16>
class CFastFilter
{
protected:
    // A bit vector containing the bloom filter data
    std::vector<unsigned char> vData;

public:
    enum
    {
        FILTER_BYTES = FILTER_SIZE / 8
    };

    CFastFilter()
    {
        static_assert((NUM_HASH_FNS > 1) && (NUM_HASH_FNS <= 16), "NUM_HASH_FNS must be between 2 and 16 inclusive");
        static_assert(isPow2(FILTER_SIZE) && (FILTER_SIZE > 1), "FILTER_SIZE must be a power of 2 greater than 1");
        FastRandomContext insecure_rand;
        vData.resize(FILTER_BYTES);
    }


    // returns true IF this function made a change (i.e. the value was previously not set).
    bool checkAndSet(const uint256 &hash)
    {
        const uint32_t *pos = (const uint32_t *)hash.begin();
        bool unset = 0; // If any position is not set, then this will be true
        for (register unsigned int i = 0; i < NUM_HASH_FNS / 2; i++, pos++)
        {
            register uint32_t val = *pos;
            register uint32_t idx = val & (FILTER_SIZE - 1);
            register uint32_t bit = (1 << (idx & 7));
            idx >>= 3;
            unset |= (0 == (vData[idx] & bit));

            val = __builtin_bswap32(val);
            register uint32_t idx2 = val & (FILTER_SIZE - 1);
            register uint32_t bit2 = (1 << (idx2 & 7));
            idx2 >>= 3;

            unset |= (0 == (vData[idx2] & bit2));

            vData[idx] |= bit;
            vData[idx2] |= bit2;
        }
        return unset;
    }

    void insert(const uint256 &hash)
    {
        const uint32_t *pos = (const uint32_t *)hash.begin();
        for (register unsigned int i = 0; i < NUM_HASH_FNS / 2; i++, pos++)
        {
            register uint32_t val = *pos;
            register uint32_t idx = val & (FILTER_SIZE - 1);
            val = __builtin_bswap32(val);
            register uint32_t idx2 = val & (FILTER_SIZE - 1);

            vData[idx >> 3] |= (1 << (idx & 7));
            vData[idx2 >> 3] |= (1 << (idx2 & 7));
        }
    }

    bool contains(const uint256 &hash) const
    {
        const uint32_t *pos = (const uint32_t *)hash.begin();
        bool unset = 0; // If any position is not set, then this will be true
        for (register unsigned int i = 0; i < NUM_HASH_FNS / 2; i++, pos++)
        {
            register uint32_t val = *pos;
            register uint32_t idx = val & (FILTER_SIZE - 1);
            val = __builtin_bswap32(val);
            register uint32_t idx2 = val & (FILTER_SIZE - 1);

            unset |= (0 == (vData[idx >> 3] & (1 << (idx & 7))));
            unset |= (0 == (vData[idx2 >> 3] & (1 << (idx2 & 7))));
        }
        return !unset;
    }

    void reset() { memset(&vData[0], 0, FILTER_BYTES); }
};


template <unsigned int FILTER_SIZE, unsigned int NUM_HASH_FNS = 16>
class CRollingFastFilter : public CFastFilter<FILTER_SIZE, NUM_HASH_FNS>
{
    unsigned int erase;
    unsigned int eraseAmt;

public:
    CRollingFastFilter(unsigned int _eraseAmt = 16)
    {
        FastRandomContext insecure_rand;
        erase = insecure_rand.rand32() % CFastFilter<FILTER_SIZE>::FILTER_BYTES;
        eraseAmt = _eraseAmt;
    }

    void roll(void)
    {
        // By clearing some entries each time, the filter's false positive rate is limited.
        // Every time insert is called 1 entry is added and eraseAmt*8 entries are cleared.
        // The average "fill" of the filter (ratio of set to total)  will therefore be 1/(eraseAmt*8).
        // Since the false positive rate is the chance that a random value insertion hits one already there, it
        // is the same as the fill ratio.  At the default value of 16, this is 1/128 or < 1%

        // To match the math above it is essential that every entry is erased before an entry is erased again.
        // Erasing entries sequentially accomplishes this and is fine because inserts happen in random position.
        erase += eraseAmt;
        erase &= (CFastFilter<FILTER_SIZE>::FILTER_BYTES - 1);
        // the loc var and repeated & is for thread safety
        unsigned int loc = erase & (CFastFilter<FILTER_SIZE>::FILTER_BYTES - 1);
        for (unsigned int j = 0; j < eraseAmt; j++) // todo: optimize by clearing in 64 bit chunks
        {
            this->vData[loc] = 0;
            loc++;
            loc &= (CFastFilter<FILTER_SIZE>::FILTER_BYTES - 1); // wrap around
        }
    }

    void insert(const uint256 &hash)
    {
        roll();
        CFastFilter<FILTER_SIZE>::insert(hash);
    }
};

#endif
