// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "memusage.h"
#include "random.h"
#include "util.h"

#include <assert.h>

/**
 * calculate number of bytes for the bitmask, and its number of non-zero bytes
 * each bit in the bitmask represents the availability of one output, but the
 * availabilities of the first two outputs are encoded separately
 */
void CCoins::CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const {
    unsigned int nLastUsedByte = 0;
    for (unsigned int b = 0; 2+b*8 < vout.size(); b++) {
        bool fZero = true;
        for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++) {
            if (!vout[2+b*8+i].IsNull()) {
                fZero = false;
                continue;
            }
        }
        if (!fZero) {
            nLastUsedByte = b + 1;
            nNonzeroBytes++;
        }
    }
    nBytes += nLastUsedByte;
}

bool CCoins::Spend(uint32_t nPos)
{
    if (nPos >= vout.size() || vout[nPos].IsNull())
        return false;
    vout[nPos].SetNull();
    Cleanup();
    return true;
}

bool CCoinsView::GetCoins(const uint256 &txid, CCoins &coins) const { return false; }
bool CCoinsView::HaveCoins(const uint256 &txid) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, size_t &nChildCachedCoinsUsage) { return false; }
bool CCoinsView::GetStats(CCoinsStats &stats) const { return false; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }
bool CCoinsViewBacked::GetCoins(const uint256 &txid, CCoins &coins) const { return base->GetCoins(txid, coins); }
bool CCoinsViewBacked::HaveCoins(const uint256 &txid) const { return base->HaveCoins(txid); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, size_t &nChildCachedCoinsUsage) { return base->BatchWrite(mapCoins, hashBlock, nChildCachedCoinsUsage); }
bool CCoinsViewBacked::GetStats(CCoinsStats &stats) const { return base->GetStats(stats); }

CCoinsKeyHasher::CCoinsKeyHasher() : salt(GetRandHash()) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), hasModifier(false), cachedCoinsUsage(0) { }

CCoinsViewCache::~CCoinsViewCache()
{
    WRITELOCK(cs_utxo);
    assert(!hasModifier);
}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    READLOCK(cs_utxo);
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

size_t CCoinsViewCache::_DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

size_t CCoinsViewCache::ResetCachedCoinUsage() const
{

    bool drifted=false;
    size_t newCachedCoinsUsage = 0;
    if(1)
    {
    READLOCK(cs_utxo);
    assert(!hasModifier);
    for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++)
        newCachedCoinsUsage += it->second.coins.DynamicMemoryUsage();
    drifted = cachedCoinsUsage != newCachedCoinsUsage;
    }

    if (drifted)
    {
        WRITELOCK(cs_utxo);
        error("Resetting: cachedCoinsUsage has drifted - before %lld after %lld", cachedCoinsUsage,
            newCachedCoinsUsage);
        cachedCoinsUsage = newCachedCoinsUsage;
    }
    return newCachedCoinsUsage;
}


CCoinsMap::const_iterator CCoinsViewCache::FetchCoins(const uint256 &txid, CDeferredSharedLocker& lock) const
{
    AssertLockHeld(cs_utxo);
    {
        lock.lock_shared();
        CCoinsMap::iterator it = cacheCoins.find(txid);
        if (it != cacheCoins.end())
            return it;
        lock.unlock();
    }
    CCoins tmp;
    if (!base->GetCoins(txid, tmp))
        return cacheCoins.end();
    // Note iterators are unaffected by map insertions so its ok to do this with just the read
    // lock held.
    CCoinsMap::iterator ret;

    lock.lock();
    ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry())).first;
    tmp.swap(ret->second.coins);
    if (ret->second.coins.IsPruned()) {
        // The parent only has an empty entry for this txid; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coins.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::FetchCoins(const uint256 &txid, CCoins *coins) const
{
    AssertLockHeld(cs_utxo);
    {
        READLOCK(csCacheInsert);
        CCoinsMap::iterator it = cacheCoins.find(txid);
        if (it != cacheCoins.end())
        {
            if (coins) *coins = it->second.coins;
            return true;
        }
    }
    CCoins tmp;
    if (!base->GetCoins(txid, tmp))
        return false;
    // Note iterators are unaffected by map insertions so its ok to do this with just the read
    // lock held.
    CCoinsMap::iterator ret;
    {
        WRITELOCK(csCacheInsert);
        ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry())).first;
        tmp.swap(ret->second.coins);
        if (ret->second.coins.IsPruned())
        {
            // The parent only has an empty entry for this txid; we can consider our
            // version as fresh.
            ret->second.flags = CCoinsCacheEntry::FRESH;
        }
        cachedCoinsUsage += ret->second.coins.DynamicMemoryUsage();
    }

    if (coins) *coins = ret->second.coins;
    return true;
}

