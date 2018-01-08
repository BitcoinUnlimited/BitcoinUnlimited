// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/validation.h"
#include "key.h"
#include "main.h"
#include "miner.h"
#include "parallel.h"
#include "pubkey.h"
#include "random.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"
#include "txmempool.h"
#include "utiltime.h"

#include <boost/test/unit_test.hpp>

extern bool AddOrphanTx(const CTransaction &tx, NodeId peer);
extern void EraseOrphansByTime();
extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans, uint64_t nMaxBytes);
extern void LimitMempoolSize(CTxMemPool &pool, size_t limit, unsigned long age);

BOOST_AUTO_TEST_SUITE(txvalidationcache_tests) // BU harmonize suite name with filename

static bool ToMemPool(CMutableTransaction &tx)
{
    LOCK(cs_main);

    CValidationState state;
    bool fMissingInputs = false;
    return AcceptToMemoryPool(mempool, state, tx, false, &fMissingInputs, true, false);
}

BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend, TestChain100Setup)
{
    // Make sure skipping validation of transctions that were
    // validated going into the memory pool does not allow
    // double-spends in blocks to pass validation when they should not.

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;


    unsigned int sighashType = SIGHASH_ALL;
    if (chainActive.Tip()->IsforkActiveOnNextBlock(miningForkTime.value))
        sighashType |= SIGHASH_FORKID;

    // Create a double-spend of mature coinbase txn:
    std::vector<CMutableTransaction> spends;
    spends.resize(2);
    for (int i = 0; i < 2; i++)
    {
        spends[i].vin.resize(1);
        spends[i].vin[0].prevout.hash = coinbaseTxns[0].GetHash();
        spends[i].vin[0].prevout.n = 0;
        spends[i].vout.resize(1);
        spends[i].vout[0].nValue = 11 * CENT;
        spends[i].vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(scriptPubKey, spends[i], 0, sighashType, coinbaseTxns[0].vout[0].nValue, 0);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)sighashType);
        spends[i].vin[0].scriptSig << vchSig;
    }

    CBlock block;

    // Test 1: block with both of those transactions should be rejected.
    block = CreateAndProcessBlock(spends, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());

    // Test 2: ... and should be rejected if spend1 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[0]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
    mempool.clear();

    // Test 3: ... and should be rejected if spend2 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
    mempool.clear();

    // Final sanity test: first spend in mempool, second in block, that's OK:
    std::vector<CMutableTransaction> oneSpend;
    oneSpend.push_back(spends[0]);
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(oneSpend, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    // spends[1] should have been removed from the mempool when the
    // block with spends[0] is accepted:
    BOOST_CHECK_EQUAL(mempool.size(), 0);
    mempool.clear();
}

