// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRAPHENE_H
#define BITCOIN_GRAPHENE_H

#include "blockrelay/blockrelay_common.h"
#include "blockrelay/graphene_set.h"
#include "bloom.h"
#include "config.h"
#include "consensus/validation.h"
#include "iblt.h"
#include "primitives/block.h"
#include "protocol.h"
#include "serialize.h"
#include "stat.h"
#include "sync.h"
#include "uint256.h"
#include "unlimited.h"

#include <atomic>
#include <vector>

enum FastFilterSupport
{
    EITHER,
    FAST,
    REGULAR
};

const uint8_t GRAPHENE_FAST_FILTER_SUPPORT = EITHER;
const uint64_t GRAPHENE_MIN_VERSION_SUPPORTED = 0;
const uint64_t GRAPHENE_MAX_VERSION_SUPPORTED = 4;
const unsigned char MIN_MEMPOOL_INFO_BYTES = 8;
const uint8_t SHORTTXIDS_LENGTH = 8;

class CDataStream;
class CNode;

class CMemPoolInfo
{
public:
    uint64_t nTx;

public:
    CMemPoolInfo(uint64_t nTx);
    CMemPoolInfo();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(nTx);
    }
};

class CGrapheneBlock
{
private:
    // Entropy used for SipHash secret key; this is distinct from the block nonce
    uint64_t sipHashNonce;

private:
    // memory only
    mutable uint64_t nSize; // Serialized grapheneblock size in bytes

public:
    // memory only
    mutable unsigned int nWaitingFor; // Number of txns we are still needing to recontruct the block

    // memory only
    std::vector<uint256> vTxHashes256; // List of all 256 bit transaction hashes in the block
    std::map<uint64_t, CTransactionRef> mapMissingTx; // Map of transactions that were re-requested
    std::vector<CTransactionRef> vAdditionalTxs; // vector of transactions receiver probably does not have
    std::map<uint64_t, uint32_t> mapHashOrderIndex;

public:
    // These describe, in two parts, the 128-bit secret key used for SipHash
    // Note that they are populated by FillShortTxIDSelector, which uses header and sipHashNonce
    uint64_t shorttxidk0, shorttxidk1;
    CBlockHeader header;
    uint64_t nBlockTxs;
    std::shared_ptr<CGrapheneSet> pGrapheneSet;
    uint64_t version;
    bool computeOptimized;

public:
    CGrapheneBlock(const CBlockRef pblock,
        uint64_t nReceiverMemPoolTx,
        uint64_t nSenderMempoolPlusBlock,
        uint64_t _version,
        bool _computeOptimized);
    CGrapheneBlock()
        : nSize(0), nWaitingFor(0), shorttxidk0(0), shorttxidk1(0), pGrapheneSet(nullptr), version(2),
          computeOptimized(false)
    {
    }
    CGrapheneBlock(uint64_t _version)
        : nSize(0), nWaitingFor(0), shorttxidk0(0), shorttxidk1(0), pGrapheneSet(nullptr), version(_version),
          computeOptimized(false)
    {
    }
    CGrapheneBlock(uint64_t _version, bool _computeOptimized)
        : nSize(0), nWaitingFor(0), shorttxidk0(0), shorttxidk1(0), pGrapheneSet(nullptr), version(_version),
          computeOptimized(_computeOptimized)
    {
    }
    ~CGrapheneBlock();
    // Create seeds for SipHash using the sipHashNonce generated in the constructor
    // Note that this must be called any time members header or sipHashNonce are changed
    void FillShortTxIDSelector();
    /**
     * Handle an incoming Graphene block
     * Once the block is validated apart from the Merkle root, forward the Xpedited block with a hop count of nHops.
     * @param[in]  vRecv        The raw binary message
     * @param[in]  pFrom        The node the message was from
     * @param[in]  strCommand   The message kind
     * @param[in]  nHops        On the wire, nHops is zero for an incoming Graphene block
     * @return True if handling succeeded
     */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom, std::string strCommand, unsigned nHops);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        if (version >= 2)
        {
            READWRITE(shorttxidk0);
            READWRITE(shorttxidk1);
            READWRITE(sipHashNonce);
        }
        READWRITE(header);
        READWRITE(vAdditionalTxs);
        READWRITE(nBlockTxs);
        // This logic assumes a smallest transaction size of MIN_TX_SIZE bytes.  This is optimistic for realistic
        // transactions and the downside for pathological blocks is just that graphene won't work so we fall back
        // to xthin
        if (nBlockTxs > (thinrelay.GetMaxAllowedBlockSize() / MIN_TX_SIZE))
            throw std::runtime_error("nBlockTxs exceeds threshold for excessive block txs");
        if (!pGrapheneSet)
        {
            if (version > 3)
                pGrapheneSet = std::make_shared<CGrapheneSet>(CGrapheneSet(3, computeOptimized));
            else if (version == 3)
                pGrapheneSet = std::make_shared<CGrapheneSet>(CGrapheneSet(2));
            else if (version == 2)
                pGrapheneSet = std::make_shared<CGrapheneSet>(CGrapheneSet(1));
            else
                pGrapheneSet = std::make_shared<CGrapheneSet>(CGrapheneSet(0));
        }
        READWRITE(*pGrapheneSet);
    }
    uint64_t GetAdditionalTxSerializationSize()
    {
        return ::GetSerializeSize(vAdditionalTxs, SER_NETWORK, PROTOCOL_VERSION);
    }

    uint64_t GetSize() const
    {
        if (nSize == 0)
            nSize = ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
        return nSize;
    }

    CInv GetInv() { return CInv(MSG_BLOCK, header.GetHash()); }
    bool process(CNode *pfrom, std::string strCommand, std::shared_ptr<CBlockThinRelay> pblock);
    bool CheckBlockHeader(const CBlockHeader &block, CValidationState &state);
};

