// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RANDOM_H
#define BITCOIN_RANDOM_H

#include "uint256.h"

#include <stdint.h>

/* Seed OpenSSL PRNG with additional entropy data */
void RandAddSeed();

/**
 * Functions to gather random data via the OpenSSL PRNG
 */
void GetRandBytes(unsigned char *buf, int num);
uint64_t GetRand(uint64_t nMax);
int GetRandInt(int nMax);
uint256 GetRandHash();

/**
 * Function to gather random data from multiple sources, failing whenever any
 * of those source fail to provide a result.
 */
void GetStrongRandBytes(unsigned char *buf, int num);

/**
 * Fast randomness source. This is seeded once with secure random data, but
 * is completely deterministic and insecure after that.
 * This class is not thread-safe.
 */
class FastRandomContext
{
private:
    uint64_t bitbuf;
    int bitbuf_size;

    void FillBitBuffer()
    {
        bitbuf = rand64();
        bitbuf_size = 64;
    }

public:
    explicit FastRandomContext(bool fDeterministic = false);

    uint32_t Rz;
    uint32_t Rw;

    uint32_t rand32()
    {
        Rz = 36969 * (Rz & 65535) + (Rz >> 16);
        Rw = 18000 * (Rw & 65535) + (Rw >> 16);
        return (Rw << 16) + Rz;
    }

    bool randbool() { return rand32() & 1; }
    uint64_t rand64()
    {
        uint64_t a = rand32();
        uint64_t b = rand32();
        return (b << 32) + a;
    }

    bool randbool() { return rand32() & 1; }
    uint64_t randbits(int bits)
    {
        if (bits == 0)
        {
            return 0;
        }
        else if (bits > 32)
        {
            return rand64() >> (64 - bits);
        }
        else
        {
            if (bitbuf_size < bits)
                FillBitBuffer();

            uint64_t ret = bitbuf & (~uint64_t(0) >> (64 - bits));
            bitbuf >>= bits;
            bitbuf_size -= bits;
            return ret;
        }
    }
};

/* Number of random bytes returned by GetOSRand.
 * When changing this constant make sure to change all call sites, and make
 * sure that the underlying OS APIs for all platforms support the number.
 * (many cap out at 256 bytes).
 */
static const ssize_t NUM_OS_RANDOM_BYTES = 32;

/** Get 32 bytes of system entropy. Do not use this in application code: use
 * GetStrongRandBytes instead.
 */
void GetOSRand(unsigned char *ent32);

/** Check that OS randomness is available and returning the requested number
 * of bytes.
 */
bool Random_SanityCheck();

#endif // BITCOIN_RANDOM_H
