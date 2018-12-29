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

private:
    // block relay timer
    CCriticalSection cs_blockrelaytimer;
    std::map<uint256, std::pair<uint64_t, bool> > mapBlockRelayTimer GUARDED_BY(cs_blockrelaytimer);

    // thin type blocks in flight and the time they were requested.
    std::multimap<const NodeId, CThinTypeBlockInFlight> mapThinTypeBlocksInFlight GUARDED_BY(cs_inflight);

    // put a cap on the total number of thin type blocks we can have in flight. This lowers any possible
    // attack surface.
    size_t MAX_THINTYPE_BLOCKS_IN_FLIGHT = 6;

    // Counters for how many of each peer are currently connected.
    std::atomic<int32_t> nThinBlockPeers{0};
    std::atomic<int32_t> nGraphenePeers{0};
    std::atomic<int32_t> nCompactBlockPeers{0};

public:
    void AddThinTypePeers(CNode *pfrom);
    void AddCompactBlockPeer(CNode *pfrom);
    void RemoveThinTypePeers(CNode *pfrom);
    bool HasBlockRelayTimerExpired(const uint256 &hash);
    bool IsBlockRelayTimerEnabled();
    void ClearBlockRelayTimer(const uint256 &hash);
    bool IsThinTypeBlockInFlight(CNode *pfrom, const std::string thinType);
    unsigned int TotalThinTypeBlocksInFlight();
    void ThinTypeBlockWasReceived(CNode *pfrom, const uint256 &hash);
    bool AddThinTypeBlockInFlight(CNode *pfrom, const uint256 &hash, const std::string thinType);
    void ClearThinTypeBlockInFlight(CNode *pfrom, const uint256 &hash);
    void CheckForThinTypeDownloadTimeout(CNode *pfrom);
    void RequestBlock(CNode *pfrom, const uint256 &hash);
};
extern ThinTypeRelay thinrelay;

#endif // BITCOIN_BLOCKRELAY_COMMON_H
