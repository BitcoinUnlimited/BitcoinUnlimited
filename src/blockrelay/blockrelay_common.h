// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCKRELAY_COMMON_H
#define BITCOIN_BLOCKRELAY_COMMON_H

#include "net.h"
#include "utiltime.h"

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
};

class ThinTypeRelay
{
public:
    CCriticalSection cs_inflight;
    CCriticalSection cs_reconstruct;

private:
    /* The sum total of all bytes for thintype blocks currently in process of being reconstructed */
    std::atomic<uint64_t> nTotalBlockBytes{0};

    // block relay timer
    CCriticalSection cs_blockrelaytimer;
    std::map<uint256, std::pair<uint64_t, bool> > mapBlockRelayTimer GUARDED_BY(cs_blockrelaytimer);

    // thin type blocks in flight and the time they were requested.
    std::multimap<const NodeId, CThinTypeBlockInFlight> mapThinTypeBlocksInFlight GUARDED_BY(cs_inflight);

    // blocks that are currently being reconstructed.
    std::map<NodeId, std::pair<uint256, std::shared_ptr<CBlockThinRelay> > > mapBlocksReconstruct GUARDED_BY(
        cs_reconstruct);

    // put a cap on the total number of thin type blocks we can have in flight. This lowers any possible
    // attack surface.
    size_t MAX_THINTYPE_BLOCKS_IN_FLIGHT = 6;

    // Counters for how many of each peer are currently connected.
    std::atomic<int32_t> nThinBlockPeers{0};
    std::atomic<int32_t> nGraphenePeers{0};
    std::atomic<int32_t> nCompactBlockPeers{0};

public:
    void AddPeers(CNode *pfrom);
    void AddCompactBlockPeer(CNode *pfrom);
    void RemovePeers(CNode *pfrom);
    bool HasBlockRelayTimerExpired(const uint256 &hash);
    bool IsBlockRelayTimerEnabled();
    void ClearBlockRelayTimer(const uint256 &hash);
    bool IsBlockInFlight(CNode *pfrom, const std::string thinType);
    unsigned int TotalBlocksInFlight();
    void BlockWasReceived(CNode *pfrom, const uint256 &hash);
    bool AddBlockInFlight(CNode *pfrom, const uint256 &hash, const std::string thinType);
    void ClearBlockInFlight(CNode *pfrom, const uint256 &hash);
    void CheckForDownloadTimeout(CNode *pfrom);
    void RequestBlock(CNode *pfrom, const uint256 &hash);

    // Find the largest block being reconstructed and disconnect it.
    bool ClearLargestBlockAndDisconnect(CNode *pfrom);

    // Accessor methods to the blocks that we're reconstructing from thintype blocks such as
    // xthins or graphene.
    std::shared_ptr<CBlockThinRelay> SetBlockToReconstruct(CNode *pfrom, const uint256 &hash);
    std::shared_ptr<CBlockThinRelay> GetBlockToReconstruct(CNode *pfrom);
    void ClearBlockToReconstruct(CNode *pfrom);

    // Accessor methods for tracking total block bytes for all blocks currently in the process
    // of being reconstructed.
    uint64_t AddTotalBlockBytes(uint64_t, std::shared_ptr<CBlockThinRelay> pblock);
    void DeleteTotalBlockBytes(uint64_t bytes);
    void ClearBlockBytes(std::shared_ptr<CBlockThinRelay> pblock);
    void ClearAllBlockData(CNode *pnode, std::shared_ptr<CBlockThinRelay> pblock);
    void ResetTotalBlockBytes();
    uint64_t GetTotalBlockBytes();
};
extern ThinTypeRelay thinrelay;

#endif // BITCOIN_BLOCKRELAY_COMMON_H
