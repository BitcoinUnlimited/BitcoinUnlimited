// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include "checkpoints.h"

#include "chainparams.h"
#include "test/test_bitcoin.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(Checkpoints_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(sanity)
{
    // Test Get Total Block Estimate
    const CCheckpointData &checkpoints = Params(CBaseChainParams::MAIN).Checkpoints();
    BOOST_CHECK(Checkpoints::GetTotalBlocksEstimate(checkpoints) >= 134444);


    // Test the finding of the last checkpoint
    uint256 hash1 = chainActive.Tip()->pprev->pprev->GetBlockHash();
    uint256 hash2 = chainActive.Tip()->pprev->GetBlockHash();
    uint256 hash_last_checkpoint = chainActive.Tip()->GetBlockHash();

    CCheckpointData checkpoints2;
    checkpoints2.mapCheckpoints[1] = hash1;
    checkpoints2.mapCheckpoints[2] = hash2;
    checkpoints2.mapCheckpoints[3] = hash_last_checkpoint;

    READLOCK(cs_mapBlockIndex);
    CBlockIndex *pindex2 = Checkpoints::GetLastCheckpoint(checkpoints2);
    BOOST_CHECK(pindex2 != nullptr);
    if (pindex2)
    {
        BOOST_CHECK(pindex2->GetBlockHash() == hash_last_checkpoint);
    }
}

BOOST_AUTO_TEST_SUITE_END()