// This class is used to respond to requests for missing transactions after sending an Graphene block.
// It is filled with the requested transactions in order.
class CGrapheneBlockTx
{
public:
    /** Public only for unit testing */
    uint256 blockhash;
    std::vector<CTransaction> vMissingTx; // map of missing transactions

public:
    CGrapheneBlockTx(uint256 blockHash, std::vector<CTransaction> &vTx);
    CGrapheneBlockTx() {}
    /**
     * Handle receiving a list of missing graphene block transactions from a prior request
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

// This class is used for requests for still missing transactions after processing a "graphene" message.
// This class uses a 64bit hash as opposed to the normal 256bit hash.  The target is expected to reply with
// a serialized CGrapheneBlockTx response message.
class CRequestGrapheneBlockTx
{
public:
    /** Public only for unit testing */
    uint256 blockhash;
    std::set<uint64_t> setCheapHashesToRequest; // map of missing transactions

public:
    CRequestGrapheneBlockTx(uint256 blockHash, std::set<uint64_t> &setHashesToRequest);
    CRequestGrapheneBlockTx() {}
    /**
     * Handle an incoming request for missing graphene block transactions
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
struct GrapheneQuickStats
{
    // Totals for the lifetime of the node (or since last clear of stats)
    uint64_t nTotalInbound;
    uint64_t nTotalOutbound;
    uint64_t nTotalBandwidthSavings;
    uint64_t nTotalDecodeFailures;

    // Last 24-hour averages (or since last clear of stats)
    uint64_t nLast24hInbound;
    double fLast24hInboundCompression;
    uint64_t nLast24hOutbound;
    double fLast24hOutboundCompression;
    uint64_t nLast24hRerequestTx;
    double fLast24hRerequestTxPercent;
};

// This class stores statistics for graphene block derived protocols.
class CGrapheneBlockData
{
private:
    /* The sum total of all bytes for graphene blocks currently in process of being reconstructed */
    std::atomic<uint64_t> nGrapheneBlockBytes{0};

    CCriticalSection cs_graphenestats; // locks everything below this point