bool CCoinsViewCache::GetCoins(const uint256 &txid, CCoins &coins) const {
    READLOCK(cs_utxo);
    return FetchCoins(txid, &coins);
}

const CCoins* CCoinsViewCache::_AccessCoins(const uint256 &txid, CDeferredSharedLocker& lock) const
{
    CCoinsMap::const_iterator it = FetchCoins(txid, lock);
    if (it == cacheCoins.end()) {
        return NULL;
    } else {
        return &it->second.coins;
    }
}

bool CCoinsViewCache::HaveCoins(const uint256 &txid) const {
    READLOCK(cs_utxo);
    CDeferredSharedLocker lock(csCacheInsert);
    CCoinsMap::const_iterator it = FetchCoins(txid, lock);
    // We're using vtx.empty() instead of IsPruned here for performance reasons,
    // as we only care about the case where a transaction was replaced entirely
    // in a reorganization (which wipes vout entirely, as opposed to spending
    // which just cleans individual outputs).
    return (it != cacheCoins.end() && !it->second.coins.vout.empty());
}

bool CCoinsViewCache::ModifyCoins(const uint256 &txid, CCoinsModifier& ret)
{
    WRITELOCK(cs_utxo);
    ret.lock(*this);
    std::pair<CCoinsMap::iterator, bool> data;
    data = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    size_t cachedCoinUsage = 0;
    if (data.second) {
        if (!base->GetCoins(txid, data.first->second.coins)) {
            // The parent view does not have this entry; mark it as fresh.
            data.first->second.coins.Clear();
            data.first->second.flags = CCoinsCacheEntry::FRESH;
        } else if (data.first->second.coins.IsPruned()) {
            // The parent view only has a pruned entry for this; mark it as fresh.
            data.first->second.flags = CCoinsCacheEntry::FRESH;
        }
    } else {
        cachedCoinUsage = data.first->second.coins.DynamicMemoryUsage();
    }
    // Assume that whenever ModifyCoins is called, the entry will be modified.
    data.first->second.flags |= CCoinsCacheEntry::DIRTY;
    ret.it = data.first;
    ret.cachedCoinUsage = cachedCoinUsage;
    return true;
}

bool CCoinsViewCache::ModifyNewCoins(const uint256 &txid, CCoinsModifier& ret)
{
    WRITELOCK(cs_utxo);
    ret.lock(*this);
    assert(!hasModifier);
    std::pair<CCoinsMap::iterator, bool> data;
    data = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    data.first->second.coins.Clear();
    data.first->second.flags = CCoinsCacheEntry::FRESH;
    data.first->second.flags |= CCoinsCacheEntry::DIRTY;
    ret.it = data.first;
    ret.cachedCoinUsage = 0;
    return true;
}


bool CCoinsViewCache::HaveCoinsInCache(const uint256 &txid) const {
    READLOCK(cs_utxo);
    CCoinsMap::const_iterator it = cacheCoins.find(txid);
    return it != cacheCoins.end();
}

