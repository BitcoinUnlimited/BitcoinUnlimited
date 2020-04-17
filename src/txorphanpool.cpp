// Copyright (c) 2018 The Bitcoin Unlimited developers
// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txorphanpool.h"
#include "main.h"
#include "timedata.h"
#include "util.h"
#include "utiltime.h"

CTxOrphanPool::CTxOrphanPool() : nLastOrphanCheck(GetTime()), nBytesOrphanPool(0){};

bool CTxOrphanPool::AlreadyHaveOrphan(const uint256 &hash)
{
    READLOCK(cs_orphanpool);
    if (mapOrphanTransactions.count(hash))
        return true;
    return false;
}

bool CTxOrphanPool::AddOrphanTx(const CTransactionRef ptx, NodeId peer)
{
    AssertWriteLockHeld(cs_orphanpool);

    if (mapOrphanTransactions.empty())
        DbgAssert(nBytesOrphanPool == 0, nBytesOrphanPool = 0);

    uint256 hash = ptx->GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore orphans larger than the largest txn size allowed.
    if (ptx->GetTxSize() > MAX_STANDARD_TX_SIZE)
    {
        LOG(MEMPOOL, "ignoring large orphan tx (size: %u, hash: %s)\n", ptx->GetTxSize(), hash.ToString());
        return false;
    }

    uint64_t nTxMemoryUsed = RecursiveDynamicUsage(*ptx) + sizeof(ptx);
    mapOrphanTransactions.emplace(hash, COrphanTx{ptx, peer, GetTime(), nTxMemoryUsed});
    for (const CTxIn &txin : ptx->vin)
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    nBytesOrphanPool += nTxMemoryUsed;
    LOG(MEMPOOL, "stored orphan tx %s bytes:%ld (mapsz %u prevsz %u), orphan pool bytes:%ld\n", hash.ToString(),
        nTxMemoryUsed, mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size(), nBytesOrphanPool);
    return true;
}

bool CTxOrphanPool::EraseOrphanTx(uint256 hash)
{
    AssertWriteLockHeld(cs_orphanpool);

    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return false;
    for (const CTxIn &txin : it->second.ptx->vin)
    {
        std::map<uint256, std::set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }

    nBytesOrphanPool -= it->second.nOrphanTxSize;
    LOG(MEMPOOL, "Erased orphan tx %s of size %ld bytes, orphan pool bytes:%ld\n", it->second.ptx->GetHash().ToString(),
        it->second.nOrphanTxSize, nBytesOrphanPool);
    mapOrphanTransactions.erase(it);
    return true;
}

void CTxOrphanPool::EraseOrphansByTime()
{
    AssertWriteLockHeld(cs_orphanpool);
    // Because we have to iterate through the entire orphan cache which can be large we don't want to check this
    // every time a tx enters the mempool but just once every 5 minutes is good enough.
    if (GetTime() < nLastOrphanCheck + 5 * 60)
        return;
    int64_t nOrphanTxCutoffTime = GetTime() - GetArg("-orphanpoolexpiry", DEFAULT_ORPHANPOOL_EXPIRY) * 60 * 60;
    std::map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end())
    {
        std::map<uint256, COrphanTx>::iterator mi = iter++; // increment to avoid iterator becoming invalid
        int64_t nEntryTime = mi->second.nEntryTime;
        if (nEntryTime < nOrphanTxCutoffTime)
        {
            uint256 txHash = mi->second.ptx->GetHash();

            // Uncache any coins that may exist for orphans that will be erased
            pcoinsTip->UncacheTx(*mi->second.ptx);

            LOG(MEMPOOL, "Erased old orphan tx %s of age %d seconds\n", txHash.ToString(), GetTime() - nEntryTime);
            EraseOrphanTx(txHash);
        }
    }

    nLastOrphanCheck = GetTime();
}

unsigned int CTxOrphanPool::LimitOrphanTxSize(unsigned int nMaxOrphans, uint64_t nMaxBytes)
{
    AssertWriteLockHeld(cs_orphanpool);

    // Limit the orphan pool size by either number of transactions or the max orphan pool size allowed.
    // Limiting by pool size to 1/10th the size of the maxmempool alone is not enough because the total number
    // of txns in the pool can adversely effect the size of the bloom filter in a get_xthin message.
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans || nBytesOrphanPool > nMaxBytes)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();

        // Uncache any coins that may exist for orphans that will be erased
        pcoinsTip->UncacheTx(*it->second.ptx);

        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

void CTxOrphanPool::QueryHashes(std::vector<uint256> &vHashes)
{
    READLOCK(cs_orphanpool);
    for (auto &it : mapOrphanTransactions)
        vHashes.push_back(it.first);
}
