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

    lookAtMore =
        r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(CTransaction{}), true /* seen before */, false);
    BOOST_CHECK(lookAtMore);
    BOOST_CHECK(!r.IsInteresting());

    lookAtMore =
        r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(CTransaction{}), false, true /* is equivalent */);
    BOOST_CHECK(lookAtMore);
    BOOST_CHECK(!r.IsInteresting());
}

BOOST_AUTO_TEST_CASE(is_interesting)
{
    RespendRelayer r;
    CTxMemPool::txiter dummy;
    bool lookAtMore;

    lookAtMore = r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(CTransaction{}), false, false);
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
    r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(respend), true, false);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.vInventoryToSend.size());
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.vInventoryToSend.size());

    // Create an interesting, but invalid respend
    r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(respend), false, false);
    BOOST_CHECK(r.IsInteresting());
    r.SetValid(false);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.vInventoryToSend.size());
    // make valid
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(1), node.vInventoryToSend.size());
    BOOST_CHECK(respend.GetHash() == node.vInventoryToSend.at(0).hash);

    // Create an interesting and valid respend to an SPV peer
    // add bloom filter using the respend hash.
    CBloomFilter *filter = new CBloomFilter(1, .00001, 5, BLOOM_UPDATE_ALL, 36000);
    delete node.pfilter;
    node.pfilter = filter;
    node.pfilter->insert(respend.GetHash());
    node.vInventoryToSend.clear();
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.vInventoryToSend.size());
    node.pfilter->clear();

    // clean up node
    delete node.pfilter;
    node.pfilter = nullptr;
    vNodes.erase(vNodes.end() - 1);
}

BOOST_AUTO_TEST_SUITE_END();
