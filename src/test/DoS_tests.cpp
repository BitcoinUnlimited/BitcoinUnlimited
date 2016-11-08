// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2016 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for denial-of-service detection/prevention code

#include "chainparams.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "pow.h"
#include "script/sign.h"
#include "serialize.h"
#include "util.h"

#include "test/test_bitcoin.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

// Tests this internal-to-main.cpp method:
extern bool AddOrphanTx(const CTransaction& tx, NodeId peer);
extern void EraseOrphansFor(NodeId peer);
extern void EraseOrphansByTime();
extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);

CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

BOOST_FIXTURE_TEST_SUITE(DoS_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(DoS_banning)
{
    CNode::ClearBanned();
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100); // Should get banned
    SendMessages(&dummyNode1);
    BOOST_CHECK(CNode::IsBanned(addr1));
    BOOST_CHECK(!CNode::IsBanned(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002));
    CNode dummyNode2(INVALID_SOCKET, addr2, "", true);
    dummyNode2.nVersion = 1;
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2);
    BOOST_CHECK(!CNode::IsBanned(addr2)); // 2 not banned yet...
    BOOST_CHECK(CNode::IsBanned(addr1));  // ... but 1 still should be
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2);
    BOOST_CHECK(CNode::IsBanned(addr2));
}

BOOST_AUTO_TEST_CASE(DoS_banscore)
{
    CNode::ClearBanned();
    mapArgs["-banscore"] = "111"; // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100);
    SendMessages(&dummyNode1);
    BOOST_CHECK(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 10);
    SendMessages(&dummyNode1);
    BOOST_CHECK(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 1);
    SendMessages(&dummyNode1);
    BOOST_CHECK(CNode::IsBanned(addr1));
    mapArgs.erase("-banscore");
}

BOOST_AUTO_TEST_CASE(DoS_bantime)
{
    CNode::ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ip(0xa0b0c001));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = 1;

    Misbehaving(dummyNode.GetId(), 100);
    SendMessages(&dummyNode);
    BOOST_CHECK(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60);
    BOOST_CHECK(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60*24+1);
    BOOST_CHECK(!CNode::IsBanned(addr));
}

CTransaction RandomOrphan()
{
    std::map<uint256, COrphanTx>::iterator it;
    it = mapOrphanTransactions.lower_bound(GetRandHash());
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();
    return it->second.tx;
}

BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
{
    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);

    // 50 orphan transactions:
    for (int i = 0; i < 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
      
        LOCK(cs_orphancache);
        AddOrphanTx(tx, i);
    }

    // ... and 50 that depend on other orphans:
    for (int i = 0; i < 50; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev.GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        SignSignature(keystore, txPrev, tx, 0);

        LOCK(cs_orphancache);
        AddOrphanTx(tx, i);
    }

    // This really-big orphan should be ignored:
    for (int i = 0; i < 1; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        tx.vin.resize(500);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev.GetHash();
        }
        SignSignature(keystore, txPrev, tx, 0);
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        LOCK(cs_orphancache);
        BOOST_CHECK(AddOrphanTx(tx, i));  // BU, we keep orphans up to the configured memory limit to help xthin compression so this should succeed whereas it fails in other clients
    }

    // Test EraseOrphansFor:
    for (NodeId i = 0; i < 3; i++)
    {
        size_t sizeBefore = mapOrphanTransactions.size();
        LOCK(cs_orphancache);
        EraseOrphansFor(i);
        BOOST_CHECK(mapOrphanTransactions.size() < sizeBefore);
    }

    // Test LimitOrphanTxSize() function:
    {
        LOCK(cs_orphancache);
        LimitOrphanTxSize(40);
        BOOST_CHECK(mapOrphanTransactions.size() <= 40);
        LimitOrphanTxSize(10);
        BOOST_CHECK(mapOrphanTransactions.size() <= 10);
        LimitOrphanTxSize(0);
        BOOST_CHECK(mapOrphanTransactions.empty());
        BOOST_CHECK(mapOrphanTransactionsByPrev.empty());
    }

    // Test EraseOrphansByTime():
    {
        LOCK(cs_orphancache);
        int64_t nStartTime = GetTime();
        SetMockTime(nStartTime); // Overrides future calls to GetTime()
        for (int i = 0; i < 50; i++)
        {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vin[0].prevout.n = 0;
            tx.vin[0].prevout.hash = GetRandHash();
            tx.vin[0].scriptSig << OP_1;
            tx.vout.resize(1);
            tx.vout[0].nValue = 1*CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
      
            AddOrphanTx(tx, i);
        }
        BOOST_CHECK(mapOrphanTransactions.size() == 50);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        // Advance the clock 1 minute
        SetMockTime(nStartTime+60);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        // Advance the clock 10 minutes
        SetMockTime(nStartTime+60*10);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        // Advance the clock 1 hour
        SetMockTime(nStartTime+60*60);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        // Advance the clock 72 hours
        SetMockTime(nStartTime+60*60*72);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        /** Test the boundary where orphans should get purged. **/
        // Advance the clock 72 hours and 4 minutes 59 seconds
        SetMockTime(nStartTime+60*60*72 + 299);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        // Advance the clock 72 hours and 5 minutes
        SetMockTime(nStartTime+60*60*72 + 300);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
