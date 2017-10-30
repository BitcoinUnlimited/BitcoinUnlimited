// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "consensus/consensus.h"
#include "memusage.h"
#include "random.h"
#include "util.h"

#include <assert.h>
Coin emptyCoin;
bool CCoinsView::GetCoin(const COutPoint &outpoint, Coin &coin) const { return false; }
bool CCoinsView::HaveCoin(const COutPoint &outpoint) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, size_t &nChildCachedCoinsUsage) { return false; }
CCoinsViewCursor *CCoinsView::Cursor() const { return nullptr; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }
bool CCoinsViewBacked::GetCoin(const COutPoint &outpoint, Coin &coin) const { return base->GetCoin(outpoint, coin); }
bool CCoinsViewBacked::HaveCoin(const COutPoint &outpoint) const { return base->HaveCoin(outpoint); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, size_t &nChildCachedCoinsUsage) { return base->BatchWrite(mapCoins, hashBlock, nChildCachedCoinsUsage); }
CCoinsViewCursor *CCoinsViewBacked::Cursor() const { return base->Cursor(); }
size_t CCoinsViewBacked::EstimateSize() const { return base->EstimateSize(); }

SaltedOutpointHasher::SaltedOutpointHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), cachedCoinsUsage(0) {}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    READLOCK(cs_utxo);
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}
size_t CCoinsViewCache::_DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

size_t CCoinsViewCache::ResetCachedCoinUsage() const
{
    bool drifted = false;
    size_t newCachedCoinsUsage = 0;
    if (1)
    {
        READLOCK(cs_utxo);
        size_t newCachedCoinsUsage = 0;
        for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++)
            newCachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
        drifted = (cachedCoinsUsage != newCachedCoinsUsage);
    }
    if (drifted)
    {
        error(
            "Resetting: cachedCoinsUsage has drifted - before %lld after %lld", cachedCoinsUsage, newCachedCoinsUsage);
        cachedCoinsUsage = newCachedCoinsUsage;
    }
    return newCachedCoinsUsage;
}

CCoinsMap::iterator CCoinsViewCache::FetchCoin(const COutPoint &outpoint, CDeferredSharedLocker* lock) const
{
    //AssertLockHeld(cs_utxo);
    {
        if (lock) lock->lock_shared();
        CCoinsMap::iterator it = cacheCoins.find(outpoint);
        if (it != cacheCoins.end())
            return it;
        if (lock) lock->unlock();
    }
    Coin tmp;
    if (!base->GetCoin(outpoint, tmp))
        return cacheCoins.end();

    if (lock) lock->lock();
    CCoinsMap::iterator ret = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::forward_as_tuple(std::move(tmp))).first;
    if (ret->second.coin.IsSpent()) {
        // The parent only has an empty entry for this outpoint; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::GetCoin(const COutPoint &outpoint, Coin &coin) const
{
    CDeferredSharedLocker lock(cs_utxo);
    CCoinsMap::const_iterator it = FetchCoin(outpoint,&lock);
    if (it != cacheCoins.end()) {
        coin = it->second.coin;
        return true;
    }
    return false;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin&& coin, bool possible_overwrite) {
    WRITELOCK(cs_utxo);
    assert(!coin.IsSpent());
    if (coin.out.scriptPubKey.IsUnspendable()) return;
    CCoinsMap::iterator it;
    bool inserted;
    std::tie(it, inserted) = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!inserted) {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    }
    if (!possible_overwrite) {
        if (!it->second.coin.IsSpent()) {
            throw std::logic_error("Adding new coin that replaces non-pruned entry");
        }
        fresh = !(it->second.flags & CCoinsCacheEntry::DIRTY);
    }
    it->second.coin = std::move(coin);
    it->second.flags |= CCoinsCacheEntry::DIRTY | (fresh ? CCoinsCacheEntry::FRESH : 0);
    cachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
}

