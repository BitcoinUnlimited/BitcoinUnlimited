// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/blockrelay_common.h"
#include "blockrelay/graphene.h"
#include "net.h"
#include "random.h"
#include "requestManager.h"
#include "sync.h"
#include "util.h"

// When a node disconnects it may not be removed from the peer tracking sets immediately and so the size
// of those sets could temporarily rise above the maxiumum number of connections.  This padding prevents
// us from asserting in debug mode when a node or group of nodes drops off suddenly while another set
// of nodes is connecting.
static unsigned int NODE_PADDING = 5;

bool IsThinBlockEnabled();
bool IsGrapheneBlockEnabled();
bool IsCompactBlocksEnabled();

// Update the counters for how many peers we have connected.
void ThinTypeRelay::AddPeers(CNode *pfrom)
{
    LOCK(cs_addpeers);

    // Don't allow the set sizes to grow unbounded.  They should never be greater
    // than the number of peers connected.  If this should happen we'll just stop
    // adding them and return, but if running a debug build we'll assert.
    uint32_t nNodes = nMaxConnections + NODE_PADDING;
    DbgAssert(setThinBlockPeers.size() <= nNodes, return );
    DbgAssert(setGraphenePeers.size() <= nNodes, return );
    if (setThinBlockPeers.size() > nNodes || setGraphenePeers.size() > nNodes)
        return;

    // Update the counters
    if (pfrom)
    {
        if (pfrom->nServices & NODE_XTHIN)
            setThinBlockPeers.insert(pfrom->GetId());
        if (pfrom->nServices & NODE_GRAPHENE)
            setGraphenePeers.insert(pfrom->GetId());
    }
    nThinBlockPeers = setThinBlockPeers.size();
    nGraphenePeers = setGraphenePeers.size();
}
void ThinTypeRelay::AddCompactBlockPeer(CNode *pfrom)
{
    LOCK(cs_addpeers);

    // Don't allow the set sizes to grow unbounded.  They should never be greater
    // than the number of peers connected.  If this should happen we'll just stop
    // adding them and return, but if running a debug build we'll assert.
    uint32_t nNodes = nMaxConnections + NODE_PADDING;
    DbgAssert(setCompactBlockPeers.size() <= nNodes, return );
    if (setCompactBlockPeers.size() > nNodes)
        return;

    if (pfrom && pfrom->fSupportsCompactBlocks)
    {
        setCompactBlockPeers.insert(pfrom->GetId());
    }
    nCompactBlockPeers = setCompactBlockPeers.size();
}
void ThinTypeRelay::RemovePeers(CNode *pfrom)
{
    LOCK(cs_addpeers);
    if (pfrom)
    {
        if (pfrom->nServices & NODE_XTHIN)
            setThinBlockPeers.erase(pfrom->GetId());
        if (pfrom->nServices & NODE_GRAPHENE)
            setGraphenePeers.erase(pfrom->GetId());
        if (pfrom->fSupportsCompactBlocks)
            setCompactBlockPeers.erase(pfrom->GetId());
    }
    nThinBlockPeers = setThinBlockPeers.size();
    nGraphenePeers = setGraphenePeers.size();
    nCompactBlockPeers = setCompactBlockPeers.size();
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

    if (!IsBlockRelayTimerEnabled())
        return true;

    LOCK(cs_blockrelaytimer);
    if (!mapBlockRelayTimer.count(hash))
    {
        // The timeout limit is a random number +/- 20%.
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
        LOG(THIN | GRAPHENE | CMPCT, "Starting Preferential Block Relay timer (%d millis)\n", nTimeToWait + nOffset);
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
                // Only print out the log entry once.  Because the thinblock timer will be hit
                // many times when requesting a block we don't want to fill up the log file.
                if (!iter->second.second)
                {
                    iter->second.second = true;
                    LOG(THIN | GRAPHENE | CMPCT,
                        "Preferential BlockRelay timer exceeded - downloading regular block instead\n");
                }
                return true;
            }
        }
    }
    return false;
}

