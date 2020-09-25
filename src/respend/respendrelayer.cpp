// Copyright (c) 2018 The Bitcoin developers
// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "respend/respendrelayer.h"
#include "DoubleSpendProof.h"
#include "DoubleSpendProofStorage.h"
#include "net.h" // RelayTransaction
#include "primitives/transaction.h"
#include "protocol.h"
#include "streams.h"
#include "util.h"
#include <mutex>

extern CTweak<uint32_t> doubleSpendProofs;

namespace respend
{
namespace
{
// Exponentially limit the rate of nSize flow to nLimit.  nLimit unit is thousands-per-minute.
bool RateLimitExceeded(double &dCount, int64_t &nLastTime, int64_t nLimit, unsigned int nSize)
{
    int64_t nNow = GetTime();
    dCount *= std::pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));
    nLastTime = nNow;
    if (dCount >= nLimit * 10 * 1000)
        return true;
    dCount += nSize;
    return false;
}

// Apply an independent rate limit to double-spend relays
class RelayLimiter
{
public:
    RelayLimiter() : respendCount(0), lastRespendTime(0) {}
    bool HasLimitExceeded(const CTransactionRef pDoubleSpend)
    {
        unsigned int size = pDoubleSpend->GetTxSize();

        std::lock_guard<std::mutex> lock(cs_relayLimiter);
        int64_t limit = GetArg("-limitrespendrelay", DEFAULT_LIMITRESPENDRELAY);
        if (RateLimitExceeded(respendCount, lastRespendTime, limit, size))
        {
            LOG(RESPEND, "respend: Double-spend relay rejected by rate limiter\n");
            return true;
        }

        LOG(RESPEND, "respend: Double-spend relay rate limiter: %g => %g\n", respendCount, respendCount + size);
        return false;
    }

private:
    double respendCount;
    int64_t lastRespendTime;
    std::mutex cs_relayLimiter;
};

} // ns anon

RespendRelayer::RespendRelayer() : interesting(false), valid(false) {}
bool RespendRelayer::AddOutpointConflict(const COutPoint &,
    const uint256 hash,
    const CTransactionRef pRespendTx,
    bool seenBefore,
    bool isEquivalent)
{
    if (seenBefore || isEquivalent)
        return true; // look at more outpoints

    // Is static to hold relay statistics
    static RelayLimiter limiter;

    if (limiter.HasLimitExceeded(pRespendTx))
    {
        // we won't relay this tx, so no no need to look at more outputs.
        return false;
    }

    spendhash = hash;
    pRespend = pRespendTx;
    interesting = true;
    return false;
}

bool RespendRelayer::IsInteresting() const { return interesting; }
void RespendRelayer::SetValid(bool v) { valid = v; }
void RespendRelayer::Trigger(CTxMemPool &pool)
{
    if (!valid || !interesting)
        return;

    if (!doubleSpendProofs.Value())
        return;

    CTransactionRef ptx;
    DoubleSpendProof dsp;

    // no DS proof exists, lets make one.
    {
        WRITELOCK(pool.cs_txmempool);
        auto originalTxIter = pool.mapTx.find(spendhash);
        if (originalTxIter == pool.mapTx.end())
            return; // if original tx is no longer in mempool then there is nothing to do.

        if (originalTxIter->dsproof == -1)
        {
            try
            {
                auto item = *originalTxIter;
                dsp = DoubleSpendProof::create(originalTxIter->GetTx(), *pRespend);
                item.dsproof = pool.doubleSpendProofStorage()->add(dsp).second;
                LOG(DSPROOF, "Double spend found, creating double spend proof %d\n", item.dsproof);
                pool.mapTx.replace(originalTxIter, item);

                ptx = pool._get(originalTxIter->GetTx().GetHash());
            }
            catch (const std::exception &e)
            {
                LOG(DSPROOF, "Double spend creation failed: %s\n", e.what());
            }
        }
    }

    // send INV to all peers
    if (ptx != nullptr && !dsp.isEmpty())
        broadcastDspInv(ptx, dsp.GetHash());
}

} // ns respend
