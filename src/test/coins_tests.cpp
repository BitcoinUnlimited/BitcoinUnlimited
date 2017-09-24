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
void UpdateCoins(const CTransaction& tx, CValidationState &state, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight);

namespace
{
//! equality test
bool operator==(const Coin &a, const Coin &b) {
    // Empty Coin objects are always equal.
    if (a.IsSpent() && b.IsSpent()) return true;
    return a.fCoinBase == b.fCoinBase &&
           a.nHeight == b.nHeight &&
           a.out == b.out;
}

class CCoinsViewTest : public CCoinsView
{
    uint256 hashBestBlock_;
    std::map<COutPoint, Coin> map_;

public:
    bool GetCoin(const COutPoint& outpoint, Coin& coin) const
    {
        std::map<COutPoint, Coin>::const_iterator it = map_.find(outpoint);
        if (it == map_.end()) {
            return false;
        }
        coin = it->second;
        if (coin.IsSpent() && insecure_rand() % 2 == 0) {
            // Randomly return false in case of an empty entry.
            return false;
        }
        return true;
    }

    bool HaveCoin(const COutPoint& outpoint) const
    {
        Coin coin;
        return GetCoin(outpoint, coin);
    }

    uint256 GetBestBlock() const { return hashBestBlock_; }

    bool BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock, size_t &nChildCachedCoinsUsage)
    {
        for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end(); ) {
            if (it->second.flags & CCoinsCacheEntry::DIRTY) {
                // Same optimization used in CCoinsViewDB is to only write dirty entries.
                map_[it->first] = it->second.coin;
                if (it->second.coin.IsSpent() && insecure_rand() % 3 == 0) {
                    // Randomly delete empty entries on write.
                    map_.erase(it->first);
                }
                nChildCachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
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
    explicit CCoinsViewCacheTest(CCoinsView* base) : CCoinsViewCache(base) {}

    void SelfTest() const
    {
        // Manually recompute the dynamic usage of the whole data, and compare it.
        size_t ret = memusage::DynamicUsage(cacheCoins);
        size_t count = 0;
        for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++) {
            ret += it->second.coin.DynamicMemoryUsage();
            ++count;
        }
        BOOST_CHECK_EQUAL(GetCacheSize(), count);
        BOOST_CHECK_EQUAL(DynamicMemoryUsage(), ret);
    }

    CCoinsMap& map() const { return cacheCoins; }
    size_t& usage() const { return cachedCoinsUsage; }
};

}

BOOST_FIXTURE_TEST_SUITE(coins_tests, BasicTestingSetup)

static const unsigned int NUM_SIMULATION_ITERATIONS = 40000;