void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight) {
    bool fCoinbase = tx.IsCoinBase();
    const uint256& txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        // Pass fCoinbase as the possible_overwrite flag to AddCoin, in order to correctly
        // deal with the pre-BIP30 occurrances of duplicate coinbase transactions.
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase), fCoinbase);
    }
}

void CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin* moveout) {
    WRITELOCK(cs_utxo);
    CCoinsMap::iterator it = FetchCoin(outpoint, nullptr);
    if (it == cacheCoins.end()) return;
    cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    if (moveout) {
        *moveout = std::move(it->second.coin);
    }
    if (it->second.flags & CCoinsCacheEntry::FRESH) {
        cacheCoins.erase(it);
    } else {
        it->second.flags |= CCoinsCacheEntry::DIRTY;
        it->second.coin.Clear();
    }
}

static const Coin coinEmpty;

const Coin& CCoinsViewCache::_AccessCoin(const COutPoint &outpoint) const {
    AssertLockHeld(cs_utxo);
    CCoinsMap::const_iterator it = FetchCoin(outpoint, nullptr);
    if (it == cacheCoins.end()) {
        return coinEmpty;
    } else {
        return it->second.coin;
    }
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const {
    CDeferredSharedLocker lock(cs_utxo);
    CCoinsMap::const_iterator it = FetchCoin(outpoint, &lock);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const {
    READLOCK(cs_utxo);
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return it != cacheCoins.end();
}

uint256 CCoinsViewCache::GetBestBlock() const {
    READLOCK(cs_utxo);
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

uint256 CCoinsViewCache::_GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    READLOCK(cs_utxo);
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlockIn, size_t &nChildCachedCoinsUsage) {
    WRITELOCK(cs_utxo);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        
        if (it->second.flags & CCoinsCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            // Update usage of the chile cache before we do any swapping and deleting
            nChildCachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();

            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                // The parent cache does not have an entry, while the child does
                // We can ignore it if it's both FRESH and pruned in the child
                if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {
                    // Otherwise we will need to create it in the parent
                    // and move the data up and mark it as dirty
                    CCoinsCacheEntry& entry = cacheCoins[it->first];
                    entry.coin = std::move(it->second.coin);
                    cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY;
                    // We can mark it FRESH in the parent if it was FRESH in the child
                    // Otherwise it might have just been flushed from the parent's cache
                    // and already exist in the grandparent
                    if (it->second.flags & CCoinsCacheEntry::FRESH)
                        entry.flags |= CCoinsCacheEntry::FRESH;
                }
            } else {
                // Assert that the child cache entry was not marked FRESH if the
                // parent cache entry has unspent outputs. If this ever happens,
                // it means the FRESH flag was misapplied and there is a logic
                // error in the calling code.
                if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent())
                    throw std::logic_error("FRESH flag misapplied to cache entry for base transaction with spendable outputs");

                // Found the entry in the parent cache
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coin.IsSpent()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
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
    return true;
}

bool CCoinsViewCache::Flush() {
    WRITELOCK(cs_utxo);
    bool fOk = base->BatchWrite(cacheCoins, hashBlock, cachedCoinsUsage);
    return fOk;
}

void CCoinsViewCache::Trim(size_t nTrimSize) const
{
    uint64_t nTrimmed = 0;

    WRITELOCK(cs_utxo);
    CCoinsMap::iterator iter = cacheCoins.begin();
    while (_DynamicMemoryUsage() > nTrimSize)
    {
        if (iter == cacheCoins.end())
            break;

        // Only erase entries that have not been modified
        if (iter->second.flags == 0)
        {
            cachedCoinsUsage -= iter->second.coin.DynamicMemoryUsage();

            CCoinsMap::iterator itOld = iter++;
            cacheCoins.erase(itOld);
            nTrimmed++;
        }
        else
            iter++;
    }

    if (nTrimmed > 0)
        LogPrint("coindb", "Trimmed %ld from the CoinsViewCache, current size after trim: %ld and usage %ld bytes\n", nTrimmed, cacheCoins.size(), cachedCoinsUsage);
}

void CCoinsViewCache::Uncache(const COutPoint& hash)
{
    WRITELOCK(cs_utxo);
    CCoinsMap::iterator it = cacheCoins.find(hash);
    if (it != cacheCoins.end() && it->second.flags == 0) {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
        cacheCoins.erase(it);
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    READLOCK(cs_utxo);
    return cacheCoins.size();
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction& tx) const
{
    READLOCK(cs_utxo);
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += _AccessCoin(tx.vin[i].prevout).out.nValue;

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            if (!HaveCoin(tx.vin[i].prevout)) {
                return false;
            }
        }
    }
    return true;
}

double CCoinsViewCache::GetPriority(const CTransaction &tx, int nHeight, CAmount &inChainInputValue) const
{
    READLOCK(cs_utxo);
    inChainInputValue = 0;
    if (tx.IsCoinBase())
        return 0.0;
    double dResult = 0.0;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        const Coin &coin = _AccessCoin(txin.prevout);
        if (coin.IsSpent()) continue;
        if (coin.nHeight <= nHeight) {
            dResult += coin.out.nValue * (nHeight-coin.nHeight);
            inChainInputValue += coin.out.nValue;
        }
    }
    return tx.ComputePriority(dResult);
}


