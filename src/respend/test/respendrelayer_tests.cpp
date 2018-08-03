// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "respend/respendrelayer.h"
#include "net.h"
#include "random.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using namespace respend;

BOOST_FIXTURE_TEST_SUITE(respendrelayer_tests, BasicTestingSetup);

BOOST_AUTO_TEST_CASE(not_interesting)
{
    RespendRelayer r;
    BOOST_CHECK(!r.IsInteresting());
    CTxMemPool::txiter dummy;
    bool lookAtMore;

    lookAtMore = r.AddOutpointConflict(COutPoint{}, dummy, CTransaction{}, true /* seen before */, false);
    BOOST_CHECK(lookAtMore);
    BOOST_CHECK(!r.IsInteresting());

    lookAtMore = r.AddOutpointConflict(COutPoint{}, dummy, CTransaction{}, false, true /* is equivalent */);
    BOOST_CHECK(lookAtMore);
    BOOST_CHECK(!r.IsInteresting());
}

BOOST_AUTO_TEST_CASE(is_interesting)
{
    RespendRelayer r;
    CTxMemPool::txiter dummy;
    bool lookAtMore;

    lookAtMore = r.AddOutpointConflict(COutPoint{}, dummy, CTransaction{}, false, false);
    BOOST_CHECK(!lookAtMore);
    BOOST_CHECK(r.IsInteresting());
}

BOOST_AUTO_TEST_CASE(triggers_correctly)
{
    CTxMemPool::txiter dummy;
    CMutableTransaction respend;
    respend.vin.resize(1);
    respend.vin[0].prevout.n = 0;
    respend.vin[0].prevout.hash = GetRandHash();
    respend.vin[0].scriptSig << OP_1;

    CNode node(INVALID_SOCKET, CAddress());
    node.fRelayTxes = true;
    LOCK(cs_vNodes);
    vNodes.push_back(&node);

    // Create a "not interesting" respend
    RespendRelayer r;
    r.AddOutpointConflict(COutPoint{}, dummy, respend, true, false);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.vInventoryToSend.size());
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.vInventoryToSend.size());

    // Create an interesting, but invalid respend
    r.AddOutpointConflict(COutPoint{}, dummy, respend, false, false);
    BOOST_CHECK(r.IsInteresting());
    r.SetValid(false);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.vInventoryToSend.size());
    // make valid
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(1), node.vInventoryToSend.size());
    BOOST_CHECK(respend.GetHash() == node.vInventoryToSend.at(0).hash);
    vNodes.erase(vNodes.end() - 1);
}

BOOST_AUTO_TEST_SUITE_END();
