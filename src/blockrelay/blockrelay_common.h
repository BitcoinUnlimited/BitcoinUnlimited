// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCKRELAY_COMMON_H
#define BITCOIN_BLOCKRELAY_COMMON_H

#include "net.h"
#include "utiltime.h"

#include <set>
#include <stdint.h>

class CNode;
class uint256;
class CBlockThinRelay;

struct CThinTypeBlockInFlight
{
    uint256 hash;
    int64_t nRequestTime;
    bool fReceived;
    const std::string thinType;
    bool operator==(const CThinTypeBlockInFlight &b) { return ((hash == b.hash) && (thinType == b.thinType)); }
    friend bool operator<(const CThinTypeBlockInFlight &a, const CThinTypeBlockInFlight &b)
    {
        if (a.hash == b.hash)
        {
            return (a.thinType < b.thinType);
        }
        return (a.hash < b.hash);
    }
};


class ThinTypeRelay
{
public:
    CCriticalSection cs_inflight;
    CCriticalSection cs_reconstruct;
    CCriticalSection cs_graphene_sender;

    // put a cap on the total number of thin type blocks we can have in flight. This lowers any possible
    // attack surface.
    size_t MAX_THINTYPE_BLOCKS_IN_FLIGHT = 6;

private:
    // block relay timer
    CCriticalSection cs_blockrelaytimer;
    std::map<uint256, std::pair<uint64_t, bool> > mapBlockRelayTimer GUARDED_BY(cs_blockrelaytimer);

    // thin type blocks in flight and the time they were requested.
    std::map<const NodeId, std::set<CThinTypeBlockInFlight> > mapThinTypeBlocksInFlight GUARDED_BY(cs_inflight);

    // blocks that are currently being reconstructed.
    std::map<NodeId, std::map<uint256, std::shared_ptr<CBlockThinRelay> > > mapBlocksReconstruct GUARDED_BY(
        cs_reconstruct);

    // Counters for how many of each peer are currently connected.  We use the set to store the
    // nodeid so that we can then get a unique count of peers with with to update the atomic counters.
    CCriticalSection cs_addpeers;
    std::set<NodeId> setThinBlockPeers;
    std::set<NodeId> setGraphenePeers;
    std::set<NodeId> setCompactBlockPeers;
    std::atomic<int32_t> nThinBlockPeers{0};
    std::atomic<int32_t> nGraphenePeers{0};
    std::atomic<int32_t> nCompactBlockPeers{0};

    // blocks still in flight sent by the sender.
    std::map<NodeId, std::shared_ptr<CGrapheneBlock> > mapGrapheneSentBlocks GUARDED_BY(cs_graphene_sender);

public:
    void AddPeers(CNode *pfrom);
    uint32_t GetGraphenePeers() { return nGraphenePeers.load(); }
    uint32_t GetThinBlockPeers() { return nThinBlockPeers.load(); }
    uint32_t GetCompactBlockPeers() { return nCompactBlockPeers.load(); }
    void AddCompactBlockPeer(CNode *pfrom);
    void RemovePeers(CNode *pfrom);
    bool HasBlockRelayTimerExpired(const uint256 &hash);
    bool IsBlockRelayTimerEnabled();
    void ClearBlockRelayTimer(const uint256 &hash);
    bool AreTooManyBlocksInFlight();
    bool IsBlockInFlight(CNode *pfrom, const std::string thinType, const uint256 &hash);
    void BlockWasReceived(CNode *pfrom, const uint256 &hash);
    bool AddBlockInFlight(CNode *pfrom, const uint256 &hash, const std::string thinType);
    void ClearBlockInFlight(NodeId id, const uint256 &hash);
    void ClearAllBlocksInFlight(NodeId id);
    void SetSentGrapheneBlocks(NodeId id, CGrapheneBlock &grapheneBlock);
    std::shared_ptr<CGrapheneBlock> GetSentGrapheneBlocks(NodeId id);
    void ClearSentGrapheneBlocks(NodeId id);
    void CheckForDownloadTimeout(CNode *pfrom);
    void RequestBlock(CNode *pfrom, const uint256 &hash);

    // Accessor methods to the blocks that we're reconstructing from thintype blocks such as
    // xthins or graphene.
    std::shared_ptr<CBlockThinRelay> SetBlockToReconstruct(CNode *pfrom, const uint256 &hash);
    std::shared_ptr<CBlockThinRelay> GetBlockToReconstruct(CNode *pfrom, const uint256 &hash);
    void ClearBlockToReconstruct(NodeId id, const uint256 &hash);
    void ClearAllBlocksToReconstruct(NodeId id);

    // Accessor methods for tracking total block bytes for all blocks currently in the process
    // of being reconstructed.
    void AddBlockBytes(uint64_t bytes, std::shared_ptr<CBlockThinRelay> pblock);
    uint64_t GetMaxAllowedBlockSize();

    // Clear all block data
    void ClearAllBlockData(CNode *pnode, const uint256 &hash);
};
extern ThinTypeRelay thinrelay;

#endif // BITCOIN_BLOCKRELAY_COMMON_H
