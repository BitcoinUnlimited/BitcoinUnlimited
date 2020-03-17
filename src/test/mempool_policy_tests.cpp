// Copyright (c) 2019-2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <validation/forks.h>
#include <policy/mempool.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(mempool_policy_tests, BasicTestingSetup)

static void SetMTP(std::array<CBlockIndex, 12> &blocks, int64_t mtp)
{
    size_t len = blocks.size();

    for (size_t i = 0; i < len; ++i)
    {
        blocks[i].nTime = mtp + (i - (len / 2));
    }

    BOOST_CHECK_EQUAL(blocks.back().GetMedianTimePast(), mtp);
}

BOOST_AUTO_TEST_CASE(mempool_policy_activation_tests)
{
    CBlockIndex prev;

    const auto &params = Params(CBaseChainParams::REGTEST).GetConsensus();
    const auto activation = params.may2020ActivationTime;

    std::array<CBlockIndex, 12> blocks;
    for (size_t i = 1; i < blocks.size(); ++i)
    {
        blocks[i].pprev = &blocks[i - 1];
    }

    SetMTP(blocks, activation - 1);
    BOOST_CHECK(!IsMay2020Enabled(params, &blocks.back()));
    BOOST_CHECK_EQUAL(BCH_DEFAULT_ANCESTOR_LIMIT, GetBCHDefaultAncestorLimit(params, &blocks.back()));
    BOOST_CHECK_EQUAL(BCH_DEFAULT_DESCENDANT_LIMIT, GetBCHDefaultDescendantLimit(params, &blocks.back()));

    SetMTP(blocks, activation);
    BOOST_CHECK(IsMay2020Enabled(params, &blocks.back()));
    BOOST_CHECK_EQUAL(BCH_DEFAULT_ANCESTOR_LIMIT_LONGER, GetBCHDefaultAncestorLimit(params, &blocks.back()));
    BOOST_CHECK_EQUAL(BCH_DEFAULT_DESCENDANT_LIMIT_LONGER, GetBCHDefaultDescendantLimit(params, &blocks.back()));
}

BOOST_AUTO_TEST_SUITE_END()