bool ThinTypeRelay::IsBlockRelayTimerEnabled()
{
    // Only engage the timer if one or more, but not all, thin type relays are active.
    // If all types are active, or all inactive, then we do not need the timer.
    // Generally speaking all types will be active and we can return early.
    if (IsThinBlocksEnabled() && IsGrapheneBlockEnabled() && IsCompactBlocksEnabled())
        return false;
    if (!IsThinBlocksEnabled() && !IsGrapheneBlockEnabled() && !IsCompactBlocksEnabled())
        return false;

    // The thin relay timer is only relevant if we have a specific thin relay type active
    // AND we have peers connected which also support that thin relay type
    bool fThinBlockPossible = IsThinBlocksEnabled() && nThinBlockPeers > 0;
    bool fGraphenePossible = IsGrapheneBlockEnabled() && nGraphenePeers > 0;
    bool fCompactBlockPossible = IsCompactBlocksEnabled() && nCompactBlockPeers > 0;

    return fThinBlockPossible || fGraphenePossible || fCompactBlockPossible;
}
// The timer is cleared as soon as we request a block or thinblock.
void ThinTypeRelay::ClearBlockRelayTimer(const uint256 &hash)
{
    LOCK(cs_blockrelaytimer);
    if (mapBlockRelayTimer.count(hash))
    {
        mapBlockRelayTimer.erase(hash);
        LOG(THIN | GRAPHENE | CMPCT, "Clearing Preferential BlockRelay timer\n");
    }
}

bool ThinTypeRelay::AreTooManyBlocksInFlight(CNode *pfrom, const std::string thinType)
{
    // check if we've exceed the max thintype blocks in flight allowed.
    LOCK(cs_inflight);
    if (mapThinTypeBlocksInFlight.size() >= MAX_THINTYPE_BLOCKS_IN_FLIGHT)
        return true;
    return false;
}

bool ThinTypeRelay::IsBlockInFlight(CNode *pfrom, const std::string thinType, const uint256 &hash)
{
    // check if this node already has this thinType of block in flight.
    auto range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
    while (range.first != range.second)
    {
        if (range.first->second.thinType == thinType && range.first->second.hash == hash)
            return true;

        range.first++;
    }
    return false;
}

unsigned int ThinTypeRelay::TotalBlocksInFlight()
{
    LOCK(cs_inflight);
    return mapThinTypeBlocksInFlight.size();
}

void ThinTypeRelay::BlockWasReceived(CNode *pfrom, const uint256 &hash)
{
    LOCK(cs_inflight);
    auto range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
    while (range.first != range.second)
    {
        if (range.first->second.hash == hash)
            range.first->second.fReceived = true;

        range.first++;
    }
}

bool ThinTypeRelay::AddBlockInFlight(CNode *pfrom, const uint256 &hash, const std::string thinType)
{
    LOCK(cs_inflight);
    if (AreTooManyBlocksInFlight(pfrom, thinType))
        return false;

    mapThinTypeBlocksInFlight.insert(
        std::pair<const NodeId, CThinTypeBlockInFlight>(pfrom->GetId(), {hash, GetTime(), false, thinType}));

    return true;
}

void ThinTypeRelay::ClearBlockInFlight(CNode *pfrom, const uint256 &hash)
{
    LOCK(cs_inflight);
    auto range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
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

void ThinTypeRelay::ClearAllBlocksInFlight(NodeId id)
{
    LOCK(cs_inflight);
    auto range = mapThinTypeBlocksInFlight.equal_range(id);
    mapThinTypeBlocksInFlight.erase(range.first, range.second);
}

void ThinTypeRelay::CheckForDownloadTimeout(CNode *pfrom)
{
    LOCK(cs_inflight);
    if (mapThinTypeBlocksInFlight.size() == 0)
        return;

    auto range = mapThinTypeBlocksInFlight.equal_range(pfrom->GetId());
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
                LOG(THIN | GRAPHENE | CMPCT,
                    "ERROR: Disconnecting peer %s due to thinblock download timeout exceeded (%d secs)\n",
                    pfrom->GetLogName(), (GetTime() - range.first->second.nRequestTime));
                pfrom->fDisconnect = true;
                return;
            }
        }

        range.first++;
    }
}

