// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/blockrelay_common.h"
#include "net.h"
#include "random.h"
#include "requestManager.h"
#include "sync.h"
#include "util.h"

bool IsThinBlockEnabled();
bool IsGrapheneBlockEnabled();

// Update the counters for how many peers we have connected.
void ThinTypeRelay::AddThinTypePeers(CNode *pfrom)
{
    if (pfrom)
    {
        if (pfrom->nServices & NODE_XTHIN)
            nThinBlockPeers++;
        if (pfrom->nServices & NODE_GRAPHENE)
            nGraphenePeers++;
    }
}
void ThinTypeRelay::AddCompactBlockPeer(CNode *pfrom)
{
    if (pfrom && pfrom->fSupportsCompactBlocks)
        nCompactBlockPeers++;
}
void ThinTypeRelay::RemoveThinTypePeers(CNode *pfrom)
{
    if (pfrom)
    {
        if (pfrom->nServices & NODE_XTHIN)
            nThinBlockPeers--;
        if (pfrom->nServices & NODE_GRAPHENE)
            nGraphenePeers--;
        if (pfrom->fSupportsCompactBlocks)
            nCompactBlockPeers--;

        DbgAssert(nThinBlockPeers >= 0, nThinBlockPeers = 0);
        DbgAssert(nGraphenePeers >= 0, nGraphenePeers = 0);
        DbgAssert(nCompactBlockPeers >= 0, nCompactBlockPeers = 0);
    }
}

// Preferential Block Relay Timer:
// The purpose of the timer is to ensure that we more often download an XTHIN/GRAPHENE/CMPCT blocks
// rather than full blocks.  Once a block announcement arrives the timer is started.  If there are no
// peers that support one of the thin blocks types then timer continues until either an announcement
// arrives from a comaptible peer, or the timer expires. If the timer expires, then and only then we
// download a full block.
bool ThinTypeRelay::HasBlockRelayTimerExpired(const uint256 &hash)
{
    // Base time used to calculate the random timeout value.
    static uint64_t nTimeToWait = GetArg("-preferential-timer", DEFAULT_PREFERENTIAL_TIMER);
    if (nTimeToWait == 0)
        return true;

    LOCK(cs_blockrelaytimer);
    if (!mapBlockRelayTimer.count(hash))
    {
        // The timeout limit is a random number belonging to graphene-timer +/- 20%
        // This way a node connected to this one may download the block
        // before the other node and thus be able to serve the other with
        // a graphene block, rather than both nodes timing out and downloading
        // a thinblock instead. This can happen at the margins of the BU network
        // where we receive full blocks from peers that don't support graphene.
        //
        // To make the timeout random we adjust the start time of the timer forward
        // or backward by a random amount plus or minus 20% of preferential timer in milliseconds.
        FastRandomContext insecure_rand(false);
        uint64_t nStartInterval = nTimeToWait * 0.8;
        uint64_t nIntervalLen = 2 * (nTimeToWait * 0.2);
        int64_t nOffset = nTimeToWait - (nStartInterval + (insecure_rand.rand64() % nIntervalLen) + 1);
        mapBlockRelayTimer.emplace(hash, std::make_pair(GetTimeMillis() + nOffset, false));
        LOG(GRAPHENE, "Starting Preferential block relay timer (%d millis)\n", nTimeToWait + nOffset);
    }
    else
    {
        // Check that we have not exceeded time limit.
        // If we have then we want to return false so that we can
        // proceed to download a regular block instead.
        auto iter = mapBlockRelayTimer.find(hash);
        if (iter != mapBlockRelayTimer.end())
        {
            int64_t elapsed = GetTimeMillis() - iter->second.first;
            if (elapsed > (int64_t)nTimeToWait)
            {
                // Only print out the log entry once.  Because the graphene timer will be hit
                // many times when requesting a block we don't want to fill up the log file.
                if (!iter->second.second)
                {
                    iter->second.second = true;
                    LOG(GRAPHENE | THIN, "Preferential block relay timer exceeded\n");
                }
                return true;
            }
        }
    }
    return false;
}

