// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for denial-of-service detection/prevention code

#include "chainparams.h"
#include "dosman.h"
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
extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans, uint64_t nMaxBytes);

CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

BOOST_FIXTURE_TEST_SUITE(DoS_tests, TestingSetup)

size_t GetNumberBanEntries()
{
    banmap_t banmap;
    dosMan.GetBanned(banmap);
    return banmap.size();
}

BOOST_AUTO_TEST_CASE(DoS_basic_ban_tests)
{
    // Ensure in-memory banlist is empty
    dosMan.ClearBanned();
    BOOST_CHECK(GetNumberBanEntries() == 0);

    // Add a CNetAddr entry to banlist
    dosMan.Ban(CNetAddr("192.168.1.1"), BanReasonManuallyAdded, DEFAULT_MISBEHAVING_BANTIME, false);
    // Add a CSubNet entry to banlist
    dosMan.Ban(CSubNet("10.168.1.0/28"), BanReasonNodeMisbehaving, DEFAULT_MISBEHAVING_BANTIME, false);
    // Ensure we have exactly 2 entries in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 2);

    // Verify IsBanned works for single IP directly specified
    BOOST_CHECK(dosMan.IsBanned(CNetAddr("192.168.1.1")));
    // Verify IsBanned works for single IP not banned
    BOOST_CHECK(!dosMan.IsBanned(CNetAddr("192.168.1.2")));
    // Verify IsBanned works for single IP banned as part of subnet
    BOOST_CHECK(dosMan.IsBanned(CNetAddr("10.168.1.1")));
    // Verify IsBanned works for single IP not banned as part of subnet
    BOOST_CHECK(!dosMan.IsBanned(CNetAddr("10.168.1.19")));
    // Verify IsBanned works for subnet exact match
    BOOST_CHECK(dosMan.IsBanned(CSubNet("10.168.1.0/28")));
    // Verify IsBanned works for subnet not banned
    BOOST_CHECK(!dosMan.IsBanned(CSubNet("10.168.1.64/30")));

    // REVISIT: Currently subnets require EXACT matches, so the encompassed case should return not banned.
    BOOST_CHECK(!dosMan.IsBanned(CSubNet("10.168.1.4/30")));

    // Verify unbanning an IP not banned doesn't change banlist contents
    dosMan.Unban(CNetAddr("192.168.10.1"));
    // Ensure we still have exactly 2 entries in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 2);

    // Verify unbanning an IP that is within a subnet, but not directly banned, doesn't
    // change our banlist conents
    dosMan.Unban(CNetAddr("10.168.1.1"));
    // Ensure we still have exactly 2 entries in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 2);
    // Verify that the IP we just "unbanned" still shows as banned since it still falls within banned subnet
    BOOST_CHECK(dosMan.IsBanned(CNetAddr("10.168.1.1")));

    // Verify that unbanning an IP that is banned works
    dosMan.Unban(CNetAddr("192.168.1.1"));
    // Ensure we now have exactly 1 entry in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 1);

    // Verify that unbanning a subnet that is inside a banned subnet doesn't change our banlist contents
    dosMan.Unban(CSubNet("10.168.1.4/30"));
    // Ensure we still have exactly 1 entry in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 1);

    // Verify that unbanning a subnet that encompasses a banned subnet doesn't change our banlist conents
    dosMan.Unban(CSubNet("10.168.1.0/24"));
    // Ensure we still have exactly 1 entry in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 1);

    // Verify that unbanning a subnet that exactly matches a banned subnet updates our banlist conents
    dosMan.Unban(CSubNet("10.168.1.0/28"));
    // Ensure we now have exactly 0 entries in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 0);

    // Re-add ban entries so we can test ClearBanned()
    dosMan.Ban(CNetAddr("192.168.1.1"), BanReasonManuallyAdded, DEFAULT_MISBEHAVING_BANTIME, false);
    dosMan.Ban(CSubNet("10.168.1.0/28"), BanReasonNodeMisbehaving, DEFAULT_MISBEHAVING_BANTIME, false);
    // Ensure we have exactly 2 entries in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 2);

    // Clear the in-memory banlist
    dosMan.ClearBanned();
    // Ensure in-memory banlist is now empty
    BOOST_CHECK(GetNumberBanEntries() == 0);
}

