// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "consensus/consensus.h"
#include "memusage.h"
#include "random.h"
#include "util.h"

#include <assert.h>

bool CCoinsView::GetCoin(const COutPoint &outpoint, Coin &coin) const { return false; }
bool CCoinsView::HaveCoin(const COutPoint &outpoint) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins,
    const uint256 &hashBlock,
    const uint64_t nBestCoinHeight,
    size_t &nChildCachedCoinsUsage)
{
    return false;
}
CCoinsViewCursor *CCoinsView::Cursor() const { return nullptr; }
CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) {}
bool CCoinsViewBacked::GetCoin(const COutPoint &outpoint, Coin &coin) const { return base->GetCoin(outpoint, coin); }
bool CCoinsViewBacked::HaveCoin(const COutPoint &outpoint) const { return base->HaveCoin(outpoint); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins,
    const uint256 &hashBlock,
    const uint64_t nBestCoinHeight,
    size_t &nChildCachedCoinsUsage)
{
    return base->BatchWrite(mapCoins, hashBlock, nBestCoinHeight, nChildCachedCoinsUsage);
}
CCoinsViewCursor *CCoinsViewBacked::Cursor() const { return base->Cursor(); }
size_t CCoinsViewBacked::EstimateSize() const { return base->EstimateSize(); }
SaltedOutpointHasher::SaltedOutpointHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max()))
{
}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), nBestCoinHeight(0), cachedCoinsUsage(0)
{
}

size_t CCoinsViewCache::DynamicMemoryUsage() const
{
    LOCK(cs_utxo);
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

size_t CCoinsViewCache::ResetCachedCoinUsage() const
{
    LOCK(cs_utxo);
    size_t newCachedCoinsUsage = 0;
    for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++)
        newCachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
    if (cachedCoinsUsage != newCachedCoinsUsage)
    {
        error(
            "Resetting: cachedCoinsUsage has drifted - before %lld after %lld", cachedCoinsUsage, newCachedCoinsUsage);
        cachedCoinsUsage = newCachedCoinsUsage;
    }
    return newCachedCoinsUsage;
}

CCoinsMap::iterator CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const
{
    AssertLockHeld(cs_utxo);
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end())
        return it;
    Coin tmp;
    if (!base->GetCoin(outpoint, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret =
        cacheCoins
            .emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::forward_as_tuple(std::move(tmp)))
            .first;
    if (ret->second.coin.IsSpent())
    {
        // The parent only has an empty entry for this outpoint; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();

    if (nBestCoinHeight < ret->second.coin.nHeight)
        nBestCoinHeight = ret->second.coin.nHeight;

    return ret;
}

bool CCoinsViewCache::GetCoin(const COutPoint &outpoint, Coin &coin) const
{
    LOCK(cs_utxo);
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it != cacheCoins.end())
    {
        coin = it->second.coin;
        return true;
    }
    return false;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin &&coin, bool possible_overwrite)
{
    LOCK(cs_utxo);
    assert(!coin.IsSpent());
    if (coin.out.scriptPubKey.IsUnspendable())
        return;
    CCoinsMap::iterator it;
    bool inserted;
    std::tie(it, inserted) =
        cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!inserted)
    {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    }
    if (!possible_overwrite)
    {
        if (!it->second.coin.IsSpent())
        {
            throw std::logic_error("Adding new coin that replaces non-pruned entry");
        }
        fresh = !(it->second.flags & CCoinsCacheEntry::DIRTY);
    }
    it->second.coin = std::move(coin);
    it->second.flags |= CCoinsCacheEntry::DIRTY | (fresh ? CCoinsCacheEntry::FRESH : 0);
    cachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
    if (nBestCoinHeight < it->second.coin.nHeight)
        nBestCoinHeight = it->second.coin.nHeight;
}

void AddCoins(CCoinsViewCache &cache, const CTransaction &tx, int nHeight)
{
    bool fCoinbase = tx.IsCoinBase();
    const uint256 &txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i)
    {
        // Pass fCoinbase as the possible_overwrite flag to AddCoin, in order to correctly
        // deal with the pre-BIP30 occurrances of duplicate coinbase transactions.
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase), fCoinbase);
    }
}

void CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin *moveout)
{
    LOCK(cs_utxo);
    CCoinsMap::iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end())
        return;
    cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    if (moveout)
    {
        *moveout = std::move(it->second.coin);
    }
    if (it->second.flags & CCoinsCacheEntry::FRESH)
    {
        cacheCoins.erase(it);
    }
    else
    {
        it->second.flags |= CCoinsCacheEntry::DIRTY;
        it->second.coin.Clear();
    }
}

static const Coin coinEmpty;

