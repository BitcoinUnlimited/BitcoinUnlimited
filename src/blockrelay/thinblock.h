// Copyright (c) 2016-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THINBLOCK_H
#define BITCOIN_THINBLOCK_H

#include "bloom.h"
#include "consensus/validation.h"
#include "primitives/block.h"
#include "protocol.h"
#include "serialize.h"
#include "stat.h"
#include "sync.h"
#include "uint256.h"
#include <atomic>
#include <vector>

class CDataStream;
class CNode;

class CThinBlock
{
public:
    CBlockHeader header;
    std::vector<uint256> vTxHashes; // List of all transaction ids in the block
    std::vector<CTransaction> vMissingTx; // vector of transactions that did not match the bloom filter

public:
    CThinBlock(const CBlock &block, CBloomFilter &filter);
    CThinBlock() {}
    /**
     * Handle an incoming thin block.  The block is fully validated, and if any transactions are missing, we fall
     * back to requesting a full block.
     * @param[in] vRecv        The raw binary message
     * @param[in] pFrom        The node the message was from
     * @return True if handling succeeded
     */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(header);
        READWRITE(vTxHashes);
        READWRITE(vMissingTx);
    }

    CInv GetInv() { return CInv(MSG_BLOCK, header.GetHash()); }
    bool process(CNode *pfrom, int nSizeThinBlock);
};

class CXThinBlock
{
public:
    CBlockHeader header;
    std::vector<uint64_t> vTxHashes; // List of all transaction ids in the block
    std::vector<CTransaction> vMissingTx; // vector of transactions that did not match the bloom filter
    bool collision;

public:
    CXThinBlock(const CBlock &block, CBloomFilter *filter); // Use the filter to determine which txns the client has
    CXThinBlock(const CBlock &block); // Assume client has all of the transactions (except coinbase)
    CXThinBlock() {}
    /**
     * Handle an incoming Xthin or Xpedited block
     * Once the block is validated apart from the Merkle root, forward the Xpedited block with a hop count of nHops.
     * @param[in]  vRecv        The raw binary message
     * @param[in] pFrom        The node the message was from
     * @param[in]  strCommand   The message kind
     * @param[in]  nHops        On the wire, an Xpedited block has a hop count of zero the first time it is sent, and
     *                          the hop count is incremented each time it is forwarded.  nHops is zero for an incoming
     *                          Xthin block, and for an incoming Xpedited block its hop count + 1.
     * @return True if handling succeeded
     */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom, std::string strCommand, unsigned nHops);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(header);
        READWRITE(vTxHashes);
        READWRITE(vMissingTx);
    }
    CInv GetInv() { return CInv(MSG_BLOCK, header.GetHash()); }
    bool process(CNode *pfrom, int nSizeThinbBlock, std::string strCommand);
    bool CheckBlockHeader(const CBlockHeader &block, CValidationState &state);
};

// This class is used to respond to requests for missing transactions after sending an XThin block.
// It is filled with the requested transactions in order.
class CXThinBlockTx
{
public:
    /** Public only for unit testing */
    uint256 blockhash;
    std::vector<CTransaction> vMissingTx; // array of missing transactions

public:
    CXThinBlockTx(uint256 blockHash, std::vector<CTransaction> &vTx);
    CXThinBlockTx() {}
    /**
     * Handle receiving a list of missing xthin block transactions from a prior request
     * @param[in] vRecv        The raw binary message
     * @param[in] pFrom        The node the message was from
     * @return True if handling succeeded
     */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(blockhash);
        READWRITE(vMissingTx);
    }
};

// This class is used for requests for still missing transactions after processing a "thinblock" message.
// This class uses a 64bit hash as opposed to the normal 256bit hash.  The target is expected to reply with
// a serialized CXThinBlockTx response message.
class CXRequestThinBlockTx
{
public:
    /** Public only for unit testing */
    uint256 blockhash;
    std::set<uint64_t> setCheapHashesToRequest; // set of missing transactions

public:
    CXRequestThinBlockTx(uint256 blockHash, std::set<uint64_t> &setHashesToRequest);
    CXRequestThinBlockTx() {}
    /**
     * Handle an incoming request for missing xthin block transactions
     * @param[in] vRecv        The raw binary message
     * @param[in] pFrom        The node the message was from
     * @return True if handling succeeded
     */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom);
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(blockhash);
        READWRITE(setCheapHashesToRequest);
    }
};

