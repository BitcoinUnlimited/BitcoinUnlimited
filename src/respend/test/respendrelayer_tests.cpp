// Copyright (c) 2018 The Bitcoin developers
// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
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
    CTxMemPool pool(CFeeRate(0));
    TestMemPoolEntryHelper entry;
   // int nHashType = InsecureRand32();

    uint256 outhash = InsecureRand256();

    // create a transaction with multiple inputs
    CMutableTransaction tx = CMutableTransaction();
    tx.vin.resize(3);
    tx.vin[0].scriptSig = CScript();
    tx.vin[0].prevout.hash = InsecureRand256();
    tx.vin[0].prevout.n = 0;
    tx.vin[1].scriptSig = CScript();
    tx.vin[1].prevout.hash = InsecureRand256();
    tx.vin[1].prevout.n = 0;
    tx.vin[2].scriptSig = CScript();
    tx.vin[2].prevout.hash = InsecureRand256();
    tx.vin[2].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = CScript();
    tx.vout[0].nValue = 6 * COIN;
    pool.addUnchecked(tx.GetHash(), entry.FromTx(tx));
    CTxMemPool::txiter iter = pool.mapTx.find(tx.GetHash());
   // uint256 sh = SignatureHash(scriptCode, tx, nIn, SIGHASH_FORKID, 0, 0);

    // create another transaction that spends one of the same inputs as the above tx
    CMutableTransaction respend;
    respend.vin.resize(1);
    respend.vin[0].prevout.n = 0;
    respend.vin[0].prevout.hash = tx.vin[1].prevout.hash;
    respend.vin[0].scriptSig << OP_1;

    CNode node(INVALID_SOCKET, CAddress());
    node.fRelayTxes = true;
    LOCK(cs_vNodes);
    vNodes.push_back(&node);

    // Create a "not interesting" respend
    RespendRelayer r;
    r.AddOutpointConflict(COutPoint{}, iter, MakeTransactionRef(respend), true, false);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());

    // Create an interesting, but invalid respend
    r.AddOutpointConflict(COutPoint{}, iter, MakeTransactionRef(respend), false, false);
    BOOST_CHECK(r.IsInteresting());
    r.SetValid(false);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());
    // make valid
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(1), node.GetInventoryToSendSize());
    {
        LOCK(node.cs_inventory);
        BOOST_CHECK(respend.GetHash() == node.vInventoryToSend.at(0).hash);
        BOOST_CHECK(0x94a0 == node.vInventoryToSend.at(0).type);
    }

    // Create an interesting and valid respend to an SPV peer
    // add bloom filter using the respend hash.
    CBloomFilter *filter = new CBloomFilter(1, .00001, 5, BLOOM_UPDATE_ALL, 36000);
    {
        LOCK(node.cs_filter);
        delete node.pfilter;
        node.pfilter = filter;
        node.pfilter->insert(respend.GetHash());
    }
    {
        LOCK(node.cs_inventory);
        node.vInventoryToSend.clear();
    }
    r.SetValid(true);
    r.Trigger();
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());
    {
        LOCK(node.cs_filter);
        node.pfilter->clear();
        // clean up node
        delete node.pfilter;
        node.pfilter = nullptr;
    }

    vNodes.erase(vNodes.end() - 1);
}

BOOST_AUTO_TEST_SUITE_END();