const Coin &CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const
{
    LOCK(cs_utxo);
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end())
    {
        return coinEmpty;
    }
    else
    {
        return it->second.coin;
    }
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const
{
    LOCK(cs_utxo);
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const
{
    LOCK(cs_utxo);
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return it != cacheCoins.end();
}

uint256 CCoinsViewCache::GetBestBlock() const
{
    LOCK(cs_utxo);
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn)
{
    LOCK(cs_utxo);
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins,
    const uint256 &hashBlockIn,
    const uint64_t nBestCoinHeightIn,
    size_t &nChildCachedCoinsUsage)
{
    LOCK(cs_utxo);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();)
    {
        if (it->second.flags & CCoinsCacheEntry::DIRTY)
        { // Ignore non-dirty entries (optimization).
            // Update usage of the child cache before we do any swapping and deleting
            nChildCachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();

            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end())
            {
                // The parent cache does not have an entry, while the child does
                // We can ignore it if it's both FRESH and pruned in the child
                if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent()))
                {
                    // Otherwise we will need to create it in the parent
                    // and move the data up and mark it as dirty
                    CCoinsCacheEntry &entry = cacheCoins[it->first];
                    entry.coin = std::move(it->second.coin);
                    cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY;
                    // We can mark it FRESH in the parent if it was FRESH in the child
                    // Otherwise it might have just been flushed from the parent's cache
                    // and already exist in the grandparent
                    if (it->second.flags & CCoinsCacheEntry::FRESH)
                        entry.flags |= CCoinsCacheEntry::FRESH;
                }
            }
            else
            {
                // Assert that the child cache entry was not marked FRESH if the
                // parent cache entry has unspent outputs. If this ever happens,
                // it means the FRESH flag was misapplied and there is a logic
                // error in the calling code.
                if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent())
                    throw std::logic_error(
                        "FRESH flag misapplied to cache entry for base transaction with spendable outputs");

                // Found the entry in the parent cache
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coin.IsSpent())
                {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                }
                else
                {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                    itUs->second.coin = std::move(it->second.coin);
                    cachedCoinsUsage += itUs->second.coin.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                }
            }

            CCoinsMap::iterator itOld = it++;
            mapCoins.erase(itOld);
        }
        else
            it++;
    }
    hashBlock = hashBlockIn;
    if (nBestCoinHeightIn > nBestCoinHeight)
        nBestCoinHeight = nBestCoinHeightIn;

    return true;
}

bool CCoinsViewCache::Flush()
{
    LOCK(cs_utxo);
    bool fOk = base->BatchWrite(cacheCoins, hashBlock, nBestCoinHeight, cachedCoinsUsage);
    return fOk;
}