// This struct is so we can obtain a quick summar of stats for UI display purposes
// without needing to take the lock more than once
struct ThinBlockQuickStats
{
    // Totals for the lifetime of the node (or since last clear of stats)
    uint64_t nTotalInbound;
    uint64_t nTotalOutbound;
    uint64_t nTotalBandwidthSavings;

    // Last 24-hour averages (or since last clear of stats)
    uint64_t nLast24hInbound;
    double fLast24hInboundCompression;
    uint64_t nLast24hOutbound;
    double fLast24hOutboundCompression;
    uint64_t nLast24hRerequestTx;
    double fLast24hRerequestTxPercent;
};

// This class stores statistics for thin block derived protocols.
class CThinBlockData
{
private:
    /* The sum total of all bytes for thinblocks currently in process of being reconstructed */
    std::atomic<uint64_t> nThinBlockBytes{0};

    CCriticalSection cs_mapThinBlockTimer; // locks mapThinBlockTimer
    std::map<uint256, std::pair<uint64_t, bool> > mapThinBlockTimer;

    CCriticalSection cs_thinblockstats; // locks everything below this point

    CStatHistory<uint64_t> nOriginalSize;
    CStatHistory<uint64_t> nThinSize;
    CStatHistory<uint64_t> nInBoundBlocks;
    CStatHistory<uint64_t> nOutBoundBlocks;
    CStatHistory<uint64_t> nMempoolLimiterBytesSaved;
    CStatHistory<uint64_t> nTotalBloomFilterBytes;
    CStatHistory<uint64_t> nTotalThinBlockBytes;
    CStatHistory<uint64_t> nTotalFullTxBytes;
    std::map<int64_t, std::pair<uint64_t, uint64_t> > mapThinBlocksInBound;
    std::map<int64_t, std::pair<uint64_t, uint64_t> > mapThinBlocksOutBound;
    std::map<int64_t, uint64_t> mapBloomFiltersOutBound;
    std::map<int64_t, uint64_t> mapBloomFiltersInBound;
    std::map<int64_t, double> mapThinBlockResponseTime;
    std::map<int64_t, double> mapThinBlockValidationTime;
    std::map<int64_t, int> mapThinBlocksInBoundReRequestedTx;
    std::map<int64_t, uint64_t> mapThinBlock;
    std::map<int64_t, uint64_t> mapFullTx;

    /**
        Add new entry to statistics array; also removes old timestamps
        from statistics array using expireStats() below.
        @param [statsMap] a statistics array
        @param [value] the value to insert for the current time
     */
    template <class T>
    void updateStats(std::map<int64_t, T> &statsMap, T value);

    /**
       Expire old statistics in given array (currently after one day).
       Uses getTimeForStats() virtual method for timing. */
    template <class T>
    void expireStats(std::map<int64_t, T> &statsMap);

    /**
      Calculate average of long long values in given map. Return 0 for no entries.
      Expires values before calculation. */
    double average(std::map<int64_t, uint64_t> &map);

    /**
      Calculate total bandwidth savings for using XThin.
      Requires lock on cs_thinblockstats be held external to this call. */
    double computeTotalBandwidthSavingsInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats);

    /**
      Calculate last 24-hour "compression" percent for XThin
      Requires lock on cs_thinblockstats be held external to this call.

      NOTE: The thinblock and bloom filter maps should be from opposite directions
            For example inbound block map paired wtih outbound bloom filter map

      Side-effect: This method calls expireStats() on mapThinBlocks and mapBloomFilters

      @param [mapThinBlocks] a statistics array of inbound/outbound XThin blocks
      @param [mapBloomFilters] a statistics array of outbound/inbound XThin bloom filters
     */
    double compute24hAverageCompressionInternal(std::map<int64_t, std::pair<uint64_t, uint64_t> > &mapThinBlocks,
        std::map<int64_t, uint64_t> &mapBloomFilters) EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats);

    /**
      Calculate last 24-hour transaction re-request percent for inbound XThin
      Requires lock on cs_thinblockstats be held external to this call.

      Side-effect: This method calls expireStats() on mapThinBlocksInBoundReRequestedTx */
    double compute24hInboundRerequestTxPercentInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats);

