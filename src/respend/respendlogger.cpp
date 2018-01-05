// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "respend/respendlogger.h"
#include "util.h"

namespace respend
{
RespendLogger::RespendLogger() : equivalent(false), valid("indeterminate"), newConflict(false) {}
bool RespendLogger::AddOutpointConflict(const COutPoint &,
    const CTxMemPool::txiter mempoolEntry,
    const CTransaction &respendTx,
    bool seen,
    bool isEquivalent)
{
    orig = mempoolEntry->GetTx().GetHash().ToString();
    respend = respendTx.GetHash().ToString();
    equivalent = isEquivalent;
    newConflict = newConflict || !seen;

    // We have enough info for logging purposes.
    return false;
}

bool RespendLogger::IsInteresting() const
{
    // Logging never triggers full tx validation
    return false;
}

void RespendLogger::Trigger()
{
    if (respend.empty())
        return;

    const std::string msg = "respend: Tx %s conflicts with %s"
                            " (new conflict: %s, equivalent %s, valid %s)\n";

    LOG(RESPEND, msg.c_str(), orig, respend, newConflict ? "yes" : "no", equivalent ? "yes" : "no", valid);
}
}
