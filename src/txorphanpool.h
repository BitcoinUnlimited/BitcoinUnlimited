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
    // Used in EraseOrphansByTime() to track when the last time was we checked the cache for anything to delete
    int64_t nLastOrphanCheck;

public:
    // Current in memory footprint of all txns in the orphan pool.
    uint64_t nBytesOrphanPool;

    struct COrphanTx
    {
        CTransactionRef ptx;
        NodeId fromPeer;
        int64_t nEntryTime;
        uint64_t nOrphanTxSize;
    };

    CCriticalSection cs;
    std::map<uint256, COrphanTx> mapOrphanTransactions;
    std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev;

    CTxOrphanPool();

    bool AlreadyHaveOrphan(const uint256 &hash);
    bool AddOrphanTx(const CTransaction &tx, NodeId peer);
    void EraseOrphanTx(uint256 hash);
    void EraseOrphansByTime();
    unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans, uint64_t nMaxBytes);
};
extern CTxOrphanPool orphanpool;

#endif
