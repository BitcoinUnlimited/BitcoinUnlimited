// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_RESPEND_RESPENDACTION_H
#define BITCOIN_RESPEND_RESPENDACTION_H

#include "txmempool.h"

#include <memory>

namespace respend
{
// A "respend action" is incrementally provided with information about a
// respending transaction. After, it will be triggered, allowing it to perform
// an action based on the information it gathered.
class RespendAction
{
public:
    virtual ~RespendAction() = 0;
    virtual bool AddOutpointConflict(
        // conflicting outpoint
        const COutPoint &out,
        // Existing mempool entry
        const CTxMemPool::txiter mempoolEntry,
        // Current TX that is respending
        const CTransaction &respendTx,
        // If we've seen a valid tx respending this output before
        bool seenBefore,
        // If original and respend tx only differ in script
        bool isEquivalent) = 0;

    // If this respend is interesting enough to this action to trigger full
    // tx validation.
    virtual bool IsInteresting() const = 0;
    // Called after tx is validated and only if it's validated.
    virtual void SetValid(bool) = 0;
    // Action should do its thing now.
    virtual void Trigger() = 0;
};
inline RespendAction::~RespendAction() {}
// shared_ptr, instead of unique_ptr, for unit testing
typedef std::shared_ptr<RespendAction> RespendActionPtr;

} // ns respend

#endif
