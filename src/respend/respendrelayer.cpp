// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "respend/respendrelayer.h"
#include "net.h" // RelayTransaction
#include "primitives/transaction.h"
#include "protocol.h"
#include "streams.h"
#include "util.h"
#include <mutex>

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
    bool HasLimitExceeded(const CTransaction &doubleSpend)
    {
        unsigned int size = ::GetSerializeSize(doubleSpend, SER_NETWORK, PROTOCOL_VERSION);

        std::lock_guard<std::mutex> lock(cs);
        int64_t limit = GetArg("-limitrespendrelay", 100);
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
    std::mutex cs;
};

} // ns anon

RespendRelayer::RespendRelayer() : interesting(false), valid(false) {}
bool RespendRelayer::AddOutpointConflict(const COutPoint &,
    const CTxMemPool::txiter,
    const CTransaction &respendTx,
    bool seenBefore,
    bool isEquivalent)
{
    if (seenBefore || isEquivalent)
        return true; // look at more outpoints

    // Is static to hold relay statistics
    static RelayLimiter limiter;

    if (limiter.HasLimitExceeded(respendTx))
    {
        // we won't relay this tx, so no no need to look at more outputs.
        return false;
    }

    respend = respendTx;
    interesting = true;
    return false;
}

bool RespendRelayer::IsInteresting() const { return interesting; }
void RespendRelayer::SetValid(bool v) { valid = v; }
void RespendRelayer::Trigger()
{
    if (!valid || !interesting)
        return;

    RelayTransaction(respend);
}

} // ns respend