void ThinTypeRelay::RequestBlock(CNode *pfrom, const uint256 &hash)
{
    std::vector<CInv> vGetData;
    vGetData.push_back(CInv(MSG_BLOCK, hash));
    pfrom->PushMessage(NetMsgType::GETDATA, vGetData);
}

std::shared_ptr<CBlockThinRelay> ThinTypeRelay::SetBlockToReconstruct(CNode *pfrom, const uint256 &hash)
{
    LOCK(cs_reconstruct);
    // If another thread has already created an instance then return it.
    // Currently we can only have one block hash in flight per node so make sure it's the same hash.
    auto iter = mapBlocksReconstruct.find(pfrom->GetId());
    if (iter != mapBlocksReconstruct.end() && iter->second.first == hash)
    {
        return iter->second.second;
    }
    // Otherwise, start with a fresh instance.
    else
    {
        // Store and empty block which can be used later
        std::shared_ptr<CBlockThinRelay> pblock;
        pblock = std::make_shared<CBlockThinRelay>(CBlockThinRelay());

        // Initialize the thintype pointers
        pblock->thinblock = std::make_shared<CThinBlock>(CThinBlock());
        pblock->xthinblock = std::make_shared<CXThinBlock>(CXThinBlock());
        pblock->cmpctblock = std::make_shared<CompactBlock>(CompactBlock());
        pblock->grapheneblock = std::make_shared<CGrapheneBlock>(CGrapheneBlock());

        mapBlocksReconstruct.insert(std::make_pair(pfrom->GetId(), std::make_pair(hash, pblock)));
        return pblock;
    }
}

std::shared_ptr<CBlockThinRelay> ThinTypeRelay::GetBlockToReconstruct(CNode *pfrom, const uint256 &hash)
{
    // Retrieve a current instance of a block being reconstructed. This is typically used
    // when we have received the response of a re-request for more transactions.
    LOCK(cs_reconstruct);
    auto range = mapBlocksReconstruct.equal_range(pfrom->GetId());
    while (range.first != range.second)
    {
        if (range.first->second.first == hash)
        {
            return range.first->second.second;
        }
        range.first++;
    }
    return nullptr;
}

void ThinTypeRelay::ClearBlockToReconstruct(NodeId id, const uint256 &hash)
{
    LOCK(cs_reconstruct);
    auto range = mapBlocksReconstruct.equal_range(id);
    while (range.first != range.second)
    {
        if (range.first->second.first == hash)
        {
            mapBlocksReconstruct.erase(range.first);
            break;
        }
        range.first++;
    }
}

void ThinTypeRelay::ClearAllBlocksToReconstruct(NodeId id)
{
    LOCK(cs_reconstruct);
    auto range = mapBlocksReconstruct.equal_range(id);
    mapBlocksReconstruct.erase(range.first, range.second);
}

void ThinTypeRelay::AddBlockBytes(uint64_t bytes, std::shared_ptr<CBlockThinRelay> pblock)
{
    pblock->nCurrentBlockSize += bytes;
}

uint64_t ThinTypeRelay::GetMaxAllowedBlockSize() { return maxMessageSizeMultiplier * excessiveBlockSize; }
void ThinTypeRelay::ClearAllBlockData(CNode *pnode, std::shared_ptr<CBlockThinRelay> pblock)
{
    // We must make sure to clear the block data first before clearing the thinblock in flight.
    uint256 hash = pblock->GetBlockHeader().GetHash();
    ClearBlockToReconstruct(pnode->GetId(), hash);

    // Clear block data
    if (pblock)
        pblock->SetNull();

    // Now clear the block in flight.
    ClearBlockInFlight(pnode, hash);
}
