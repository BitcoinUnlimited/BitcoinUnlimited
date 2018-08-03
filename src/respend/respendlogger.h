// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_RESPEND_RESPENDLOGGER_H
#define BITCOIN_RESPEND_RESPENDLOGGER_H

#include "respend/respendaction.h"
#include <string>

namespace respend
{
class RespendLogger : public RespendAction
{
public:
    RespendLogger();

    bool AddOutpointConflict(const COutPoint &,
        const CTxMemPool::txiter mempoolEntry,
        const CTransaction &respendTx,
        bool seen,
        bool isEquivalent) override;

    virtual bool IsInteresting() const override;

    void Trigger() override;

    void SetValid(bool v) override { valid = v ? "yes" : "no"; }
private:
    std::string orig;
    std::string respend;
    bool equivalent;
    std::string valid;
    bool newConflict; // TX has at least 1 output that's not respent earlier
};

} // ns respend

#endif
