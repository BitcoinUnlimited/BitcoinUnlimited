// Copyright (c) 2014-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"
#include "random.h"
#include "uint256.h"
#include "test/test_bitcoin.h"
#include "main.h"
#include "consensus/validation.h"
#include "undo.h"

#include <vector>
#include <map>

#include <boost/test/unit_test.hpp>

int ApplyTxInUndo(Coin&& undo, CCoinsViewCache& view, const COutPoint& out);
void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight);

namespace
{
//! equality test
bool operator==(const Coin &a, const Coin &b) {
    // Empty Coin objects are always equal.
    if (a.IsPruned() && b.IsPruned()) return true;
    return a.fCoinBase == b.fCoinBase &&
           a.nHeight == b.nHeight &&
           a.out == b.out;
}

class CCoinsViewTest : public CCoinsView
{
    uint256 hashBestBlock_;
    std::map<uint256, CCoins> map_;

public:
    bool GetCoins(const uint256& txid, CCoins& coins) const
    {
        std::map<uint256, CCoins>::const_iterator it = map_.find(txid);
        if (it == map_.end()) {
            return false;
        }
        coins = it->second;
        if (coins.IsPruned() && insecure_rand() % 2 == 0) {
            // Randomly return false in case of an empty entry.
            return false;
        }
        return true;
    }

    bool HaveCoins(const uint256& txid) const
    {
        CCoins coins;
        return GetCoins(txid, coins);
    }

    uint256 GetBestBlock() const { return hashBestBlock_; }

    bool BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock, size_t &nChildCachedCoinsUsage)
    {
        for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end(); ) {
            if (it->second.flags & CCoinsCacheEntry::DIRTY) {
                // Same optimization used in CCoinsViewDB is to only write dirty entries.
                map_[it->first] = it->second.coins;
                if (it->second.coins.IsPruned() && insecure_rand() % 3 == 0) {
                    // Randomly delete empty entries on write.
                    map_.erase(it->first);
                }
                nChildCachedCoinsUsage -= it->second.coins.DynamicMemoryUsage();
                mapCoins.erase(it++);
            }
            else
                it++;
        }
        if (!hashBlock.IsNull())
            hashBestBlock_ = hashBlock;
        return true;
    }

    bool GetStats(CCoinsStats& stats) const { return false; }
};

class CCoinsViewCacheTest : public CCoinsViewCache
{
public:
    explicit CCoinsViewCacheTest(CCoinsView* _base) : CCoinsViewCache(_base) {}

    void SelfTest() const
    {
        // Manually recompute the dynamic usage of the whole data, and compare it.
        size_t ret = memusage::DynamicUsage(cacheCoins);
        size_t count = 0;
        for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++) {
            ret += it->second.coins.DynamicMemoryUsage();
            ++count;
        }
        BOOST_CHECK_EQUAL(GetCacheSize(), count);
        BOOST_CHECK_EQUAL(DynamicMemoryUsage(), ret);
    }

    CCoinsMap& map() const { return cacheCoins; }
    size_t& usage() const { return cachedCoinsUsage; }
};

/*
class CCoinsViewCacheTest : public CCoinsViewCache
{
public:
    CCoinsViewCacheTest(CCoinsView* base) : CCoinsViewCache(base) {}

    void SelfTest() const
    {
        // Manually recompute the dynamic usage of the whole data, and compare it.
        size_t ret = memusage::DynamicUsage(cacheCoins);
        for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++) {
            ret += it->second.coins.DynamicMemoryUsage();
        }
        BOOST_CHECK_EQUAL(DynamicMemoryUsage(), ret);
    }

};
*/

}

BOOST_FIXTURE_TEST_SUITE(coins_tests, BasicTestingSetup)

static const unsigned int NUM_SIMULATION_ITERATIONS = 40000;