    CStatHistory<uint64_t> nOriginalSize;
    CStatHistory<uint64_t> nGrapheneSize;
    CStatHistory<uint64_t> nInBoundBlocks;
    CStatHistory<uint64_t> nOutBoundBlocks;
    CStatHistory<uint64_t> nDecodeFailures;
    CStatHistory<uint64_t> nTotalMemPoolInfoBytes;
    CStatHistory<uint64_t> nTotalFilterBytes;
    CStatHistory<uint64_t> nTotalIbltBytes;
    CStatHistory<uint64_t> nTotalRankBytes;
    CStatHistory<uint64_t> nTotalGrapheneBlockBytes;
    CStatHistory<uint64_t> nTotalAdditionalTxBytes;
    std::map<int64_t, std::pair<uint64_t, uint64_t> > mapGrapheneBlocksInBound;
    std::map<int64_t, std::pair<uint64_t, uint64_t> > mapGrapheneBlocksOutBound;
    std::map<int64_t, uint64_t> mapMemPoolInfoOutBound;
    std::map<int64_t, uint64_t> mapMemPoolInfoInBound;
    std::map<int64_t, uint64_t> mapFilter;
    std::map<int64_t, uint64_t> mapIblt;
    std::map<int64_t, uint64_t> mapRank;
    std::map<int64_t, uint64_t> mapGrapheneBlock;
    std::map<int64_t, uint64_t> mapAdditionalTx;
    std::map<int64_t, double> mapGrapheneBlockResponseTime;
    std::map<int64_t, double> mapGrapheneBlockValidationTime;
    std::map<int64_t, int> mapGrapheneBlocksInBoundReRequestedTx;

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
      Calculate total bandwidth savings for using Graphene.
      Requires lock on cs_graphenestats be held external to this call. */
    double computeTotalBandwidthSavingsInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_graphenestats);

    /**
      Calculate last 24-hour "compression" percent for graphene
      Requires lock on cs_graphenestats be held external to this call.

      NOTE: The graphene block and mempool info maps should be from opposite directions
            For example inbound block map paired wtih outbound mempool info map

      Side-effect: This method calls expireStats() on mapGrapheneBlocks and mapMemPoolInfo

      @param [mapGrapheneBlocks] a statistics array of inbound/outbound graphene blocks
      @param [mapMemPoolInfo] a statistics array of outbound/inbound graphene mempool info
     */
    double compute24hAverageCompressionInternal(std::map<int64_t, std::pair<uint64_t, uint64_t> > &mapGrapheneBlocks,
        std::map<int64_t, uint64_t> &mapMemPoolInfo) EXCLUSIVE_LOCKS_REQUIRED(cs_graphenestats);

    /**
      Calculate last 24-hour transaction re-request percent for inbound graphene blocks
      Requires lock on cs_graphenestats be held external to this call.

      Side-effect: This method calls expireStats() on mapGrapheneBlocksInBoundReRequestedTx */
    double compute24hInboundRerequestTxPercentInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_graphenestats);

protected:
    //! Virtual method so it can be overridden for better unit testing
    virtual int64_t getTimeForStats() { return GetTimeMillis(); }
public:
    void IncrementDecodeFailures();
    void UpdateInBound(uint64_t nGrapheneBlockSize, uint64_t nOriginalBlockSize);
    void UpdateOutBound(uint64_t nGrapheneBlockSize, uint64_t nOriginalBlockSize);
    void UpdateOutBoundMemPoolInfo(uint64_t nMemPoolInfoSize);
    void UpdateInBoundMemPoolInfo(uint64_t nMemPoolInfoSize);
    void UpdateFilter(uint64_t nFilterSize);
    void UpdateIblt(uint64_t nIbltSize);
    void UpdateRank(uint64_t nRankSize);
    void UpdateGrapheneBlock(uint64_t nRankSize);
    void UpdateAdditionalTx(uint64_t nAdditionalTxSize);
    void UpdateResponseTime(double nResponseTime);
    void UpdateValidationTime(double nValidationTime);
    void UpdateInBoundReRequestedTx(int nReRequestedTx);
    std::string ToString();
    std::string InBoundPercentToString();
    std::string OutBoundPercentToString();
    std::string InBoundMemPoolInfoToString();
    std::string OutBoundMemPoolInfoToString();
    std::string FilterToString();
    std::string IbltToString();
    std::string RankToString();
    std::string GrapheneBlockToString();
    std::string AdditionalTxToString();
    std::string ResponseTimeToString();
    std::string ValidationTimeToString();
    std::string ReRequestedTxToString();

    void ClearGrapheneBlockStats();

    void FillGrapheneQuickStats(GrapheneQuickStats &stats);
};
extern CGrapheneBlockData graphenedata; // Singleton class


bool IsGrapheneBlockEnabled();
void SendGrapheneBlock(CBlockRef pblock, CNode *pfrom, const CInv &inv, const CMemPoolInfo &mempoolinfo);
bool IsGrapheneBlockValid(CNode *pfrom, const CBlockHeader &header);
bool HandleGrapheneBlockRequest(CDataStream &vRecv, CNode *pfrom, const CChainParams &chainparams);
CMemPoolInfo GetGrapheneMempoolInfo();
void RequestFailoverBlock(CNode *pfrom, std::shared_ptr<CBlockThinRelay> pblock);
// Generate cheap hash from seeds using SipHash
uint64_t GetShortID(uint64_t shorttxidk0, uint64_t shorttxidk1, const uint256 &txhash, uint64_t grapheneVersion);
// This method decides on the value of computeOptimized depending on what modes are supported
// by both the sender and receiver
bool NegotiateFastFilterSupport(CNode *pfrom);
uint64_t NegotiateGrapheneVersion(CNode *pfrom);

#endif // BITCOIN_GRAPHENE_H
