// Copyright (c) 2023 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <util.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(getmintxsize_tests, BasicTestingSetup)

static void SetMTP(std::array<CBlockIndex, 12> &blocks, int64_t mtp)
{
    size_t len = blocks.size();

    for (size_t i = 0; i < len; ++i)
    {
        blocks[i].nTime = mtp + (i - (len / 2));
    }

    assert(blocks.back().GetMedianTimePast() == mtp);
}

BOOST_AUTO_TEST_CASE(getmintxsize)
{
    const CChainParams config = Params(CBaseChainParams::REGTEST);
    CBlockIndex prev;

    std::array<CBlockIndex, 12> blocks;
    for (size_t i = 1; i < blocks.size(); ++i)
    {
        blocks[i].pprev = &blocks[i - 1];
    }

    // For functional tests, the activation time can be overridden.
    uint64_t activation = 1600000000;
    SetArg("-upgrade9activationtime", "1600000000");

    SetMTP(blocks, activation - 1);
    // Check if GetMinimumTxSize returns the correct value
    BOOST_CHECK_EQUAL(GetMinimumTxSize(config.GetConsensus(), &blocks.back()), MIN_TX_SIZE_MAGNETIC_ANOMALY);

    SetMTP(blocks, activation);
    BOOST_CHECK_EQUAL(GetMinimumTxSize(config.GetConsensus(), &blocks.back()), MIN_TX_SIZE_UPGRADE9);

    SetMTP(blocks, activation + 1);
    BOOST_CHECK_EQUAL(GetMinimumTxSize(config.GetConsensus(), &blocks.back()), MIN_TX_SIZE_UPGRADE9);

    // Cleanup
    UnsetArg("-upgrade9activationtime");
}

BOOST_AUTO_TEST_SUITE_END()