// This is a large randomized insert/remove simulation test on a variable-size
// stack of caches on top of CCoinsViewTest.
//
// It will randomly create/update/delete CCoins entries to a tip of caches, with
// txids picked from a limited list of random 256-bit hashes. Occasionally, a
// new tip is added to the stack of caches, or the tip is flushed and removed.
//
// During the process, booleans are kept to make sure that the randomized
// operation hits all branches.
BOOST_AUTO_TEST_CASE(coins_cache_simulation_test)
{
    // Various coverage trackers.
    bool removed_all_caches = false;
    bool reached_4_caches = false;
    bool added_an_entry = false;
    bool removed_an_entry = false;
    bool updated_an_entry = false;
    bool found_an_entry = false;
    bool missed_an_entry = false;

    // A simple map to track what we expect the cache stack to represent.
    std::map<uint256, CCoins> result;

    // The cache stack.
    CCoinsViewTest base; // A CCoinsViewTest at the bottom.
    std::vector<CCoinsViewCacheTest*> stack; // A stack of CCoinsViewCaches on top.
    stack.push_back(new CCoinsViewCacheTest(&base)); // Start with one cache.

    // Use a limited set of random transaction ids, so we do test overwriting entries.
    std::vector<uint256> txids;
    txids.resize(NUM_SIMULATION_ITERATIONS / 8);
    for (unsigned int i = 0; i < txids.size(); i++) {
        txids[i] = GetRandHash();
    }

    for (unsigned int i = 0; i < NUM_SIMULATION_ITERATIONS; i++) {
        // Do a random modification.
        {
            uint256 txid = txids[insecure_rand() % txids.size()]; // txid we're going to modify in this iteration.
            CCoins& coins = result[txid];
            const Coin& entry = stack.back()->AccessCoin(COutPoint(txid, 0));
            BOOST_CHECK((entry.IsPruned() && coins.IsPruned()) || entry == Coin(coins.vout[0], coins.nHeight, coins.fCoinBase));

            if (insecure_rand() % 5 == 0 || coins.IsPruned()) {
                if (coins.IsPruned()) {
                    added_an_entry = true;
                } else {
                    updated_an_entry = true;
                }
                coins.vout.resize(1);
                coins.vout[0].nValue = insecure_rand();
            } else {
                coins.Clear();
                removed_an_entry = true;
            }
            if (coins.IsPruned()) {
                stack.back()->SpendCoin(COutPoint(txid, 0));
            } else {
                stack.back()->AddCoin(COutPoint(txid, 0), Coin(coins.vout[0], coins.nHeight, coins.fCoinBase), true);
            }
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (insecure_rand() % 1000 == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (std::map<uint256, CCoins>::iterator it = result.begin(); it != result.end(); it++) {
                const CCoins* coins = stack.back()->AccessCoins(it->first);
                if (coins) {
                    BOOST_CHECK(*coins == it->second);
                    found_an_entry = true;
                } else {
                    BOOST_CHECK(it->second.IsPruned());
                    missed_an_entry = true;
                }
            }
            BOOST_FOREACH(const CCoinsViewCacheTest *test, stack) {
                test->SelfTest();
            }
        }

        if (insecure_rand() % 100 == 0) {
            // Every 100 iterations, flush an intermediate cache
            if (stack.size() > 1 && insecure_rand() % 2 == 0) {
                unsigned int flushIndex = insecure_rand() % (stack.size() - 1);
                stack[flushIndex]->Flush();
            }
        }
        if (insecure_rand() % 100 == 0) {
            // Every 100 iterations, change the cache stack.
            if (stack.size() > 0 && insecure_rand() % 2 == 0) {
                //Remove the top cache
                stack.back()->Flush();
                delete stack.back();
                stack.pop_back();
            }
            if (stack.size() == 0 || (stack.size() < 4 && insecure_rand() % 2)) {
                //Add a new cache
                CCoinsView* tip = &base;
                if (stack.size() > 0) {
                    tip = stack.back();
                } else {
                    removed_all_caches = true;
                }

                stack.push_back(new CCoinsViewCacheTest(tip));
                if (stack.size() == 4)
                    reached_4_caches = true;
            }
        }
    }

    // Clean up the stack.
    while (stack.size() > 0) {
        delete stack.back();
        stack.pop_back();
    }

    // Verify coverage.
    BOOST_CHECK(removed_all_caches);
    BOOST_CHECK(reached_4_caches);
    BOOST_CHECK(added_an_entry);
    BOOST_CHECK(removed_an_entry);
    BOOST_CHECK(updated_an_entry);
    BOOST_CHECK(found_an_entry);
    BOOST_CHECK(missed_an_entry);
}

// This test is similar to the previous test
// except the emphasis is on testing the functionality of UpdateCoins
// random txs are created and UpdateCoins is used to update the cache stack
// In particular it is tested that spending a duplicate coinbase tx
// has the expected effect (the other duplicate is overwitten at all cache levels)
BOOST_AUTO_TEST_CASE(updatecoins_simulation_test)
{
    bool spent_a_duplicate_coinbase = false;
    // A simple map to track what we expect the cache stack to represent.
    std::map<uint256, CCoins> result;

    // The cache stack.
    CCoinsViewTest base; // A CCoinsViewTest at the bottom.
    std::vector<CCoinsViewCacheTest*> stack; // A stack of CCoinsViewCaches on top.
    stack.push_back(new CCoinsViewCacheTest(&base)); // Start with one cache.

    // Track the txids we've used and whether they have been spent or not
    std::map<uint256, CAmount> coinbaseids;
    std::set<uint256> alltxids;
    std::set<uint256> duplicateids;

    for (unsigned int i = 0; i < NUM_SIMULATION_ITERATIONS; i++) {
        {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vout.resize(1);
            tx.vout[0].nValue = i; //Keep txs unique unless intended to duplicate
            unsigned int height = insecure_rand();

            // 1/10 times create a coinbase
            if (insecure_rand() % 10 == 0 || coinbaseids.size() < 10) {
                // 1/100 times create a duplicate coinbase
                if (insecure_rand() % 10 == 0 && coinbaseids.size()) {
                    std::map<uint256, CAmount>::iterator coinbaseIt = coinbaseids.lower_bound(GetRandHash());
                    if (coinbaseIt == coinbaseids.end()) {
                        coinbaseIt = coinbaseids.begin();
                    }
                    //Use same random value to have same hash and be a true duplicate
                    tx.vout[0].nValue = coinbaseIt->second;
                    assert(tx.GetHash() == coinbaseIt->first);
                    duplicateids.insert(coinbaseIt->first);
                }
                else {
                    coinbaseids[tx.GetHash()] = tx.vout[0].nValue;
                }
                assert(CTransaction(tx).IsCoinBase());
            }
            // 9/10 times create a regular tx
            else {
                uint256 prevouthash;
                // equally likely to spend coinbase or non coinbase
                std::set<uint256>::iterator txIt = alltxids.lower_bound(GetRandHash());
                if (txIt == alltxids.end()) {
                    txIt = alltxids.begin();
                }
                prevouthash = *txIt;

                // Construct the tx to spend the coins of prevouthash
                tx.vin[0].prevout.hash = prevouthash;
                tx.vin[0].prevout.n = 0;

                // Update the expected result of prevouthash to know these coins are spent
                CCoins& oldcoins = result[prevouthash];
                oldcoins.Clear();

                // It is of particular importance here that once we spend a coinbase tx hash
                // it is no longer available to be duplicated (or spent again)
                // BIP 34 in conjunction with enforcing BIP 30 (at least until BIP 34 was active)
                // results in the fact that no coinbases were duplicated after they were already spent
                alltxids.erase(prevouthash);
                coinbaseids.erase(prevouthash);

                // The test is designed to ensure spending a duplicate coinbase will work properly
                // if that ever happens and not resurrect the previously overwritten coinbase
                if (duplicateids.count(prevouthash)) {
                    spent_a_duplicate_coinbase = true;
                }

                assert(!CTransaction(tx).IsCoinBase());
            }


            // Track this tx and undo info to use later
            alltxs.insert(std::make_pair(tx.GetHash(),std::make_tuple(tx,undo,oldcoins)));
        } else if (utxoset.size()) {
            //1/20 times undo a previous transaction
            TxData &txd = FindRandomFrom(utxoset);

            CTransaction &tx = std::get<0>(txd);
            CTxUndo &undo = std::get<1>(txd);
            CCoins &origcoins = std::get<2>(txd);

            uint256 undohash = tx.GetHash();

            // Update the expected result
            // Remove new outputs
            result[undohash].Clear();
            // If not coinbase restore prevout
            if (!tx.IsCoinBase()) {
                result[tx.vin[0].prevout.hash] = origcoins;
            }

            // Disconnect the tx from the current UTXO
            // See code in DisconnectBlock
            // remove outputs
            {
                stack.back()->SpendCoin(COutPoint(undohash, 0));
            }
            // restore inputs
            if (!tx.IsCoinBase()) {
                const COutPoint &out = tx.vin[0].prevout;
                Coin coin = undo.vprevout[0];
                ApplyTxInUndo(std::move(coin), *(stack.back()), out);
            }
            // Store as a candidate for reconnection
            disconnectedids.insert(undohash);

            CValidationState dummy;
            UpdateCoins(tx, dummy, *(stack.back()), height);
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (insecure_rand() % 1000 == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (std::map<uint256, CCoins>::iterator it = result.begin(); it != result.end(); it++) {
                const CCoins* coins = stack.back()->AccessCoins(it->first);
                if (coins) {
                    BOOST_CHECK(*coins == it->second);
                } else {
                    BOOST_CHECK(it->second.IsPruned());
                }
            }
        }

        if (insecure_rand() % 100 == 0) {
            // Every 100 iterations, flush an intermediate cache
            if (stack.size() > 1 && insecure_rand() % 2 == 0) {
                unsigned int flushIndex = insecure_rand() % (stack.size() - 1);
                stack[flushIndex]->Flush();
            }
        }
        if (insecure_rand() % 100 == 0) {
            // Every 100 iterations, change the cache stack.
            if (stack.size() > 0 && insecure_rand() % 2 == 0) {
                stack.back()->Flush();
                delete stack.back();
                stack.pop_back();
            }
            if (stack.size() == 0 || (stack.size() < 4 && insecure_rand() % 2)) {
                CCoinsView* tip = &base;
                if (stack.size() > 0) {
                    tip = stack.back();
                }
                stack.push_back(new CCoinsViewCacheTest(tip));
           }
        }
    }

    // Clean up the stack.
    while (stack.size() > 0) {
        delete stack.back();
        stack.pop_back();
    }

    // Verify coverage.
    BOOST_CHECK(spent_a_duplicate_coinbase);
}



const static uint256 TXID;
const static COutPoint OUTPOINT = {uint256(), 0};
const static CAmount PRUNED = -1;
const static CAmount ABSENT = -2;
const static CAmount FAIL = -3;
const static CAmount VALUE1 = 100;
const static CAmount VALUE2 = 200;
const static CAmount VALUE3 = 300;
const static char DIRTY = CCoinsCacheEntry::DIRTY;
const static char FRESH = CCoinsCacheEntry::FRESH;
const static char NO_ENTRY = -1;

const static auto FLAGS = {char(0), FRESH, DIRTY, char(DIRTY | FRESH)};
const static auto CLEAN_FLAGS = {char(0), FRESH};
const static auto ABSENT_FLAGS = {NO_ENTRY};

void SetCoinsValue(CAmount value, CCoins& coins)
{
    assert(value != ABSENT);
    coins.Clear();
    assert(coins.IsPruned());
    if (value != PRUNED) {
        coins.vout.emplace_back();
        coins.vout.back().nValue = value;
        assert(!coins.IsPruned());
    }
}

size_t InsertCoinsMapEntry(CCoinsMap& map, CAmount value, char flags)
{
    if (value == ABSENT) {
        assert(flags == NO_ENTRY);
        return 0;
    }
    assert(flags != NO_ENTRY);
    CCoinsCacheEntry entry;
    entry.flags = flags;
    SetCoinsValue(value, entry.coins);
    auto inserted = map.emplace(TXID, std::move(entry));
    assert(inserted.second);
    return inserted.first->second.coins.DynamicMemoryUsage();
}

void GetCoinsMapEntry(const CCoinsMap& map, CAmount& value, char& flags)
{
    auto it = map.find(TXID);
    if (it == map.end()) {
        value = ABSENT;
        flags = NO_ENTRY;
    } else {
        if (it->second.coins.IsPruned()) {
            assert(it->second.coins.vout.size() == 0);
            value = PRUNED;
        } else {
            assert(it->second.coins.vout.size() == 1);
            value = it->second.coins.vout[0].nValue;
        }
        flags = it->second.flags;
        assert(flags != NO_ENTRY);
    }
}

void WriteCoinsViewEntry(CCoinsView& view, CAmount value, char flags)
{
    CCoinsMap map;
    InsertCoinsMapEntry(map, value, flags);
    uint256 hash;
    hash.SetNull();
//    view.BatchWrite(map, {});
    uint64_t cacheusage = 0;
    view.BatchWrite(map, hash, cacheusage);
}

class SingleEntryCacheTest
{
public:
    SingleEntryCacheTest(CAmount base_value, CAmount cache_value, char cache_flags)
    {
        WriteCoinsViewEntry(base, base_value, base_value == ABSENT ? NO_ENTRY : DIRTY);
        cache.usage() += InsertCoinsMapEntry(cache.map(), cache_value, cache_flags);
    }

    CCoinsView root;
    CCoinsViewCacheTest base{&root};
    CCoinsViewCacheTest cache{&base};
};

void CheckAccessCoin(CAmount base_value, CAmount cache_value, CAmount expected_value, char cache_flags, char expected_flags)
{
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);
    test.cache.AccessCoin(OUTPOINT);
    test.cache.SelfTest();

    CAmount result_value;
    char result_flags;
    GetCoinsMapEntry(test.cache.map(), result_value, result_flags);
    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

BOOST_AUTO_TEST_CASE(ccoins_access)
{
    /* Check AccessCoin behavior, requesting a coin from a cache view layered on
     * top of a base view, and checking the resulting entry in the cache after
     * the access.
     *
     *               Base    Cache   Result  Cache        Result
     *               Value   Value   Value   Flags        Flags
     */
    CheckAccessCoin(ABSENT, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   );
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, 0          , 0          );
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, FRESH      , FRESH      );
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, 0          , 0          );
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, FRESH      , FRESH      );
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, DIRTY      , DIRTY      );
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCoin(PRUNED, ABSENT, PRUNED, NO_ENTRY   , FRESH      );
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, 0          , 0          );
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, FRESH      , FRESH      );
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, 0          , 0          );
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, FRESH      , FRESH      );
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, DIRTY      , DIRTY      );
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCoin(VALUE1, ABSENT, VALUE1, NO_ENTRY   , 0          );
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, 0          , 0          );
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, FRESH      , FRESH      );
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, 0          , 0          );
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, FRESH      , FRESH      );
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, DIRTY      , DIRTY      );
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH);
}