void CCoinsViewCache::Trim(size_t nTrimSize) const
{
    LOCK(cs_utxo);

    uint64_t nTrimmed = 0;
    uint64_t nTrimmedByHeight = 0;
    static uint64_t nTrimHeightDelta = nBestCoinHeight * 0.80; // This is where we attempt to do our first trim
    uint64_t nTrimHeight = nBestCoinHeight - nTrimHeightDelta;

    // if we've already walked the nTrimHeight all the way back as far as we can go and there is nothing to trim
    // then no need to check further.  This should be the typical state after a block sync is completed and there is
    // enough dbcache to hold all the coins from recent transactions in memory.
    if (nTrimHeight == 0 && DynamicMemoryUsage() <= nTrimSize)
        return;

    // Begin first Trim loop. This loop will trim coins from cache by the coin height, removing the oldest coins first.
    // This has been proven to improve sync performance significantly for nodes that can not hold the entire dbcache
    // in memory.
    bool fDone = false;
    uint64_t nSmallestDelta = 50; // number of blocks to adjust trim height by
    CCoinsMap::iterator iter = cacheCoins.begin();
    while (!fDone && DynamicMemoryUsage() > nTrimSize)
    {
        LOG(COINDB, "cacheCoinsUsage at start: %d total dynamic usage: %d trim to size: %d nBestCoinHeight: %d "
                    "trim height:%d\n",
            cachedCoinsUsage, DynamicMemoryUsage(), nTrimSize, nBestCoinHeight, nTrimHeight);

        iter = cacheCoins.begin();
        while (DynamicMemoryUsage() > nTrimSize)
        {
            if (iter == cacheCoins.end())
            {
                fDone = true;
                break;
            }

            if (iter->second.flags == 0 && iter->second.coin.nHeight < nTrimHeight)
            {
                cachedCoinsUsage -= iter->second.coin.DynamicMemoryUsage();

                iter = cacheCoins.erase(iter);
                nTrimmed++;
                nTrimmedByHeight++;
            }
            else
                iter++;
        }

        // Gradually increase the nTrimHeight if we didn't trim enought entries.
        if (fDone && DynamicMemoryUsage() > nTrimSize && nTrimHeightDelta > nSmallestDelta)
        {
            if (nTrimHeightDelta <= nSmallestDelta * 100)
                nTrimHeightDelta =
                    (nTrimHeightDelta > (nSmallestDelta * 2) ? nTrimHeightDelta - (nSmallestDelta * 2) : 0);
            else if (nTrimHeightDelta <= nSmallestDelta * 400)
                nTrimHeightDelta =
                    (nTrimHeightDelta > (nSmallestDelta * 10) ? nTrimHeightDelta - (nSmallestDelta * 10) : 0);
            else
                nTrimHeightDelta =
                    (nTrimHeightDelta > (nSmallestDelta * 200) ? nTrimHeightDelta - (nSmallestDelta * 200) : 0);

            nTrimHeight = (nBestCoinHeight > nTrimHeightDelta ? nBestCoinHeight - nTrimHeightDelta : 0);

            // We're not done yet. We've adjusted the nTrimHeight so we have to go back and trim again.
            fDone = false;

            LOG(COINDB, "Re-adjusting trim height to %d using a trim height delta of %d\n", nTrimHeight,
                nTrimHeightDelta);
        }
    }

    // If trimming by coin height failed to find any or enough coins to trim then trim the cache by ignoring
    // coin height. While this is not ideal we still have to trim to keep the cache from growing unbounded.
    iter = cacheCoins.begin();
    while (DynamicMemoryUsage() > nTrimSize)
    {
        if (iter == cacheCoins.end())
            break;

        // Only erase entries that have not been modified
        if (iter->second.flags == 0)
        {
            cachedCoinsUsage -= iter->second.coin.DynamicMemoryUsage();

            iter = cacheCoins.erase(iter);
            nTrimmed++;
        }
        else
            iter++;
    }
    if (nTrimmed > 0)
    {
        LOG(COINDB, "Trimmed %d by coin height\n", nTrimmedByHeight);
        LOG(COINDB, "Trimmed %ld from the CoinsViewCache, current size after trim: %ld and usage %ld bytes\n", nTrimmed,
            cacheCoins.size(), cachedCoinsUsage);
    }

    // If we're not trimming anything then gradually walk the trim height backwards from the tip.  This is to adjust
    // and account for the possiblity that the average block size could be getting smaller for certain periods of time
    // and thus we can keep more of the recent coins from getting trimmed.
    if (nTrimmedByHeight == 0 && nTrimmed == 0)
    {
        nTrimHeightDelta += nSmallestDelta;
        if (nTrimHeightDelta > nBestCoinHeight)
            nTrimHeightDelta = nBestCoinHeight;
        nTrimHeight = nBestCoinHeight - nTrimHeightDelta;
        LOG(COINDB, "Re-adjusting trim height to %d using a trim height delta of %d\n", nTrimHeight, nTrimHeightDelta);
    }
}

void CCoinsViewCache::Uncache(const COutPoint &hash)
{
    LOCK(cs_utxo);
    CCoinsMap::iterator it = cacheCoins.find(hash);

    // only uncache coins that are not dirty.
    if (it != cacheCoins.end() && it->second.flags == 0)
    {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
        cacheCoins.erase(it);
    }
}

void CCoinsViewCache::UncacheTx(const CTransaction &tx)
{
    for (const CTxIn &txin : tx.vin)
        Uncache(txin.prevout);
}

unsigned int CCoinsViewCache::GetCacheSize() const
{
    LOCK(cs_utxo);
    return cacheCoins.size();
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction &tx) const
{
    LOCK(cs_utxo);
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += AccessCoin(tx.vin[i].prevout).out.nValue;

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction &tx) const
{
    LOCK(cs_utxo);
    if (!tx.IsCoinBase())
    {
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            if (!HaveCoin(tx.vin[i].prevout))
            {
                return false;
            }
        }
    }
    return true;
}

double CCoinsViewCache::GetPriority(const CTransaction &tx, int nHeight, CAmount &inChainInputValue) const
{
    LOCK(cs_utxo);
    inChainInputValue = 0;
    if (tx.IsCoinBase())
        return 0.0;
    double dResult = 0.0;
    BOOST_FOREACH (const CTxIn &txin, tx.vin)
    {
        const Coin &coin = AccessCoin(txin.prevout);
        if (coin.IsSpent())
            continue;
        if (coin.nHeight <= nHeight)
        {
            dResult += coin.out.nValue * (nHeight - coin.nHeight);
            inChainInputValue += coin.out.nValue;
        }
    }
    return tx.ComputePriority(dResult);
}


CCoinsViewCursor::~CCoinsViewCursor() {}
static const size_t nMaxOutputsPerBlock =
    DEFAULT_LARGEST_TRANSACTION / ::GetSerializeSize(CTxOut(), SER_NETWORK, PROTOCOL_VERSION);
const Coin &AccessByTxid(const CCoinsViewCache &view, const uint256 &txid)
{
    COutPoint iter(txid, 0);
    while (iter.n < nMaxOutputsPerBlock)
    {
        const Coin &alternate = view.AccessCoin(iter);
        if (!alternate.IsSpent())
            return alternate;
        ++iter.n;
    }
    return coinEmpty;
}
