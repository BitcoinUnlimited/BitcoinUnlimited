// Copyright (c) 2016-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <bench/data.h>
#include <chainparams.h>
#include <test/test_bitcoin.h>

#include <rpc/blockchain.h>
#include <streams.h>
#include <validation/validation.h>

#include <univalue.h>

static void BlockToJsonVerbose(benchmark::State &state)
{
    TestingSetup test_setup(CBaseChainParams::REGTEST);
    CDataStream stream(benchmark::data::block413567, SER_NETWORK, PROTOCOL_VERSION);
    char a = '\0';
    stream.write(&a, 1); // Prevent compaction

    CBlock block;
    stream >> block;

    CBlockIndex blockindex;
    const uint256 blockHash = block.GetHash();
    blockindex.phashBlock = &blockHash;
    blockindex.nBits = 403014710;

    while (state.KeepRunning())
    {
        (void)blockToJSON(block, &blockindex, /*verbose*/ true);
    }
}

BENCHMARK(BlockToJsonVerbose, 10);
