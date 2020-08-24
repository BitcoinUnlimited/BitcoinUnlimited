// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"
#include "hashwrapper.h"

static void Murmur3(benchmark::State &state)
{
    std::vector<uint8_t> in(32, 0);
    unsigned int x = 0;
    while (state.KeepRunning())
    {
        x += MurmurHash3(5 * 0xfba4c795, in);
    }
}

BENCHMARK(Murmur3, 500000);
