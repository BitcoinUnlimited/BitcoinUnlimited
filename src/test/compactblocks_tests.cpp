// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockencodings.h"
#include "chainparams.h"
#include "consensus/merkle.h"
#include "random.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

struct RegtestingSetup : public TestingSetup
{
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(blockencodings_tests, RegtestingSetup)

static CBlock BuildBlockTestCase()
{
    CBlock block;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(1);
    tx.vout[0].nValue = 42;

    block.vtx.resize(3);
    block.vtx[0] = MakeTransactionRef(CTransaction(tx));
    block.nVersion = 42;
    block.hashPrevBlock = GetRandHash();
    block.nBits = 0x207fffff;

    tx.vin[0].prevout.hash = GetRandHash();
    tx.vin[0].prevout.n = 0;
    block.vtx[1] = MakeTransactionRef(CTransaction(tx));

    tx.vin.resize(10);
    for (size_t i = 0; i < tx.vin.size(); i++)
    {
        tx.vin[i].prevout.hash = GetRandHash();
        tx.vin[i].prevout.n = 0;
    }
    block.vtx[2] = MakeTransactionRef(CTransaction(tx));

    bool mutated;
    block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);
    assert(!mutated);
    while (!CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus()))
        ++block.nNonce;
    return block;
}

// Number of shared use_counts we expect for a tx we havent touched
// == 2 (mempool + our copy from the GetSharedTx call)
#define SHARED_TX_OFFSET 2


BOOST_AUTO_TEST_SUITE_END()
