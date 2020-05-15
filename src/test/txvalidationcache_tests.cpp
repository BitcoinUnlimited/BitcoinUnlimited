// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockstorage/blockstorage.h"
#include "consensus/validation.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "miner.h"
#include "parallel.h"
#include "pubkey.h"
#include "random.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"
#include "txadmission.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "utiltime.h"
#include "validation/forks.h"

#include <boost/test/unit_test.hpp>

extern void LimitMempoolSize(CTxMemPool &pool, size_t limit, unsigned long age);

BOOST_AUTO_TEST_SUITE(txvalidationcache_tests) // BU harmonize suite name with filename

static bool ToMemPool(CMutableTransaction &tx, std::string rejectReason = "")
{
    CValidationState state;
    bool fMissingInputs = false;
    bool ret = false;
    ret = AcceptToMemoryPool(mempool, state, MakeTransactionRef(tx), false, &fMissingInputs, true, false);

    if (rejectReason != "")
        BOOST_CHECK_EQUAL(rejectReason, state.GetRejectReason());
    return ret;
}

BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend, TestChain100Setup)
{
    // Make sure skipping validation of transctions that were
    // validated going into the memory pool does not allow
    // double-spends in blocks to pass validation when they should not.

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;


    unsigned int sighashType = SIGHASH_ALL;
    if (IsUAHFforkActiveOnNextBlock(chainActive.Tip()->nHeight))
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
        std::vector<uint8_t> vchSig;
        uint256 hash = SignatureHash(scriptPubKey, spends[i], 0, sighashType, coinbaseTxns[0].vout[0].nValue, 0);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig));
        vchSig.push_back((uint8_t)sighashType);
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

BOOST_FIXTURE_TEST_CASE(cache_configuration, TestChain100Setup)
{
    // check that default values are returned
    CacheConfig cacheConfig1 = DiscoverCacheConfiguration(true);
    BOOST_CHECK(cacheConfig1.nBlockDBCache == 0);
    BOOST_CHECK(cacheConfig1.nBlockUndoDBCache == 0);
    BOOST_CHECK(cacheConfig1.nBlockTreeDBCache == 2097152);
    BOOST_CHECK(cacheConfig1.nTxIndexCache == 0);
    BOOST_CHECK(cacheConfig1.nCoinDBCache == 73662464);
    BOOST_CHECK(nCoinCacheMaxSize == 448528384);

    // Check non-default values are returned
    CacheConfig cacheConfig2 = DiscoverCacheConfiguration();
    BOOST_CHECK(cacheConfig2.nBlockDBCache == 0);
    BOOST_CHECK(cacheConfig2.nBlockUndoDBCache == 0);
    BOOST_CHECK(cacheConfig2.nBlockTreeDBCache == 655360);
    BOOST_CHECK(cacheConfig2.nTxIndexCache == 0);
    BOOST_CHECK(cacheConfig2.nCoinDBCache == 1146880);
    BOOST_CHECK(nCoinCacheMaxSize == 3440640);


    // check default values are honored if blockdb storage is on
    BLOCK_DB_MODE = LEVELDB_BLOCK_STORAGE;
    cacheConfig1 = DiscoverCacheConfiguration(true);
    BOOST_CHECK(cacheConfig1.nBlockDBCache == 52219084);
    BOOST_CHECK(cacheConfig1.nBlockUndoDBCache == 10443816);
    BOOST_CHECK(cacheConfig1.nBlockTreeDBCache == 2097152);
    BOOST_CHECK(cacheConfig1.nTxIndexCache == 0);
    BOOST_CHECK(cacheConfig1.nCoinDBCache == 65829601);
    BOOST_CHECK(nCoinCacheMaxSize == 393698347);

    // check settings when txindex is on
    bool nTemp = GetBoolArg("-txindex", 0);
    SetBoolArg("-txindex", true);
    cacheConfig1 = DiscoverCacheConfiguration(true);
    BOOST_CHECK(cacheConfig1.nBlockDBCache == 52219084);
    BOOST_CHECK(cacheConfig1.nBlockUndoDBCache == 10443816);
    BOOST_CHECK(cacheConfig1.nBlockTreeDBCache == 2097152);
    BOOST_CHECK(cacheConfig1.nTxIndexCache == 32914800);
    BOOST_CHECK(cacheConfig1.nCoinDBCache == 32914800);
    BOOST_CHECK(nCoinCacheMaxSize == 393698348);

    // Check non-default values are returned
    cacheConfig2 = DiscoverCacheConfiguration();
    BOOST_CHECK(cacheConfig2.nBlockDBCache == 655360);
    BOOST_CHECK(cacheConfig2.nBlockUndoDBCache == 655360);
    BOOST_CHECK(cacheConfig2.nBlockTreeDBCache == 655360);
    BOOST_CHECK(cacheConfig2.nTxIndexCache == 409600);
    BOOST_CHECK(cacheConfig2.nCoinDBCache == 409600);
    BOOST_CHECK(nCoinCacheMaxSize == 2457600);

    // Cleanup
    SetBoolArg("-txindex", nTemp);
}