BOOST_AUTO_TEST_CASE(DoS_misbehaving_ban_tests)
{
    dosMan.ClearBanned();
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    dosMan.Misbehaving(dummyNode1.GetId(), 100); // Should get banned
    SendMessages(&dummyNode1);
    BOOST_CHECK(dosMan.IsBanned(addr1));
    BOOST_CHECK(!dosMan.IsBanned(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002));
    CNode dummyNode2(INVALID_SOCKET, addr2, "", true);
    dummyNode2.nVersion = 1;
    dosMan.Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2);
    BOOST_CHECK(!dosMan.IsBanned(addr2)); // 2 not banned yet...
    BOOST_CHECK(dosMan.IsBanned(addr1));  // ... but 1 still should be
    dosMan.Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2);
    BOOST_CHECK(dosMan.IsBanned(addr2));
}

BOOST_AUTO_TEST_CASE(DoS_non_default_banscore)
{
    dosMan.ClearBanned();
    mapArgs["-banscore"] = "111"; // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    dosMan.Misbehaving(dummyNode1.GetId(), 100);
    SendMessages(&dummyNode1);
    BOOST_CHECK(!dosMan.IsBanned(addr1));
    dosMan.Misbehaving(dummyNode1.GetId(), 10);
    SendMessages(&dummyNode1);
    BOOST_CHECK(!dosMan.IsBanned(addr1));
    dosMan.Misbehaving(dummyNode1.GetId(), 1);
    SendMessages(&dummyNode1);
    BOOST_CHECK(dosMan.IsBanned(addr1));
    mapArgs.erase("-banscore");
}

BOOST_AUTO_TEST_CASE(DoS_bantime_expiration)
{
    dosMan.ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ip(0xa0b0c001));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = 1;

    dosMan.Misbehaving(dummyNode.GetId(), 100);
    SendMessages(&dummyNode);
    BOOST_CHECK(dosMan.IsBanned(addr));

    // Verify that SweepBanned does not remove the entry
    dosMan.SweepBanned();
    // Ensure we still have exactly 1 entry in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 1);

    SetMockTime(nStartTime+60*60);
    BOOST_CHECK(dosMan.IsBanned(addr));

    // Verify that SweepBanned still does not remove the entry
    dosMan.SweepBanned();
    // Ensure we still have exactly 1 entry in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 1);

    SetMockTime(nStartTime+60*60*24+1);
    BOOST_CHECK(!dosMan.IsBanned(addr));

    // Verify that SweepBanned does remove the entry this time as it is expired
    dosMan.SweepBanned();
    // Ensure we now have exactly 0 entries in our banlist
    BOOST_CHECK(GetNumberBanEntries() == 0);
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

    // Test LimitOrphanTxSize() function: limit by orphan pool bytes
    // add 50 orphan transactions:
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

    {
        LOCK(cs_orphancache);
        LimitOrphanTxSize(50, 8000);
        BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 50);
        LimitOrphanTxSize(50, 6300);
        BOOST_CHECK(mapOrphanTransactions.size() <= 49);
        LimitOrphanTxSize(50, 1000);
        BOOST_CHECK(mapOrphanTransactions.size() <= 8);
        LimitOrphanTxSize(50, 0);
        BOOST_CHECK(mapOrphanTransactions.empty());
    }

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

    // Test LimitOrphanTxSize() function: limit by number of txns
    {
        LOCK(cs_orphancache);
        LimitOrphanTxSize(40, 10000000);
        BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 40);
        LimitOrphanTxSize(10, 10000000);
        BOOST_CHECK_EQUAL(mapOrphanTransactions.size(), 10);
        LimitOrphanTxSize(0, 10000000);
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

        // Advance the clock DEFAULT_ORPHANPOOL_EXPIRY hours
        SetMockTime(nStartTime+60*60*DEFAULT_ORPHANPOOL_EXPIRY);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        /** Test the boundary where orphans should get purged. **/
        // Advance the clock DEFAULT_ORPHANPOOL_EXPIRY hours plus 4 minutes 59 seconds
        SetMockTime(nStartTime+60*60*DEFAULT_ORPHANPOOL_EXPIRY + 299);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 50);

        // Advance the clock DEFAULT_ORPHANPOOL_EXPIRY hours plus 5 minutes
        SetMockTime(nStartTime+60*60*DEFAULT_ORPHANPOOL_EXPIRY + 300);
        EraseOrphansByTime();
        BOOST_CHECK(mapOrphanTransactions.size() == 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