CCoinsViewCursor::~CCoinsViewCursor()
{
}

static const size_t nMaxOutputsPerBlock = DEFAULT_LARGEST_TRANSACTION / ::GetSerializeSize(CTxOut(), SER_NETWORK, PROTOCOL_VERSION);

#if 0
const Coin& AccessByTxid(const CCoinsViewCache& view, const uint256& txid)
{
    COutPoint iter(txid, 0);
    while (iter.n < nMaxOutputsPerBlock) {
        const Coin& alternate = view._AccessCoin(iter);
        if (!alternate.IsSpent()) return alternate;
        ++iter.n;
    }
    return coinEmpty;
}
#endif


CoinAccessor::CoinAccessor(const CCoinsViewCache& view, const uint256& txid):
    cache(&view), lock(cache->csCacheInsert)
{
    cache->cs_utxo.lock_shared();
    EnterCritical("CCoinsViewCache.cs_utxo", __FILE__, __LINE__, (void*)(&cache->cs_utxo));
    COutPoint iter(txid, 0);
    coin = &emptyCoin;
    while (iter.n < nMaxOutputsPerBlock)
    {
        const Coin& alternate = view._AccessCoin(iter);
        if (!alternate.IsSpent())
        {
            coin=&alternate;
            return;
        }
        ++iter.n;
    }
}

CoinAccessor::CoinAccessor(const CCoinsViewCache &cacheObj, const COutPoint &output):
    cache(&cacheObj), lock(cache->csCacheInsert)
{
    cache->cs_utxo.lock_shared();
    EnterCritical("CCoinsViewCache.cs_utxo", __FILE__, __LINE__, (void*)(&cache->cs_utxo));
    it  = cache->FetchCoin(output, &lock);
    if (it != cache->cacheCoins.end()) coin = &it->second.coin;
    else coin = &emptyCoin;
}

CoinAccessor::~CoinAccessor()
    {
        coin=nullptr;
        LeaveCritical();
        cache->cs_utxo.unlock_shared();
    }


CoinModifier::CoinModifier(const CCoinsViewCache &cacheObj, const COutPoint &output):
    cache(&cacheObj)
{
    cache->cs_utxo.lock();
    EnterCritical("CCoinsViewCache.cs_utxo", __FILE__, __LINE__, (void*)(&cache->cs_utxo));
    it  = cache->FetchCoin(output, nullptr);
    if (it != cache->cacheCoins.end()) coin = &it->second.coin;
    else coin = &emptyCoin;
}

CoinModifier::~CoinModifier()
    {
        coin=nullptr;
        LeaveCritical();
        cache->cs_utxo.unlock();
    }