BOOST_FIXTURE_TEST_CASE(uncache_coins, TestChain100Setup)
{
    int64_t nStartTime = GetTime();
    orphanpool.SetLastOrphanCheck(nStartTime);
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    mempool.clear();
    pcoinsTip->Flush();

    bool fSpent = false;

    // Make sure coins are uncached when txns are not accepted into the memory pool
    // and also verify they are uncached when orphans or txns are evicted from either the
    // orphan cache or the transaction memory pool.
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    unsigned int sighashType = SIGHASH_ALL;
    if (IsUAHFforkActiveOnNextBlock(chainActive.Tip()->nHeight))
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
    BOOST_CHECK(hash1 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(coinbaseKey.SignECDSA(hash1, vchSig1));
    vchSig1.push_back((unsigned char)sighashType);
    spends[0].vin[0].scriptSig << vchSig1;

    BOOST_CHECK(ToMemPool(spends[0]));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent));
    BOOST_CHECK(fSpent == false);

    // Try to add the same tx to the memory pool. The coins should still be present.
    BOOST_CHECK(!ToMemPool(spends[0], "txn-already-in-mempool"));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent));
    BOOST_CHECK(fSpent == false);

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
    BOOST_CHECK(hash2 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(coinbaseKey.SignECDSA(hash2, vchSig2));
    vchSig2.push_back((unsigned char)sighashType);
    spends[1].vin[0].scriptSig << vchSig2;

    BOOST_CHECK(!ToMemPool(spends[1], "bad-txns-premature-spend-of-coinbase"));
    // not uncached because from a previous txn
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent));
    BOOST_CHECK(fSpent == false);
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[1].vin[0].prevout, fSpent));

    // Add an orphan to the orphan cache.  The valid inputs should be present in the coins cache.
    spends.resize(3);
    spends[2].vin.resize(3);
    spends[2].vin[2].prevout.hash = InsecureRand256();
    spends[2].vin[2].prevout.n = 0;
    spends[2].vin[1].prevout.hash = InsecureRand256();
    spends[2].vin[1].prevout.n = 0;
    spends[2].vin[0].prevout.hash = coinbaseTxns[2].GetHash();
    spends[2].vin[0].prevout.n = 0;
    spends[2].vout.resize(1);
    spends[2].vout[0].nValue = 799999999;
    spends[2].vout[0].scriptPubKey = scriptPubKey;

    // Sign:
    std::vector<unsigned char> vchSig3;
    uint256 hash3 = SignatureHash(scriptPubKey, spends[2], 0, sighashType, coinbaseTxns[2].vout[0].nValue, 0);
    BOOST_CHECK(hash3 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(coinbaseKey.SignECDSA(hash3, vchSig3));
    vchSig3.push_back((unsigned char)sighashType);
    spends[2].vin[0].scriptSig << vchSig2;

    BOOST_CHECK(!ToMemPool(spends[2]));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[2].vin[0].prevout, fSpent)); // the only valid coin from the orphantx
    BOOST_CHECK(fSpent == false);
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        BOOST_CHECK(orphanpool.AddOrphanTx(MakeTransactionRef(spends[2]), 1));
    }
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == false);
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[2].prevout, fSpent));
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[1].prevout, fSpent));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[2].vin[0].prevout, fSpent)); // the only valid coin from the orphantx
    BOOST_CHECK(fSpent == false);

    // Remove valid orphans by time.  The coins should be removed from the coins cache
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        SetMockTime(nStartTime + 3600 * DEFAULT_ORPHANPOOL_EXPIRY + 300);
        orphanpool.EraseOrphansByTime();
    }

    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == false);
    // the valid coin from orphantx is uncached
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[2].prevout, fSpent));

    // Remove valid orphans by size.  The coins should be removed from the coins cache
    BOOST_CHECK(!ToMemPool(spends[2]));
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        BOOST_CHECK(orphanpool.AddOrphanTx(MakeTransactionRef(spends[2]), 1));
    }
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == false);
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[2].prevout, fSpent));
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[1].prevout, fSpent));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[2].vin[0].prevout, fSpent)); // the only valid coin from the orphantx
    BOOST_CHECK(fSpent == false);

    {
        WRITELOCK(orphanpool.cs_orphanpool);
        orphanpool.LimitOrphanTxSize(0, 0);
    }

    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == false);
    // the valid coin from orphantx is uncached
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[2].vin[0].prevout, fSpent));

    // Evict the valid previous tx, by time.  The coins should be removed from the coins cache
    SetMockTime(nStartTime + 1 + 72 * 60 * 60); // move to 1 second beyond time to evict
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == false);
    LimitMempoolSize(mempool, 100 * 1000 * 1000, 72 * 60 * 60);
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[0].vin[0].prevout, fSpent)); // valid coin from previous txn

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
    BOOST_CHECK(hash4 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(coinbaseKey.SignECDSA(hash4, vchSig4));
    vchSig4.push_back((unsigned char)sighashType);
    spends[3].vin[0].scriptSig << vchSig4;

    BOOST_CHECK(ToMemPool(spends[3]));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout, fSpent));

    // Evict a valid tx by size of memory pool.  The coins should be removed from the coins cache
    SetMockTime(nStartTime + 1); // change start time so we are well within the limits
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == false);
    LimitMempoolSize(mempool, 0, 72 * 60 * 60); // limit mempool size to zero
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout, fSpent)); // valid coin from previous txn

    // Test getting a coin that is not in the cache.
    BOOST_CHECK(pcoinsTip->GetCoinFromDB(spends[3].vin[0].prevout));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == false);

    // Spend the coin and then check the results from HaveCoinInCached()
    Coin *pcoin = nullptr;
    pcoinsTip->SpendCoin(spends[3].vin[0].prevout, pcoin);
    fSpent = false;
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[3].vin[0].prevout, fSpent)); // valid coin from previous txn
    BOOST_CHECK(fSpent == true);

    /**  Simulate the following scenario:
     *     Add an orphan to the orphan pool
     *     then add the parent to the mempool which causes the orphan to also be pulled into the mempool.
     *     then delete the orphan using EraseOrphanTx(hash).
     *   Result: All coins should still be present in cache.
     */

    // Add an orphan to the orphan cache.  The valid inputs should be present in the coins cache.
    spends.resize(5);
    spends[4].vin.resize(3);
    spends[4].vin[2].prevout.hash = InsecureRand256();
    spends[4].vin[2].prevout.n = 0;
    spends[4].vin[1].prevout.hash = InsecureRand256();
    spends[4].vin[1].prevout.n = 0;
    spends[4].vin[0].prevout.hash = coinbaseTxns[5].GetHash();
    spends[4].vin[0].prevout.n = 0;
    spends[4].vout.resize(1);
    spends[4].vout[0].nValue = 799999999;
    spends[4].vout[0].scriptPubKey = scriptPubKey;

    // Sign:
    std::vector<unsigned char> vchSig5;
    uint256 hash5 = SignatureHash(scriptPubKey, spends[2], 0, sighashType, coinbaseTxns[5].vout[0].nValue, 0);
    BOOST_CHECK(hash5 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(coinbaseKey.SignECDSA(hash5, vchSig5));
    vchSig5.push_back((unsigned char)sighashType);
    spends[4].vin[0].scriptSig << vchSig5;

    BOOST_CHECK(!ToMemPool(spends[4]));
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        BOOST_CHECK(orphanpool.AddOrphanTx(MakeTransactionRef(spends[4]), 1));
    }
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[4].vin[2].prevout, fSpent));
    BOOST_CHECK(!pcoinsTip->HaveCoinInCache(spends[4].vin[1].prevout, fSpent));
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[4].vin[0].prevout, fSpent)); // the only valid coin from the orphantx
    BOOST_CHECK(fSpent == false);

    // All we need to do to simluate the above scenario is now erase the orphan tx from the orphan cache as it
    // would be if the orphan was moved into the mempool.
    // Result: All the coins should still be remaining in the coins cache.
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        orphanpool.EraseOrphanTx(spends[4].GetHash());
    }
    BOOST_CHECK(pcoinsTip->HaveCoinInCache(spends[4].vin[0].prevout, fSpent));
    BOOST_CHECK(fSpent == false);


    // cleanup
    mempool.clear();
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        orphanpool.mapOrphanTransactions.clear();
    }
    pcoinsTip->Flush();
    SetMockTime(0);
}