uint256 CCoinsViewCache::GetBestBlock() const {
    READLOCK(cs_utxo);
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    WRITELOCK(cs_utxo);
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlockIn, size_t &nChildCachedCoinsUsage)
{
    WRITELOCK(cs_utxo);
    assert(!hasModifier);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {

        if (it->second.flags & CCoinsCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            // Update usage of the chile cache before we do any swapping and deleting
            nChildCachedCoinsUsage -= it->second.coins.DynamicMemoryUsage();

            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                // The parent cache does not have an entry, while the child does
                // We can ignore it if it's both FRESH and pruned in the child
                if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coins.IsPruned())) {
                    // Otherwise we will need to create it in the parent
                    // and move the data up and mark it as dirty
                    CCoinsCacheEntry& entry = cacheCoins[it->first];
                    entry.coins.swap(it->second.coins);
                    cachedCoinsUsage += entry.coins.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY;
                    // We can mark it FRESH in the parent if it was FRESH in the child
                    // Otherwise it might have just been flushed from the parent's cache
                    // and already exist in the grandparent
                    if (it->second.flags & CCoinsCacheEntry::FRESH)
                        entry.flags |= CCoinsCacheEntry::FRESH;
                }
            } else {
                // Found the entry in the parent cache
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.coins.swap(it->second.coins);
                    cachedCoinsUsage += itUs->second.coins.DynamicMemoryUsage();
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
            cachedCoinsUsage -= iter->second.coins.DynamicMemoryUsage();

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

void CCoinsViewCache::Uncache(const uint256& hash)
{
    WRITELOCK(cs_utxo);
    CCoinsMap::iterator it = cacheCoins.find(hash);
    if (it != cacheCoins.end() && it->second.flags == 0) {
        cachedCoinsUsage -= it->second.coins.DynamicMemoryUsage();
        cacheCoins.erase(it);
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    READLOCK(cs_utxo);
    return cacheCoins.size();
}

bool CCoinsViewCache::GetOutputFor(const CTxIn& input, CTxOut& ret) const
{
    READLOCK(cs_utxo);
    CDeferredSharedLocker lock(csCacheInsert);
    const CCoins* coins = _AccessCoins(input.prevout.hash, lock);
    if (!(coins && coins->IsAvailable(input.prevout.n))) return false;
    ret = coins->vout[input.prevout.n];
    return true;
}

const CTxOut &CCoinsViewCache::_GetOutputFor(const CTxIn& input, CDeferredSharedLocker& lock) const
{
    const CCoins* coins = _AccessCoins(input.prevout.hash, lock);
    assert(coins && coins->IsAvailable(input.prevout.n));
    return coins->vout[input.prevout.n];
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction& tx) const
{
    READLOCK(cs_utxo);
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        CDeferredSharedLocker lock(csCacheInsert);
        nResult += _GetOutputFor(tx.vin[i], lock).nValue;
    }

    return nResult;
}

bool CCoinsViewCache::HaveInput(const COutPoint &prevout) const
{
    READLOCK(cs_utxo);
    CDeferredSharedLocker lock(csCacheInsert);
    const CCoins *coins = _AccessCoins(prevout.hash, lock);
    if (!coins || !coins->IsAvailable(prevout.n))
    {
        return false;
    }
    return true;
}

bool CCoinsViewCache::HaveInputs(const CTransaction &tx) const
{
    READLOCK(cs_utxo);
    if (!tx.IsCoinBase())
    {
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            CDeferredSharedLocker lock(csCacheInsert);
            const COutPoint &prevout = tx.vin[i].prevout;
            const CCoins *coins = _AccessCoins(prevout.hash, lock);
            if (!coins || !coins->IsAvailable(prevout.n))
            {
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
        CDeferredSharedLocker lock(csCacheInsert);
        const CCoins* coins = _AccessCoins(txin.prevout.hash, lock);
        assert(coins);
        if (!coins->IsAvailable(txin.prevout.n)) continue;
        if (coins->nHeight <= nHeight) {
            dResult += coins->vout[txin.prevout.n].nValue * (nHeight-coins->nHeight);
            inChainInputValue += coins->vout[txin.prevout.n].nValue;
        }
    }
    return tx.ComputePriority(dResult);
}

CCoinsModifier::CCoinsModifier():cache(nullptr)
{
}

CCoinsModifier::~CCoinsModifier()
{
    if (cache)
    {
        //assert(cache->hasModifier);
        //cache->hasModifier = false;
        it->second.coins.Cleanup();
        cache->cachedCoinsUsage -= cachedCoinUsage; // Subtract the old usage
        if ((it->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned())
        {
            cache->cacheCoins.erase(it);
        }
        else
        {
            // If the coin still exists after the modification, add the new usage
            cache->cachedCoinsUsage += it->second.coins.DynamicMemoryUsage();
        }
        cache->csCacheInsert.unlock();
        cache=nullptr;
    }
}

CCoinsAccessor::CCoinsAccessor(const CCoinsViewCache &cacheObj, const uint256& hash):
    cache(&cacheObj), lock(cache->csCacheInsert)
{
    cache->cs_utxo.lock_shared();
    EnterCritical("CCoinsViewCache.cs_utxo", __FILE__, __LINE__, (void*)(&cache->cs_utxo));
    it  = cache->FetchCoins(hash, lock);
    if (it != cache->cacheCoins.end()) coins = &it->second.coins;
    else coins = nullptr;
}
