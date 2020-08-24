// Copyright (c) 2018 The Bitcoin developers
// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "respend/respenddetector.h"
#include "DoubleSpendProof.h"
#include "DoubleSpendProofStorage.h"
#include "bloom.h"
#include "dosman.h"
#include "respend/respendaction.h"
#include "respend/respendlogger.h"
#include "respend/respendrelayer.h"
#include "txmempool.h"
#include "util.h"

#include <algorithm>

extern CTweak<uint32_t> doubleSpendProofs;

namespace respend
{
static const unsigned int MAX_RESPEND_BLOOM = 100000;
std::unique_ptr<CRollingBloomFilter> RespendDetector::respentBefore;
std::mutex RespendDetector::respentBeforeMutex;

std::vector<RespendActionPtr> CreateDefaultActions()
{
    std::vector<RespendActionPtr> actions;

    actions.push_back(RespendActionPtr(new RespendRelayer{}));
    if (Logging::LogAcceptCategory(RESPEND))
    {
        actions.push_back(RespendActionPtr(new RespendLogger{}));
    }
    return actions;
}

RespendDetector::RespendDetector(const CTxMemPool &pool,
    const CTransactionRef ptx,
    std::vector<RespendActionPtr> _actions)
    : actions(_actions)
{
    {
        std::lock_guard<std::mutex> lock(respentBeforeMutex);
        if (!bool(respentBefore))
        {
            respentBefore.reset(new CRollingBloomFilter(MAX_RESPEND_BLOOM, 0.01));
        }
    }
    CheckForRespend(pool, ptx);
}

RespendDetector::~RespendDetector()
{
    // Time for actions to perform their task using the (limited)
    // information they've gathered.
    for (auto &a : actions)
    {
        try
        {
            a->Trigger(mempool);
        }
        catch (const std::exception &e)
        {
            LOGA("respend: ERROR - respend action threw: %s\n", e.what());
        }
    }
}

void RespendDetector::CheckForRespend(const CTxMemPool &pool, const CTransactionRef ptx)
{
    READLOCK(pool.cs_txmempool); // protect pool.mapNextTx

    for (const CTxIn &in : ptx->vin)
    {
        const COutPoint outpoint = in.prevout;

        if (doubleSpendProofs.Value())
        {
            // Check first if there are already double spend orphans. If there are
            // then we can broadcast them here and continue without needing to check
            // further for conflicts for this outpoint.
            auto orphans = pool.doubleSpendProofStorage()->findOrphans(outpoint);
            if (!orphans.empty())
            {
                for (auto iter = orphans.begin(); iter != orphans.end(); iter++)
                {
                    const int proofId = iter->first;
                    auto dsp = pool.doubleSpendProofStorage()->proof(proofId);
                    LOG(DSPROOF, "Rescued a DoubleSpendProof orphan %d", proofId);
                    auto rc = dsp.validate(pool, ptx);
                    DbgAssert(rc == DoubleSpendProof::Valid || rc == DoubleSpendProof::Invalid, );

                    if (rc == DoubleSpendProof::Valid)
                    {
                        LOG(DSPROOF, "DoubleSpendProof for orphan validated correctly %d", proofId);
                        pool.doubleSpendProofStorage()->claimOrphan(proofId);
                        {
                            std::lock_guard<std::mutex> lock(respentBeforeMutex);
                            dsproof = proofId;
                        }

                        // remove all other orphans since we only need one
                        while (++iter != orphans.end())
                        {
                            pool.doubleSpendProofStorage()->remove(iter->first);
                            LOG(DSPROOF, "Removing DoubleSpendProof orphan, we only need one %d", proofId);
                        }

                        // Finally, send the dsp inventory message
                        broadcastDspInv(ptx, dsp.GetHash());
                        break;
                    }
                    else
                    {
                        LOG(DSPROOF, "DoubleSpendProof did not validate %s", dsp.GetHash().ToString());
                        pool.doubleSpendProofStorage()->remove(proofId);
                        dosMan.Misbehaving(iter->second, 5);
                    }
                }
            }
        }

        // Is there a conflicting spend?
        auto spendIter = pool.mapNextTx.find(outpoint);
        if (spendIter == pool.mapNextTx.end())
            continue;

        conflictingOutpoints.push_back(outpoint);

        CTxMemPool::txiter poolIter = pool.mapTx.find(spendIter->second.ptx->GetHash());
        if (poolIter == pool.mapTx.end())
            continue;

        bool collectMore = false;
        bool seen;
        {
            std::lock_guard<std::mutex> lock(respentBeforeMutex);
            seen = respentBefore->contains(outpoint);
        }
        for (auto &a : actions)
        {
            // Actions can return true if they want to check more
            // outpoints for conflicts.
            bool m = a->AddOutpointConflict(
                outpoint, poolIter->GetSharedTx()->GetHash(), ptx, seen, ptx->IsEquivalentTo(poolIter->GetTx()));
            collectMore = collectMore || m;
        }
        if (!collectMore)
            return;
    }
}

void RespendDetector::SetValid(bool valid)
{
    if (valid)
    {
        std::lock_guard<std::mutex> lock(respentBeforeMutex);
        for (auto &o : conflictingOutpoints)
        {
            respentBefore->insert(o);
        }
    }
    for (auto &a : actions)
    {
        a->SetValid(valid);
    }
}

bool RespendDetector::IsRespend() const { return !conflictingOutpoints.empty(); }
bool RespendDetector::IsInteresting() const
{
    return std::any_of(begin(actions), end(actions), [](const RespendActionPtr &a) { return a->IsInteresting(); });
}

int RespendDetector::GetDsproof() const { return dsproof; }
} // ns respend
