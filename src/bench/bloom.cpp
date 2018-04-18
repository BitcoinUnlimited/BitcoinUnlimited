// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>

#include "bench.h"
#include "bloom.h"
#include "fastfilter.h"
#include "key.h"
#include "pubkey.h"
#include "utiltime.h"


int sideEffect = 0;

// Set up stuff that should not be timed
class ALotOfSHA256
{
public:
    int amt = 1000000;
    std::vector<uint256> data;

    CFastFilter<4 * 1024 * 1024, 16> filter;
    CFastFilter<4 * 1024 * 1024, 2> filter2;
    CBloomFilter bloom;

    ALotOfSHA256() : bloom(1000000, 0.000001, 0x49393, BLOOM_UPDATE_NONE, 100000000)
    {
        const ECCVerifyHandle verify_handle;
        ECC_Start();
        data.reserve(amt);
        for (int i = 0; i < amt; i++)
        {
            uint256 num = GetRandHash();
            data.push_back(num);
            if (i & 1)
            {
                filter.insert(num);
                filter2.insert(num);
                bloom.insert(num);
            }
        }
        ECC_Stop();
    }
};

ALotOfSHA256 sha;

static void BloomCheckSet(benchmark::State &state)
{
    CBloomFilter filter(1000000, 0.000001, 0x49393, BLOOM_UPDATE_NONE, 100000000);
    int count = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000; i++)
        {
            if (count >= sha.amt)
            {
                count = 0;
                filter.clear();
            }
            if (!filter.contains(sha.data[count]))
            {
                filter.insert(sha.data[count]);
            }
            count++;
        }
    }
}

static void BloomContains(benchmark::State &state)
{
    int count = 0;
    int contains = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000; i++)
        {
            if (count >= sha.amt)
            {
                count = 0;
            }
            if (sha.bloom.contains(sha.data[count]))
                contains++;
            count++;
        }
    }
    sideEffect = contains;
}

BENCHMARK(BloomCheckSet, 1);
BENCHMARK(BloomContains, 1);


static void FastFilterCheckSet(benchmark::State &state)
{
    CFastFilter<4 * 1024 * 1024, 16> filter;
    int count = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000; i++)
        {
            if (count >= sha.amt)
            {
                count = 0;
                filter.reset();
            }
            filter.checkAndSet(sha.data[count]);
            count++;
        }
    }
}

static void FastFilterCheckSet2(benchmark::State &state)
{
    CFastFilter<4 * 1024 * 1024, 2> filter;
    int count = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000; i++)
        {
            if (count >= sha.amt)
            {
                count = 0;
                filter.reset();
            }
            filter.checkAndSet(sha.data[count]);
            count++;
        }
    }
}

static void FastFilterContains(benchmark::State &state)
{
    int count = 0;
    int contains = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000; i++)
        {
            if (count >= sha.amt)
                count = 0;
            if (sha.filter.contains(sha.data[count]))
                contains++;
            count++;
        }
    }
    sideEffect = contains;
}

static void FastFilterContains2(benchmark::State &state)
{
    int count = 0;
    int contains = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000; i++)
        {
            if (count >= sha.amt)
                count = 0;
            if (sha.filter2.contains(sha.data[count]))
                contains++;
            count++;
        }
    }
    sideEffect = contains;
}


// Get data on the overhead of this benchmarking system
static void Nothing(benchmark::State &state)
{
    int count = 0;
    int contains = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000; i++)
        {
            if (count > sha.amt)
            {
                count = 0;
            }
            count++;
            if (count & 1)
                contains++;
        }
    }
    sideEffect = contains;
}


BENCHMARK(Nothing, 1);
BENCHMARK(FastFilterCheckSet, 2);
BENCHMARK(FastFilterCheckSet2, 1);
BENCHMARK(FastFilterContains, 1);
BENCHMARK(FastFilterContains2, 1);
