// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_RESPEND_RESPENDDETECTOR_H
#define BITCOIN_RESPEND_RESPENDDETECTOR_H

#include "respend/respendaction.h"

#include <atomic>
#include <memory>
#include <mutex>

class CRollingBloomFilter;

namespace respend
{
std::vector<RespendActionPtr> CreateDefaultActions();

// Detects if a transaction is in conflict with mempool, and feeds various
// actions with data about the respend. Finally triggers the actions.
class RespendDetector
{
public:
    RespendDetector(const CTxMemPool &pool,
        const CTransaction &tx,
        std::vector<RespendActionPtr> = CreateDefaultActions());

    ~RespendDetector();
    void CheckForRespend(const CTxMemPool &pool, const CTransaction &tx);
    void SetValid(bool valid);
    bool IsRespend() const;

    // Respend is interesting enough to trigger full tx validation.
    bool IsInteresting() const;

private:
    std::vector<COutPoint> conflictingOutpoints;

    // Outputs we've already seen in valid double spending transactions
    static std::unique_ptr<CRollingBloomFilter> respentBefore;
    static std::mutex respentBeforeMutex;
    std::vector<RespendActionPtr> actions;
};

} // ns respend

#endif