BOOST_FIXTURE_TEST_CASE(long_unconfirmed_chains, TestChain100Setup)
{
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // Get 1 more spendable coinbase tx
    std::vector<CMutableTransaction> noTxns;
    CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
    coinbaseTxns.push_back(*b.vtx[0]);

    unsigned int sighashType = SIGHASH_ALL;
    if (IsUAHFforkActiveOnNextBlock(chainActive.Tip()->nHeight))
        sighashType |= SIGHASH_FORKID;

    uint256 prevout = coinbaseTxns[0].GetHash();
    uint256 hash;

    // Create a chain of 50 unconfirmed transactions
    SetArg("-limitancestorcount", std::to_string(50));
    SetArg("-limitdescendantcount", std::to_string(50));
    for (int i = 1; i <= 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = prevout;
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 11 * CENT;
        tx.vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        if (i == 1)
        {
            hash = SignatureHash(scriptPubKey, tx, 0, sighashType, coinbaseTxns[0].vout[0].nValue, 0);
        }
        else
        {
            hash = SignatureHash(scriptPubKey, tx, 0, sighashType, 11 * CENT, 0);
        }
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig));
        vchSig.push_back((unsigned char)sighashType);
        tx.vin[0].scriptSig << vchSig;
        BOOST_CHECK(ToMemPool(tx));

        prevout = tx.GetHash();
    }


    // Add one more which should fail because it's over the 50 limit.
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = prevout;
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 11 * CENT;
        tx.vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        hash = SignatureHash(scriptPubKey, tx, 0, sighashType, 11 * CENT, 0);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig));
        vchSig.push_back((unsigned char)sighashType);
        tx.vin[0].scriptSig << vchSig;
        BOOST_CHECK(!ToMemPool(tx, "too-long-mempool-chain"));
    }

    SetArg("-limitancestorcount", std::to_string(52));
    SetArg("-limitdescendantcount", std::to_string(52));

    // Add one more which should should work because the limit is now 52
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = prevout;
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 11 * CENT;
        tx.vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        hash = SignatureHash(scriptPubKey, tx, 0, sighashType, 11 * CENT, 0);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig));
        vchSig.push_back((unsigned char)sighashType);
        tx.vin[0].scriptSig << vchSig;
        BOOST_CHECK(ToMemPool(tx));

        prevout = tx.GetHash();
    }

    // Now try to add a tx with multiple inputs.  It should fail
    {
        CMutableTransaction tx;
        tx.vin.resize(2);
        tx.vin[0].prevout.hash = prevout;
        tx.vin[0].prevout.n = 0;
        tx.vin[1].prevout.hash = coinbaseTxns[1].GetHash();
        tx.vin[1].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 11 * CENT;
        tx.vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        hash = SignatureHash(scriptPubKey, tx, 0, sighashType, 11 * CENT, 0);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig));
        vchSig.push_back((unsigned char)sighashType);
        tx.vin[0].scriptSig << vchSig;

        std::vector<unsigned char> vchSig1;
        hash = SignatureHash(scriptPubKey, tx, 1, sighashType, coinbaseTxns[1].vout[0].nValue, 0);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig1));
        vchSig1.push_back((unsigned char)sighashType);
        tx.vin[1].scriptSig << vchSig1;

        ToMemPool(tx, "bad-txn-too-many-inputs");
        BOOST_CHECK(!ToMemPool(tx, "bad-txn-too-many-inputs"));
    }

    // Now try to add a tx with only one input. It should succeed.
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = prevout;
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 11 * CENT;
        tx.vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        hash = SignatureHash(scriptPubKey, tx, 0, sighashType, 11 * CENT, 0);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig));
        vchSig.push_back((unsigned char)sighashType);
        tx.vin[0].scriptSig << vchSig;
        BOOST_CHECK(ToMemPool(tx));

        prevout = tx.GetHash();
    }

    // Now try to add one more tx with only one input. It should fail because
    // we are over the limit of 52.
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = prevout;
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 11 * CENT;
        tx.vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        hash = SignatureHash(scriptPubKey, tx, 0, sighashType, 11 * CENT, 0);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        BOOST_CHECK(coinbaseKey.SignECDSA(hash, vchSig));
        vchSig.push_back((unsigned char)sighashType);
        tx.vin[0].scriptSig << vchSig;
        BOOST_CHECK(!ToMemPool(tx, "too-long-mempool-chain"));
    }
}
BOOST_AUTO_TEST_SUITE_END()