// This is a large randomized insert/remove simulation test on a variable-size
// stack of caches on top of CCoinsViewTest.
//
// It will randomly create/update/delete Coin entries to a tip of caches, with
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
    bool added_an_unspendable_entry = false;
    bool removed_an_entry = false;
    bool updated_an_entry = false;
    bool found_an_entry = false;
    bool missed_an_entry = false;
    bool uncached_an_entry = false;

    // A simple map to track what we expect the cache stack to represent.
    std::map<COutPoint, Coin> result;

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
            Coin& coin = result[COutPoint(txid, 0)];
            const Coin& entry = (insecure_rand() % 500 == 0) ? AccessByTxid(*stack.back(), txid) : stack.back()->AccessCoin(COutPoint(txid, 0));
            BOOST_CHECK(coin == entry);

            if (insecure_rand() % 5 == 0 || coin.IsSpent()) {
                Coin newcoin;
                newcoin.out.nValue = insecure_rand();
                newcoin.nHeight = 1;
                if (insecure_rand() % 16 == 0 && coin.IsSpent()) {
                    newcoin.out.scriptPubKey.assign(1 + (insecure_rand() & 0x3F), OP_RETURN);
                    BOOST_CHECK(newcoin.out.scriptPubKey.IsUnspendable());
                    added_an_unspendable_entry = true;
                } else {
                    newcoin.out.scriptPubKey.assign(insecure_rand() & 0x3F, 0); // Random sizes so we can test memory usage accounting
                    (coin.IsSpent() ? added_an_entry : updated_an_entry) = true;
                    coin = newcoin;
                }
                stack.back()->AddCoin(COutPoint(txid, 0), std::move(newcoin), !coin.IsSpent() || insecure_rand() & 1);
            } else {
                removed_an_entry = true;
                coin.Clear();
                stack.back()->SpendCoin(COutPoint(txid, 0));
            }
        }

        // One every 10 iterations, remove a random entry from the cache
        if (insecure_rand() % 10) {
            COutPoint out(txids[insecure_rand() % txids.size()], 0);
            int cacheid = insecure_rand() % stack.size();
            stack[cacheid]->Uncache(out);
            uncached_an_entry |= !stack[cacheid]->HaveCoinInCache(out);
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (insecure_rand() % 1000 == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (auto it = result.begin(); it != result.end(); it++) {
                bool have = stack.back()->HaveCoin(it->first);
                const Coin& coin = stack.back()->AccessCoin(it->first);
                BOOST_CHECK(have == !coin.IsSpent());
                BOOST_CHECK(coin == it->second);
                if (coin.IsSpent()) {
                    missed_an_entry = true;
                } else {
                    BOOST_CHECK(stack.back()->HaveCoinInCache(it->first));
                    found_an_entry = true;
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
    BOOST_CHECK(added_an_unspendable_entry);
    BOOST_CHECK(removed_an_entry);
    BOOST_CHECK(updated_an_entry);
    BOOST_CHECK(found_an_entry);
    BOOST_CHECK(missed_an_entry);
    BOOST_CHECK(uncached_an_entry);
}

// Store of all necessary tx and undo data for next test
typedef std::map<COutPoint, std::tuple<CTransaction,CTxUndo,Coin>> UtxoData;
UtxoData utxoData;

UtxoData::iterator FindRandomFrom(const std::set<COutPoint> &utxoSet) {
    assert(utxoSet.size());
    auto utxoSetIt = utxoSet.lower_bound(COutPoint(GetRandHash(), 0));
    if (utxoSetIt == utxoSet.end()) {
        utxoSetIt = utxoSet.begin();
    }
    auto utxoDataIt = utxoData.find(*utxoSetIt);
    assert(utxoDataIt != utxoData.end());
    return utxoDataIt;
}

// This test is similar to the previous test
// except the emphasis is on testing the functionality of UpdateCoins
// random txs are created and UpdateCoins is used to update the cache stack
// In particular it is tested that spending a duplicate coinbase tx
// has the expected effect (the other duplicate is overwitten at all cache levels)


BOOST_AUTO_TEST_CASE(ccoins_serialization)
{
    // Good example
    CDataStream ss1(ParseHex("97f23c835800816115944e077fe7c803cfa57f29b36bf87c1d35"), SER_DISK, CLIENT_VERSION);
    Coin cc1;
    ss1 >> cc1;

    CKeyID keyid1 = CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35")));
    CScript script1;
    script1 << OP_DUP << OP_HASH160 << ToByteVector(keyid1) << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK_EQUAL(cc1.fCoinBase, false);
    BOOST_CHECK_EQUAL(cc1.nHeight, 203998);
    BOOST_CHECK_EQUAL(cc1.out.nValue, 60000000000ULL);
    BOOST_CHECK_EQUAL(HexStr(cc1.out.scriptPubKey), HexStr(script1));

    // Good example
    CDataStream ss2(ParseHex("8ddf77bbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa4"), SER_DISK, CLIENT_VERSION);
    Coin cc2;
    ss2 >> cc2;

    CKeyID keyid2 = CKeyID(uint160(ParseHex("8c988f1a4a4de2161e0f50aac7f17e7f9555caa4")));
    CScript script2;
    script2 << OP_DUP << OP_HASH160 << ToByteVector(keyid2) << OP_EQUALVERIFY << OP_CHECKSIG;

    BOOST_CHECK_EQUAL(cc2.fCoinBase, true);
    BOOST_CHECK_EQUAL(cc2.nHeight, 120891);
    BOOST_CHECK_EQUAL(cc2.out.nValue, 110397);
    BOOST_CHECK_EQUAL(HexStr(cc2.out.scriptPubKey), HexStr(script2));

    // Smallest possible example
    CDataStream ss3(ParseHex("000006"), SER_DISK, CLIENT_VERSION);
    Coin cc3;
    ss3 >> cc3;
    BOOST_CHECK_EQUAL(cc3.fCoinBase, false);
    BOOST_CHECK_EQUAL(cc3.nHeight, 0);
    BOOST_CHECK_EQUAL(cc3.out.nValue, 0);
    BOOST_CHECK_EQUAL(cc3.out.scriptPubKey.size(), 0);

    // scriptPubKey that ends beyond the end of the stream
    CDataStream ss4(ParseHex("000007"), SER_DISK, CLIENT_VERSION);
    try {
        Coin cc4;
        ss4 >> cc4;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure& e) {
    }

    // Very large scriptPubKey (3*10^9 bytes) past the end of the stream
    CDataStream tmp(SER_DISK, CLIENT_VERSION);
    uint64_t x = 3000000000ULL;
    tmp << VARINT(x);
    BOOST_CHECK_EQUAL(HexStr(tmp.begin(), tmp.end()), "8a95c0bb00");

    CDataStream ss5(ParseHex("0008a95c0bb00"), SER_DISK, CLIENT_VERSION);
    try {
        Coin cc5;
        ss5 >> cc5;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure& e) {
    }
}

const static COutPoint OUTPOINT;
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

void SetCoinsValue(CAmount value, Coin& coin)
{
    assert(value != ABSENT);
    coin.Clear();
    assert(coin.IsSpent());
    if (value != PRUNED) {
        coin.out.nValue = value;
        coin.nHeight = 1;
        assert(!coin.IsSpent());
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
    SetCoinsValue(value, entry.coin);
    auto inserted = map.emplace(OUTPOINT, std::move(entry));
    assert(inserted.second);
    return inserted.first->second.coin.DynamicMemoryUsage();
}

void GetCoinsMapEntry(const CCoinsMap& map, CAmount& value, char& flags)
{
    auto it = map.find(OUTPOINT);
    if (it == map.end()) {
        value = ABSENT;
        flags = NO_ENTRY;
    } else {
        if (it->second.coin.IsSpent()) {
            value = PRUNED;
        } else {
            value = it->second.coin.out.nValue;
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
    size_t cacheusage = 0;
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