void CheckSpendCoins(CAmount base_value, CAmount cache_value, CAmount expected_value, char cache_flags, char expected_flags)
{
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);
    test.cache.SpendCoin(OUTPOINT);
    test.cache.SelfTest();

    CAmount result_value;
    char result_flags;
    GetCoinsMapEntry(test.cache.map(), result_value, result_flags);
    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
};

BOOST_AUTO_TEST_CASE(ccoins_spend)
{
    /* Check SpendCoin behavior, requesting a coin from a cache view layered on
     * top of a base view, spending, and then checking
     * the resulting entry in the cache after the modification.
     *
     *              Base    Cache   Result  Cache        Result
     *              Value   Value   Value   Flags        Flags
     */
    CheckSpendCoins(ABSENT, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   );
    CheckSpendCoins(ABSENT, PRUNED, PRUNED, 0          , DIRTY      );
    CheckSpendCoins(ABSENT, PRUNED, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCoins(ABSENT, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCoins(ABSENT, PRUNED, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCoins(ABSENT, VALUE2, PRUNED, 0          , DIRTY      );
    CheckSpendCoins(ABSENT, VALUE2, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCoins(ABSENT, VALUE2, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCoins(ABSENT, VALUE2, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCoins(PRUNED, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   );
    CheckSpendCoins(PRUNED, PRUNED, PRUNED, 0          , DIRTY      );
    CheckSpendCoins(PRUNED, PRUNED, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCoins(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCoins(PRUNED, PRUNED, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCoins(PRUNED, VALUE2, PRUNED, 0          , DIRTY      );
    CheckSpendCoins(PRUNED, VALUE2, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCoins(PRUNED, VALUE2, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCoins(PRUNED, VALUE2, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCoins(VALUE1, ABSENT, PRUNED, NO_ENTRY   , DIRTY      );
    CheckSpendCoins(VALUE1, PRUNED, PRUNED, 0          , DIRTY      );
    CheckSpendCoins(VALUE1, PRUNED, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCoins(VALUE1, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCoins(VALUE1, PRUNED, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCoins(VALUE1, VALUE2, PRUNED, 0          , DIRTY      );
    CheckSpendCoins(VALUE1, VALUE2, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCoins(VALUE1, VALUE2, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCoins(VALUE1, VALUE2, ABSENT, DIRTY|FRESH, NO_ENTRY   );
}

void CheckAddCoinBase(CAmount base_value, CAmount cache_value, CAmount modify_value, CAmount expected_value, char cache_flags, char expected_flags, bool coinbase)
{
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);

    CAmount result_value;
    char result_flags;
    try {
        CTxOut output;
        output.nValue = modify_value;
        test.cache.AddCoin(OUTPOINT, Coin(std::move(output), 1, coinbase), coinbase);
        test.cache.SelfTest();
        GetCoinsMapEntry(test.cache.map(), result_value, result_flags);
    } catch (std::logic_error& e) {
        result_value = FAIL;
        result_flags = NO_ENTRY;
    }

    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

// Simple wrapper for CheckAddCoinBase function above that loops through
// different possible base_values, making sure each one gives the same results.
// This wrapper lets the coins_add test below be shorter and less repetitive,
// while still verifying that the CoinsViewCache::AddCoin implementation
// ignores base values.
template <typename... Args>
void CheckAddCoin(Args&&... args)
{
    for (CAmount base_value : {ABSENT, PRUNED, VALUE1})
        CheckAddCoinBase(base_value, std::forward<Args>(args)...);
}

BOOST_AUTO_TEST_CASE(ccoins_add)
{
    /* Check AddCoin behavior, requesting a new coin from a cache view,
     * writing a modification to the coin, and then checking the resulting
     * entry in the cache after the modification. Verify behavior with the
     * with the AddCoin potential_overwrite argument set to false, and to true.
     *
     *           Cache   Write   Result  Cache        Result       potential_overwrite
     *           Value   Value   Value   Flags        Flags
     */
    CheckAddCoin(ABSENT, VALUE3, VALUE3, NO_ENTRY   , DIRTY|FRESH, false);
    CheckAddCoin(ABSENT, VALUE3, VALUE3, NO_ENTRY   , DIRTY      , true );
    CheckAddCoin(PRUNED, VALUE3, VALUE3, 0          , DIRTY|FRESH, false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, 0          , DIRTY      , true );
    CheckAddCoin(PRUNED, VALUE3, VALUE3, FRESH      , DIRTY|FRESH, false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, FRESH      , DIRTY|FRESH, true );
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY      , DIRTY      , false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY      , DIRTY      , true );
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY|FRESH, DIRTY|FRESH, false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY|FRESH, DIRTY|FRESH, true );
    CheckAddCoin(VALUE2, VALUE3, FAIL  , 0          , NO_ENTRY   , false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, 0          , DIRTY      , true );
    CheckAddCoin(VALUE2, VALUE3, FAIL  , FRESH      , NO_ENTRY   , false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, FRESH      , DIRTY|FRESH, true );
    CheckAddCoin(VALUE2, VALUE3, FAIL  , DIRTY      , NO_ENTRY   , false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, DIRTY      , DIRTY      , true );
    CheckAddCoin(VALUE2, VALUE3, FAIL  , DIRTY|FRESH, NO_ENTRY   , false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, DIRTY|FRESH, DIRTY|FRESH, true );
}

void CheckWriteCoins(CAmount parent_value, CAmount child_value, CAmount expected_value, char parent_flags, char child_flags, char expected_flags)
{
    SingleEntryCacheTest test(ABSENT, parent_value, parent_flags);

    CAmount result_value;
    char result_flags;
    try {
        WriteCoinsViewEntry(test.cache, child_value, child_flags);
        test.cache.SelfTest();
        GetCoinsMapEntry(test.cache.map(), result_value, result_flags);
    } catch (std::logic_error& e) {
        result_value = FAIL;
        result_flags = NO_ENTRY;
    }

    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

BOOST_AUTO_TEST_CASE(ccoins_write)
{
    /* Check BatchWrite behavior, flushing one entry from a child cache to a
     * parent cache, and checking the resulting entry in the parent cache
     * after the write.
     *
     *              Parent  Child   Result  Parent       Child        Result
     *              Value   Value   Value   Flags        Flags        Flags
     */
    CheckWriteCoins(ABSENT, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   , NO_ENTRY   );
    CheckWriteCoins(ABSENT, PRUNED, PRUNED, NO_ENTRY   , DIRTY      , DIRTY      );
    CheckWriteCoins(ABSENT, PRUNED, ABSENT, NO_ENTRY   , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(ABSENT, VALUE2, VALUE2, NO_ENTRY   , DIRTY      , DIRTY      );
    CheckWriteCoins(ABSENT, VALUE2, VALUE2, NO_ENTRY   , DIRTY|FRESH, DIRTY|FRESH);
    CheckWriteCoins(PRUNED, ABSENT, PRUNED, 0          , NO_ENTRY   , 0          );
    CheckWriteCoins(PRUNED, ABSENT, PRUNED, FRESH      , NO_ENTRY   , FRESH      );
    CheckWriteCoins(PRUNED, ABSENT, PRUNED, DIRTY      , NO_ENTRY   , DIRTY      );
    CheckWriteCoins(PRUNED, ABSENT, PRUNED, DIRTY|FRESH, NO_ENTRY   , DIRTY|FRESH);
    CheckWriteCoins(PRUNED, PRUNED, PRUNED, 0          , DIRTY      , DIRTY      );
    CheckWriteCoins(PRUNED, PRUNED, PRUNED, 0          , DIRTY|FRESH, DIRTY      );
    CheckWriteCoins(PRUNED, PRUNED, ABSENT, FRESH      , DIRTY      , NO_ENTRY   );
    CheckWriteCoins(PRUNED, PRUNED, ABSENT, FRESH      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCoins(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY|FRESH, DIRTY      );
    CheckWriteCoins(PRUNED, PRUNED, ABSENT, DIRTY|FRESH, DIRTY      , NO_ENTRY   );
    CheckWriteCoins(PRUNED, PRUNED, ABSENT, DIRTY|FRESH, DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, 0          , DIRTY      , DIRTY      );
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, 0          , DIRTY|FRESH, DIRTY      );
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, FRESH      , DIRTY      , DIRTY|FRESH);
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, FRESH      , DIRTY|FRESH, DIRTY|FRESH);
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, DIRTY      , DIRTY|FRESH, DIRTY      );
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, DIRTY|FRESH, DIRTY      , DIRTY|FRESH);
    CheckWriteCoins(PRUNED, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH, DIRTY|FRESH);
    CheckWriteCoins(VALUE1, ABSENT, VALUE1, 0          , NO_ENTRY   , 0          );
    CheckWriteCoins(VALUE1, ABSENT, VALUE1, FRESH      , NO_ENTRY   , FRESH      );
    CheckWriteCoins(VALUE1, ABSENT, VALUE1, DIRTY      , NO_ENTRY   , DIRTY      );
    CheckWriteCoins(VALUE1, ABSENT, VALUE1, DIRTY|FRESH, NO_ENTRY   , DIRTY|FRESH);
    CheckWriteCoins(VALUE1, PRUNED, PRUNED, 0          , DIRTY      , DIRTY      );
    CheckWriteCoins(VALUE1, PRUNED, FAIL  , 0          , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(VALUE1, PRUNED, ABSENT, FRESH      , DIRTY      , NO_ENTRY   );
    CheckWriteCoins(VALUE1, PRUNED, FAIL  , FRESH      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(VALUE1, PRUNED, PRUNED, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCoins(VALUE1, PRUNED, FAIL  , DIRTY      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(VALUE1, PRUNED, ABSENT, DIRTY|FRESH, DIRTY      , NO_ENTRY   );
    CheckWriteCoins(VALUE1, PRUNED, FAIL  , DIRTY|FRESH, DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(VALUE1, VALUE2, VALUE2, 0          , DIRTY      , DIRTY      );
    CheckWriteCoins(VALUE1, VALUE2, FAIL  , 0          , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(VALUE1, VALUE2, VALUE2, FRESH      , DIRTY      , DIRTY|FRESH);
    CheckWriteCoins(VALUE1, VALUE2, FAIL  , FRESH      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(VALUE1, VALUE2, VALUE2, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCoins(VALUE1, VALUE2, FAIL  , DIRTY      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCoins(VALUE1, VALUE2, VALUE2, DIRTY|FRESH, DIRTY      , DIRTY|FRESH);
    CheckWriteCoins(VALUE1, VALUE2, FAIL  , DIRTY|FRESH, DIRTY|FRESH, NO_ENTRY   );

    // The checks above omit cases where the child flags are not DIRTY, since
    // they would be too repetitive (the parent cache is never updated in these
    // cases). The loop below covers these cases and makes sure the parent cache
    // is always left unchanged.
    for (CAmount parent_value : {ABSENT, PRUNED, VALUE1})
        for (CAmount child_value : {ABSENT, PRUNED, VALUE2})
            for (char parent_flags : parent_value == ABSENT ? ABSENT_FLAGS : FLAGS)
                for (char child_flags : child_value == ABSENT ? ABSENT_FLAGS : CLEAN_FLAGS)
                    CheckWriteCoins(parent_value, child_value, parent_value, parent_flags, child_flags, parent_flags);
}


BOOST_AUTO_TEST_SUITE_END()
