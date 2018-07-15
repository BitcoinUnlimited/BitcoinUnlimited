// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRAPHENE_H
#define BITCOIN_GRAPHENE_H

#include "bloom.h"
#include "config.h"
#include "consensus/validation.h"
#include "graphene_set.h"
#include "iblt.h"
#include "primitives/block.h"
#include "protocol.h"
#include "serialize.h"
#include "stat.h"
#include "sync.h"
#include "uint256.h"

#include <atomic>
#include <vector>

const unsigned char MIN_MEMPOOL_INFO_BYTES = 8;

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
public:
    CBlockHeader header;
    std::vector<uint256> vTxHashes; // List of all transactions id's in the block
    std::vector<CTransactionRef> vAdditionalTxs; // vector of transactions receiver probably does not have
    uint64_t nBlockTxs;
    CGrapheneSet *pGrapheneSet;

public:
    CGrapheneBlock(const CBlockRef pblock, uint64_t nReceiverMemPoolTx);
    CGrapheneBlock() : pGrapheneSet(nullptr) {}
    ~CGrapheneBlock();
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
        READWRITE(header);
        READWRITE(vAdditionalTxs);
        READWRITE(nBlockTxs);
        if (!pGrapheneSet)
            pGrapheneSet = new CGrapheneSet();
        READWRITE(*pGrapheneSet);
    }
    uint64_t GetAdditionalTxSerializationSize()
    {
        return ::GetSerializeSize(vAdditionalTxs, SER_NETWORK, PROTOCOL_VERSION);
    }
    CInv GetInv() { return CInv(MSG_BLOCK, header.GetHash()); }
    bool process(CNode *pfrom, int nSizeGrapheneBlock, std::string strCommand);
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

// This class stores statistics for graphene block derived protocols.
class CGrapheneBlockData
{
private:
    /* The sum total of all bytes for graphene blocks currently in process of being reconstructed */
    std::atomic<uint64_t> nGrapheneBlockBytes{0};

    CCriticalSection cs_mapGrapheneBlockTimer; // locks mapGrapheneBlockTimer
    std::map<uint256, uint64_t> mapGrapheneBlockTimer;

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

    bool CheckGrapheneBlockTimer(const uint256 &hash);
    void ClearGrapheneBlockTimer(const uint256 &hash);

    void ClearGrapheneBlockData(CNode *pfrom);
    void ClearGrapheneBlockData(CNode *pfrom, const uint256 &hash);
    void ClearGrapheneBlockStats();

    uint64_t AddGrapheneBlockBytes(uint64_t, CNode *pfrom);
    void DeleteGrapheneBlockBytes(uint64_t, CNode *pfrom);
    void ResetGrapheneBlockBytes();
    uint64_t GetGrapheneBlockBytes();
};
extern CGrapheneBlockData graphenedata; // Singleton class


bool HaveGrapheneNodes();
bool IsGrapheneBlockEnabled();
bool CanGrapheneBlockBeDownloaded(CNode *pto);
bool ClearLargestGrapheneBlockAndDisconnect(CNode *pfrom);
void ClearGrapheneBlockInFlight(CNode *pfrom, const uint256 &hash);
void AddGrapheneBlockInFlight(CNode *pfrom, const uint256 &hash);
void SendGrapheneBlock(CBlockRef pblock, CNode *pfrom, const CInv &inv, const CMemPoolInfo &mempoolinfo);
bool IsGrapheneBlockValid(CNode *pfrom, const CBlockHeader &header);
bool HandleGrapheneBlockRequest(CDataStream &vRecv, CNode *pfrom, const CChainParams &chainparams);
CMemPoolInfo GetGrapheneMempoolInfo();
void RequestFailoverBlock(CNode *pfrom, uint256 blockHash);

#endif // BITCOIN_GRAPHENE_H
