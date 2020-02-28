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
    if (GetArg("-preferential-timer", DEFAULT_PREFERENTIAL_TIMER) == 0)
        return false;

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

bool ThinTypeRelay::AreTooManyBlocksInFlight()
{
    // check if we've exceed the max thintype blocks in flight allowed.
    LOCK(cs_inflight);
    size_t mapSize = 0;
    for (auto &entry : mapThinTypeBlocksInFlight)
    {
        // add the size of the sets of each entry
        // it is possible for a set to be empty
        mapSize = mapSize + entry.second.size();
    }
    return (mapSize >= MAX_THINTYPE_BLOCKS_IN_FLIGHT);
}

bool ThinTypeRelay::IsBlockInFlight(CNode *pfrom, const std::string thinType, const uint256 &hash)
{
    // check if this node already has this thinType of block in flight.
    LOCK(cs_inflight);
    auto key = mapThinTypeBlocksInFlight.find(pfrom->GetId());
    if (key != mapThinTypeBlocksInFlight.end())
    {
        // time and recieved arent checked in the comparator, so they dont matter here
        return key->second.count(CThinTypeBlockInFlight{hash, 0, false, thinType});
    }
    return false;
}

void ThinTypeRelay::BlockWasReceived(CNode *pfrom, const uint256 &hash)
{
    LOCK(cs_inflight);
    auto key = mapThinTypeBlocksInFlight.find(pfrom->GetId());
    if (key != mapThinTypeBlocksInFlight.end())
    {
        // elements in a set are immutable. they can be added/removed but not edited
        // inserting/emplacing new elements in a set while iterating through a set is safe behavior
        for (auto entry = key->second.begin(); entry != key->second.end();)
        {
            // our sets uniqueness is based on hash + thinType so just checking the hash
            // does not guaranteed that all entries with that hash are marked as received
            if (entry->hash == hash && entry->fReceived == false)
            {
                CThinTypeBlockInFlight updatedEntry = *entry;
                updatedEntry.fReceived = true;
                // we have to erase before emplacing to comply with comparator uniqueness
                // erase never throws exceptions
                entry = key->second.erase(entry);
                key->second.emplace(updatedEntry);
                // intended thin type block relay behavior should clear failed entries when making
                // a failover request so there should only ever be 1 entry in the set with any
                // given block hash across all thinType
                // we do not break here to prevent a disconnect from a peer in the event
                // that we did not properly clean up entries when a failover request was made.
            }
            else
            {
                ++entry;
            }
        }
    }
}

bool ThinTypeRelay::AddBlockInFlight(CNode *pfrom, const uint256 &hash, const std::string thinType)
{
    LOCK(cs_inflight);
    if (AreTooManyBlocksInFlight())
        return false;

    // this insert returns a pair <iterator,bool> where the bool denotes whether the insertion took place
    auto key = mapThinTypeBlocksInFlight.find(pfrom->GetId());
    if (key != mapThinTypeBlocksInFlight.end())
    {
        return key->second.emplace(CThinTypeBlockInFlight{hash, GetTime(), false, thinType}).second;
    }
    auto result = mapThinTypeBlocksInFlight.emplace(pfrom->GetId(), std::set<CThinTypeBlockInFlight>());
    // strong guarantee, if emplace fails there are no changes made to the map
    if (result.second == false)
    {
        return false;
    }
    return result.first->second.emplace(CThinTypeBlockInFlight{hash, GetTime(), false, thinType}).second;
}

void ThinTypeRelay::ClearBlockInFlight(NodeId id, const uint256 &hash)
{
    LOCK(cs_inflight);
    auto key = mapThinTypeBlocksInFlight.find(id);
    if (key != mapThinTypeBlocksInFlight.end())
    {
        for (auto entry = key->second.begin(); entry != key->second.end();)
        {
            if (entry->hash == hash)
            {
                // it is safe to erase elements while iterating through sets since c++14
                entry = key->second.erase(entry);
                // set entry uniqueness is based on hash + thinType so dont break here to make sure
                // that all entries with this block hash regardless of thinType are cleared
            }
            else
            {
                ++entry;
            }
        }
    }
}

void ThinTypeRelay::ClearAllBlocksInFlight(NodeId id)
{
    LOCK(cs_inflight);
    auto key = mapThinTypeBlocksInFlight.find(id);
    if (key != mapThinTypeBlocksInFlight.end())
    {
        key->second.clear();
    }
}

void ThinTypeRelay::SetSentGrapheneBlocks(NodeId id, CGrapheneBlock &grapheneBlock)
{
    LOCK(cs_graphene_sender);
    mapGrapheneSentBlocks[id] = std::make_shared<CGrapheneBlock>(grapheneBlock);
}