bool ThinTypeRelay::IsBlockRelayTimerEnabled()
{
    // Only engage the timer if at least one thin type relay is active.
    if (!IsThinBlocksEnabled() && !IsGrapheneBlockEnabled())
        return false;

    // Under certain conditions the thin relay timer is not relevant.
    bool fHaveThinBlockPeers = false;
    bool fHaveGraphenePeers = false;
    if (nThinBlockPeers > 0)
        fHaveThinBlockPeers = true;
    if (nGraphenePeers > 0)
        fHaveGraphenePeers = true;

    if (!fHaveGraphenePeers && !fHaveThinBlockPeers)
        return false;
    else if (IsGrapheneBlockEnabled() && !fHaveGraphenePeers && !IsThinBlocksEnabled() && fHaveThinBlockPeers)
        return false;
    else if (!IsGrapheneBlockEnabled() && fHaveGraphenePeers && IsThinBlocksEnabled() && !fHaveThinBlockPeers)
        return false;

    return true;
}
// The timer is cleared as soon as we request a block or thinblock.
void ThinTypeRelay::ClearBlockRelayTimer(const uint256 &hash)
{
    LOCK(cs_blockrelaytimer);
    if (mapBlockRelayTimer.count(hash))
    {
        mapBlockRelayTimer.erase(hash);
        LOG(THIN | GRAPHENE, "Clearing Preferential BlockRelay timer\n");
    }
}

bool ThinTypeRelay::IsThinTypeBlockInFlight(CNode *pfrom, const std::string thinType)
{
    LOCK(cs_inflight);
    // first check that we are in bounds.
    if (mapThinTypeBlocksInFlight.size() >= MAX_THINTYPE_BLOCKS_IN_FLIGHT)
        return true;

    // check if this node already has this thinType of block in flight.
    std::pair<std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator,
        std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator>
        range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
    while (range.first != range.second)
    {
        if (range.first->second.thinType == thinType)
            return true;

        range.first++;
    }
    return false;
}

unsigned int ThinTypeRelay::TotalThinTypeBlocksInFlight()
{
    LOCK(cs_inflight);
    return mapThinTypeBlocksInFlight.size();
}

void ThinTypeRelay::ThinTypeBlockWasReceived(CNode *pfrom, const uint256 &hash)
{
    LOCK(cs_inflight);
    std::pair<std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator,
        std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator>
        range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
    while (range.first != range.second)
    {
        if (range.first->second.hash == hash)
            range.first->second.fReceived = true;

        range.first++;
    }
}

bool ThinTypeRelay::AddThinTypeBlockInFlight(CNode *pfrom, const uint256 &hash, const std::string thinType)
{
    LOCK(cs_inflight);
    if (IsThinTypeBlockInFlight(pfrom, thinType))
        return false;

    mapThinTypeBlocksInFlight.insert(
        std::pair<const NodeId, CThinTypeBlockInFlight>(pfrom->GetId(), {hash, GetTime(), false, thinType}));
    return true;
}

void ThinTypeRelay::ClearThinTypeBlockInFlight(CNode *pfrom, const uint256 &hash)
{
    LOCK(cs_inflight);
    std::pair<std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator,
        std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator>
        range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
    while (range.first != range.second)
    {
        if (range.first->second.hash == hash)
        {
            range.first = mapThinTypeBlocksInFlight.erase(range.first);
        }
        else
        {
            range.first++;
        }
    }
}

void ThinTypeRelay::CheckForThinTypeDownloadTimeout(CNode *pfrom)
{
    LOCK(cs_inflight);
    if (mapThinTypeBlocksInFlight.size() == 0)
        return;

    std::pair<std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator,
        std::multimap<const NodeId, CThinTypeBlockInFlight>::iterator>
        range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
    while (range.first != range.second)
    {
        // Use a timeout of 6 times the retry inverval before disconnecting.  This way only a max of 6
        // re-requested thinblocks or graphene blocks could be in memory at any one time.
        if (!range.first->second.fReceived &&
            (GetTime() - range.first->second.nRequestTime) >
                (int)MAX_THINTYPE_BLOCKS_IN_FLIGHT * blkReqRetryInterval / 1000000)
        {
            if (!pfrom->fWhitelisted && Params().NetworkIDString() != "regtest")
            {
                LOG(THIN | GRAPHENE,
                    "ERROR: Disconnecting peer %s due to thinblock download timeout exceeded (%d secs)\n",
                    pfrom->GetLogName(), (GetTime() - range.first->second.nRequestTime));
                pfrom->fDisconnect = true;
                return;
            }
        }

        range.first++;
    }
}