BOOST_FIXTURE_TEST_CASE(uncache_coins, TestChain100Setup)
{
    int64_t nStartTime = GetTime();
    nLastOrphanCheck = nStartTime;
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    mempool.clear();
    pcoinsTip->Flush();

    // Make sure coins are uncached when txns are not accepted into the memory pool
    // and also verify they are uncached when orphans or txns are evicted from either the
    // orphan cache or the transaction memory pool.
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    unsigned int sighashType = SIGHASH_ALL;
    if (chainActive.Tip()->IsforkActiveOnNextBlock(miningForkTime.value))
        sighashType |= SIGHASH_FORKID;

    std::vector<CMutableTransaction> spends;

    // Add valid txns to the memory pool.  The coins should be present in the coins cache.
    spends.resize(1);
    spends[0].vin.resize(1);
    spends[0].vin[0].prevout.hash = coinbaseTxns[0].GetHash();
    spends[0].vin[0].prevout.n = 0;
    spends[0].vout.resize(1);
    spends[0].vout[0].nValue = 11 * CENT;
    spends[0].vout[0].scriptPubKey = scriptPubKey;

    // Sign:
    std::vector<unsigned char> vchSig1;
    uint256 hash1 = SignatureHash(scriptPubKey, spends[0], 0, sighashType, coinbaseTxns[0].vout[0].nValue, 0);
    BOOST_CHECK(coinbaseKey.Sign(hash1, vchSig1));
    vchSig1.push_back((unsigned char)sighashType);
    spends[0].vin[0].scriptSig << vchSig1;

    BOOST_CHECK(ToMemPool(spends[0]));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout));

    // Try to add the same tx to the memory pool. The coins should still be present.
    BOOST_CHECK(!ToMemPool(spends[0]));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout));

    // Try to add an invalid txn to the memory pool.  The coins for the previous txn should
    // still be present and but the coins from the rejected txn should not be present.
    spends.resize(2);
    spends[1].vin.resize(1);
    spends[1].vin[0].prevout.hash = coinbaseTxns[1].GetHash();
    spends[1].vin[0].prevout.n = 0;
    spends[1].vout.resize(1);
    spends[1].vout[0].nValue = 11 * CENT;
    spends[1].vout[0].scriptPubKey = scriptPubKey;

    // Sign:
    std::vector<unsigned char> vchSig2;
    uint256 hash2 = SignatureHash(scriptPubKey, spends[1], 0, sighashType, coinbaseTxns[1].vout[0].nValue, 0);
    BOOST_CHECK(coinbaseKey.Sign(hash2, vchSig2));
    vchSig2.push_back((unsigned char)sighashType);
    spends[1].vin[0].scriptSig << vchSig2;

    BOOST_CHECK(!ToMemPool(spends[1]));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout)); // not uncached because from a previous txn
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[1].vin[0].prevout));

    // Add an orphan to the orphan cache.  The valid inputs should be present in the coins cache.
    spends.resize(3);
    spends[2].vin.resize(3);
    spends[2].vin[0].prevout.hash = GetRandHash();
    spends[2].vin[0].prevout.n = 0;
    spends[2].vin[1].prevout.hash = GetRandHash();
    spends[2].vin[1].prevout.n = 0;
    spends[2].vin[2].prevout.hash = coinbaseTxns[2].GetHash();
    spends[2].vin[2].prevout.n = 0;
    spends[2].vout.resize(1);
    spends[2].vout[0].nValue = 799999999;
    spends[2].vout[0].scriptPubKey = scriptPubKey;

    // Sign:
    std::vector<unsigned char> vchSig3;
    uint256 hash3 = SignatureHash(scriptPubKey, spends[2], 0, sighashType, coinbaseTxns[2].vout[0].nValue, 0);
    BOOST_CHECK(coinbaseKey.Sign(hash3, vchSig3));
    vchSig3.push_back((unsigned char)sighashType);
    spends[2].vin[0].scriptSig << vchSig2;

    BOOST_CHECK(!ToMemPool(spends[2]));
    {
        LOCK(cs_orphancache);
        BOOST_CHECK(AddOrphanTx(spends[2], 1));
    }
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout)); // valid coin from previous txn
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[0].prevout));
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[1].prevout));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[2].vin[2].prevout)); // the only valid coin from the orphantx

    // Remove valid orphans by time.  The coins should be removed from the coins cache
    {
        LOCK(cs_orphancache);
        SetMockTime(nStartTime + 3600 * DEFAULT_ORPHANPOOL_EXPIRY + 300);
        EraseOrphansByTime();
    }

    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout)); // valid coin from previous txn
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[2].prevout)); // the valid coin from orphantx is uncached

    // Remove valid orphans by size.  The coins should be removed from the coins cache
    BOOST_CHECK(!ToMemPool(spends[2]));
    {
        LOCK(cs_orphancache);
        BOOST_CHECK(AddOrphanTx(spends[2], 1));
    }
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout)); // valid coin from previous txn
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[0].prevout));
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[1].prevout));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[2].vin[2].prevout)); // the only valid coin from the orphantx

    {
        LOCK(cs_orphancache);
        LimitOrphanTxSize(0, 0);
    }

    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout)); // valid coin from previous txn
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[2].prevout)); // the valid coin from orphantx is uncached

    // Evict the valid previous tx, by time.  The coins should be removed from the coins cache
    SetMockTime(nStartTime + 1 + 72 * 60 * 60); // move to 1 second beyond time to evict
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout)); // valid coin from previous txn
    LimitMempoolSize(mempool, 100 * 1000 * 1000, 72 * 60 * 60);
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout)); // valid coin from previous txn

    // Add a tx to the memory pool.  The valid inputs should be present in the coins cache.
    spends.resize(4);
    spends[3].vin.resize(1);
    spends[3].vin[0].prevout.hash = coinbaseTxns[0].GetHash();
    spends[3].vin[0].prevout.n = 0;
    spends[3].vout.resize(1);
    spends[3].vout[0].nValue = 11 * CENT;
    spends[3].vout[0].scriptPubKey = scriptPubKey;

    // Sign:
    std::vector<unsigned char> vchSig4;
    uint256 hash4 = SignatureHash(scriptPubKey, spends[3], 0, sighashType, coinbaseTxns[3].vout[0].nValue, 0);
    BOOST_CHECK(coinbaseKey.Sign(hash4, vchSig4));
    vchSig4.push_back((unsigned char)sighashType);
    spends[3].vin[0].scriptSig << vchSig4;

    BOOST_CHECK(ToMemPool(spends[3]));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout));

    // Evict a valid tx by size of memory pool.  The coins should be removed from the coins cache
    SetMockTime(nStartTime + 1); // change start time so we are well within the limits
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout)); // valid coin from previous txn
    LimitMempoolSize(mempool, 0, 72 * 60 * 60); // limit mempool size to zero
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout)); // valid coin from previous txn

    /**  Simulate the following scenario:
     *     Add an orphan to the orphan pool
     *     then add the parent to the mempool which causes the orphan to also be pulled into the mempool.
     *     then delete the orphan using EraseOrphanTx(hash).
     *   Result: All coins should still be present in cache.
     */

    // Add an orphan to the orphan cache.  The valid inputs should be present in the coins cache.
    spends.resize(5);
    spends[4].vin.resize(3);
    spends[4].vin[0].prevout.hash = GetRandHash();
    spends[4].vin[0].prevout.n = 0;
    spends[4].vin[1].prevout.hash = GetRandHash();
    spends[4].vin[1].prevout.n = 0;
    spends[4].vin[2].prevout.hash = coinbaseTxns[5].GetHash();
    spends[4].vin[2].prevout.n = 0;
    spends[4].vout.resize(1);
    spends[4].vout[0].nValue = 799999999;
    spends[4].vout[0].scriptPubKey = scriptPubKey;

    // Sign:
    std::vector<unsigned char> vchSig5;
    uint256 hash5 = SignatureHash(scriptPubKey, spends[2], 0, sighashType, coinbaseTxns[5].vout[0].nValue, 0);
    BOOST_CHECK(coinbaseKey.Sign(hash5, vchSig5));
    vchSig5.push_back((unsigned char)sighashType);
    spends[4].vin[0].scriptSig << vchSig5;

    BOOST_CHECK(!ToMemPool(spends[4]));
    {
        LOCK(cs_orphancache);
        BOOST_CHECK(AddOrphanTx(spends[4], 1));
    }
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[4].vin[0].prevout));
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[4].vin[1].prevout));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[4].vin[2].prevout)); // the only valid coin from the orphantx

    // All we need to do to simluate the above scenario is now erase the orphan tx from the orphan cache as it
    // would be if the orphan was moved into the mempool.
    // Result: All the coins should still be remaining in the coins cache.
    {
        LOCK(cs_orphancache);
        EraseOrphanTx(spends[4].GetHash());
    }
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[4].vin[2].prevout));


    // cleanup
    mempool.clear();
    mapOrphanTransactions.clear();
    pcoinsTip->Flush();
    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
