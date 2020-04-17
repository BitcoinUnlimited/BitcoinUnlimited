// Copyright (c) 2018 The Bitcoin Unlimited developers
// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TX_ORPHANPOOL
#define BITCOIN_TX_ORPHANPOOL

#include "net.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "uint256.h"

#include <map>
#include <set>
#include <stdint.h>

class CTxOrphanPool
{
private:
    //! Used in EraseOrphansByTime() to track when the last time was we checked the cache for anything to delete
    int64_t nLastOrphanCheck;

public:
    //! Current in memory footprint of all txns in the orphan pool.
    uint64_t nBytesOrphanPool;

    struct COrphanTx
    {
        CTransactionRef ptx;
        NodeId fromPeer;
        int64_t nEntryTime;
        uint64_t nOrphanTxSize;
    };

    CSharedCriticalSection cs_orphanpool;
    std::map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(cs_orphanpool);
    std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_orphanpool);

    CTxOrphanPool();

    //! Do we already have this orphan in the orphan pool
    bool AlreadyHaveOrphan(const uint256 &hash);

    //! Add a transaction to the orphan pool
    bool AddOrphanTx(const CTransactionRef ptx, NodeId peer);

    //! Erase an ophan tx from the orphan pool
    //! @return true if an orphan matching the hash was found in the orphanpool and successfully erased.
    bool EraseOrphanTx(uint256 hash);

    //! Expire old orphans from the orphan pool
    void EraseOrphansByTime();

    //! Limit the orphan pool size by either number of transactions or the max orphan pool size allowed.
    unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans, uint64_t nMaxBytes);

    //! Return all the transaction hashes for transactions currently in the orphan pool.
    void QueryHashes(std::vector<uint256> &vHashes);

    //! Set the last orphan check time (used primarily in testing)
    void SetLastOrphanCheck(int64_t nTime) { nLastOrphanCheck = nTime; }
    //! Orphan pool current number of transactions
    uint64_t GetOrphanPoolSize()
    {
        READLOCK(cs_orphanpool);
        return mapOrphanTransactions.size();
    }

    //! Orphan pool bytes used
    uint64_t GetOrphanPoolBytes()
    {
        READLOCK(cs_orphanpool);
        return nBytesOrphanPool;
    }
};
extern CTxOrphanPool orphanpool;

#endif