std::shared_ptr<CGrapheneBlock> ThinTypeRelay::GetSentGrapheneBlocks(NodeId id)
{
    LOCK(cs_graphene_sender);

    auto it = mapGrapheneSentBlocks.find(id);
    if (it != mapGrapheneSentBlocks.end())
        return it->second;
    else
        return std::shared_ptr<CGrapheneBlock>();
}

void ThinTypeRelay::ClearSentGrapheneBlocks(NodeId id)
{
    LOCK(cs_graphene_sender);
    mapGrapheneSentBlocks.erase(id);
}

void ThinTypeRelay::CheckForDownloadTimeout(CNode *pfrom)
{
    LOCK(cs_inflight);
    auto key = mapThinTypeBlocksInFlight.find(pfrom->GetId());
    if (key != mapThinTypeBlocksInFlight.end())
    {
        for (auto &entry : (*key).second)
        {
            // Use a timeout of 6 times the retry inverval before disconnecting.  This way only a max of 6
            // re-requested thinblocks or graphene blocks could be in memory at any one time.
            if (!entry.fReceived &&
                (GetTime() - entry.nRequestTime) > (int)MAX_THINTYPE_BLOCKS_IN_FLIGHT * blkReqRetryInterval / 1000000)
            {
                if (!pfrom->fWhitelisted && Params().NetworkIDString() != "regtest")
                {
                    LOG(THIN | GRAPHENE | CMPCT,
                        "ERROR: Disconnecting peer %s due to thinblock download timeout exceeded (%d secs)\n",
                        pfrom->GetLogName(), (GetTime() - entry.nRequestTime));
                    pfrom->fDisconnect = true;
                    return;
                }
            }
        }
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
    std::shared_ptr<CBlockThinRelay> existing_entry = GetBlockToReconstruct(pfrom, hash);
    if (existing_entry != nullptr)
    {
        return existing_entry;
    }
    // Otherwise, start with a fresh instance.
    // Store and empty block which can be used later
    std::shared_ptr<CBlockThinRelay> pblock;
    pblock = std::make_shared<CBlockThinRelay>(CBlockThinRelay());

    // Initialize the thintype pointers
    pblock->thinblock = std::make_shared<CThinBlock>(CThinBlock());
    pblock->xthinblock = std::make_shared<CXThinBlock>(CXThinBlock());
    pblock->cmpctblock = std::make_shared<CompactBlock>(CompactBlock());
    pblock->grapheneblock = std::make_shared<CGrapheneBlock>(CGrapheneBlock());
    // unless we run out of memory, emplace should never fail
    auto newKey = mapBlocksReconstruct.emplace(pfrom->GetId(), std::map<uint256, std::shared_ptr<CBlockThinRelay> >());
    newKey.first->second.emplace(hash, pblock);
    return pblock;
}

std::shared_ptr<CBlockThinRelay> ThinTypeRelay::GetBlockToReconstruct(CNode *pfrom, const uint256 &hash)
{
    // Retrieve a current instance of a block being reconstructed. This is typically used
    // when we have received the response of a re-request for more transactions.
    LOCK(cs_reconstruct);
    auto key_node = mapBlocksReconstruct.find(pfrom->GetId());
    if (key_node != mapBlocksReconstruct.end())
    {
        auto key_hash = key_node->second.find(hash);
        if (key_hash != key_node->second.end())
        {
            return key_hash->second;
        }
    }
    return nullptr;
}

void ThinTypeRelay::ClearBlockToReconstruct(NodeId id, const uint256 &hash)
{
    LOCK(cs_reconstruct);
    auto key = mapBlocksReconstruct.find(id);
    if (key != mapBlocksReconstruct.end())
    {
        key->second.erase(hash);
    }
}

void ThinTypeRelay::ClearAllBlocksToReconstruct(NodeId id)
{
    LOCK(cs_reconstruct);
    auto key = mapBlocksReconstruct.find(id);
    if (key != mapBlocksReconstruct.end())
    {
        // we could just erase the entire id key in the outer map, but then we would have to reallocate
        // space for that node in the event we get another block from them.
        // only clearing the inner map will take more memory but less cpu time
        key->second.clear();
    }
}

void ThinTypeRelay::AddBlockBytes(uint64_t bytes, std::shared_ptr<CBlockThinRelay> pblock)
{
    pblock->nCurrentBlockSize += bytes;
}

uint64_t ThinTypeRelay::GetMaxAllowedBlockSize() { return maxMessageSizeMultiplier * excessiveBlockSize; }
void ThinTypeRelay::ClearAllBlockData(CNode *pnode, const uint256 &hash)
{
    // Clear the entries for block to reconstruct and block in flight
    ClearBlockToReconstruct(pnode->GetId(), hash);
    ClearBlockInFlight(pnode->GetId(), hash);
}