protected:
    //! Virtual method so it can be overridden for better unit testing
    virtual int64_t getTimeForStats() { return GetTimeMillis(); }
public:
    void UpdateInBound(uint64_t nThinBlockSize, uint64_t nOriginalBlockSize);
    void UpdateOutBound(uint64_t nThinBlockSize, uint64_t nOriginalBlockSize);
    void UpdateOutBoundBloomFilter(uint64_t nBloomFilterSize);
    void UpdateInBoundBloomFilter(uint64_t nBloomFilterSize);
    void UpdateResponseTime(double nResponseTime);
    void UpdateValidationTime(double nValidationTime);
    void UpdateInBoundReRequestedTx(int nReRequestedTx);
    void UpdateMempoolLimiterBytesSaved(unsigned int nBytesSaved);
    void UpdateThinBlock(uint64_t nThinBlockSize);
    void UpdateFullTx(uint64_t nFullTxSize);
    std::string ToString();
    std::string InBoundPercentToString();
    std::string OutBoundPercentToString();
    std::string InBoundBloomFiltersToString();
    std::string OutBoundBloomFiltersToString();
    std::string ResponseTimeToString();
    std::string ValidationTimeToString();
    std::string ReRequestedTxToString();
    std::string MempoolLimiterBytesSavedToString();
    std::string ThinBlockToString();
    std::string FullTxToString();

    bool CheckThinblockTimer(const uint256 &hash);
    void ClearThinBlockTimer(const uint256 &hash);

    void ClearThinBlockData(CNode *pfrom);
    void ClearThinBlockData(CNode *pfrom, const uint256 &hash);
    void ClearThinBlockStats();

    uint64_t AddThinBlockBytes(uint64_t, CNode *pfrom);
    void DeleteThinBlockBytes(uint64_t, CNode *pfrom);
    void ResetThinBlockBytes();
    uint64_t GetThinBlockBytes();

    void FillThinBlockQuickStats(ThinBlockQuickStats &stats);
};
extern CThinBlockData thindata; // Singleton class


bool HaveThinblockNodes();
bool IsThinBlocksEnabled();
bool CanThinBlockBeDownloaded(CNode *pto);
bool ClearLargestThinBlockAndDisconnect(CNode *pfrom);
void ClearThinBlockInFlight(CNode *pfrom, const uint256 &hash);
void AddThinBlockInFlight(CNode *pfrom, const uint256 &hash);
void SendXThinBlock(ConstCBlockRef pblock, CNode *pfrom, const CInv &inv);
bool IsThinBlockValid(CNode *pfrom, const std::vector<CTransaction> &vMissingTx, const CBlockHeader &header);
void BuildSeededBloomFilter(CBloomFilter &memPoolFilter,
    std::vector<uint256> &vOrphanHashes,
    uint256 hash,
    CNode *pfrom,
    bool fDeterministic = false);

// Xpress Validation: begin
// Transactions that have already been accepted into the memory pool do not need to be
// re-verified and can avoid having to do a second and expensive CheckInputs() when
// processing a new block.  (Protected by cs_xval)
extern std::set<uint256> setPreVerifiedTxHash;

// Orphans that are added to the thinblock must be verifed since they have never been
// accepted into the memory pool.  (Protected by cs_xval)
extern std::set<uint256> setUnVerifiedOrphanTxHash;

extern CCriticalSection cs_xval;
// Xpress Validation: end

#endif // BITCOIN_THINBLOCK_H
