// Copyright (c) 2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"
#include "hash.h"

static void Murmur3(benchmark::State &state)
{
    std::vector<uint8_t> in(32, 0);
    unsigned int x = 0;
    while (state.KeepRunning())
    {
        for (int i = 0; i < 1000000; i++)
            x += MurmurHash3(5 * 0xfba4c795, in);
    }
}

BENCHMARK(Murmur3);
