// Copyright (c) 2016-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COMPACTBLOCK_H
#define BITCOIN_COMPACTBLOCK_H

#include "bloom.h"
#include "consensus/validation.h"
#include "fastfilter.h"
#include "primitives/block.h"
#include "protocol.h"
#include "serialize.h"
#include "stat.h"
#include "sync.h"
#include "uint256.h"
#include <atomic>
#include <memory>
#include <vector>

class CTxMemPool;
class CDataStream;
class CNode;


uint64_t GetShortID(const uint64_t &shorttxidk0, const uint64_t &shorttxidk1, const uint256 &txhash);


// Dumb helper to handle CTransaction compression at serialize-time
struct TransactionCompressor
{
private:
    CTransaction &tx;

public:
    TransactionCompressor(CTransaction &txIn) : tx(txIn) {}
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(tx); // TODO: Compress tx encoding
    }
};

class CompactReRequest
{
public:
    // A CompactReRequest message
    uint256 blockhash;
    std::vector<uint16_t> indexes;

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
        READWRITE(blockhash);
        uint64_t indexes_size = (uint64_t)indexes.size();
        READWRITE(COMPACTSIZE(indexes_size));
        if (ser_action.ForRead())
        {
            size_t i = 0;
            while (indexes.size() < indexes_size)
            {
                indexes.resize(std::min((uint64_t)(1000 + indexes.size()), indexes_size));
                for (; i < indexes.size(); i++)
                {
                    uint64_t index = 0;
                    READWRITE(COMPACTSIZE(index));
                    if (index > std::numeric_limits<uint16_t>::max())
                        throw std::ios_base::failure("index overflowed 16 bits");
                    indexes[i] = index;
                }
            }

            uint16_t offset = 0;
            for (size_t j = 0; j < indexes.size(); j++)
            {
                if (uint64_t(indexes[j]) + uint64_t(offset) > std::numeric_limits<uint16_t>::max())
                    throw std::ios_base::failure("indexes overflowed 16 bits");
                indexes[j] = indexes[j] + offset;
                offset = indexes[j] + 1;
            }
        }
        else
        {
            for (size_t j = 0; j < indexes.size(); j++)
            {
                uint64_t index = indexes[j] - (j == 0 ? 0 : (indexes[j - 1] + 1));
                READWRITE(COMPACTSIZE(index));
            }
        }
    }
};

class CompactReReqResponse
{
public:
    uint256 blockhash;
    std::vector<CTransaction> txn;

    CompactReReqResponse() {}
    CompactReReqResponse(const CompactReRequest &req) : blockhash(req.blockhash), txn(req.indexes.size()) {}
    CompactReReqResponse(const CBlock &block, const std::vector<uint16_t> &indexes)
    {
        blockhash = block.GetHash();
        if (indexes.size() > block.vtx.size())
            throw std::invalid_argument("request more transactions than are in a block");
        for (uint16_t i : indexes)
        {
            if (i >= block.vtx.size())
                throw std::invalid_argument("out of bound tx in rerequest");
            txn.push_back(*block.vtx.at(i));
        }
    }

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
        READWRITE(blockhash);
        uint64_t txn_size = (uint64_t)txn.size();
        READWRITE(COMPACTSIZE(txn_size));
        if (ser_action.ForRead())
        {
            size_t i = 0;
            while (txn.size() < txn_size)
            {
                txn.resize(std::min((uint64_t)(1000 + txn.size()), txn_size));
                for (; i < txn.size(); i++)
                    READWRITE(REF(TransactionCompressor(txn[i])));
            }
        }
        else
        {
            for (size_t i = 0; i < txn.size(); i++)
                READWRITE(REF(TransactionCompressor(txn[i])));
        }
    }
};

// Dumb serialization/storage-helper for CompactBlock
struct PrefilledTransaction
{
    // Used as an offset since last prefilled tx in CompactBlock,
    uint16_t index;
    CTransaction tx;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        uint64_t idx = index;
        READWRITE(COMPACTSIZE(idx));
        if (idx > std::numeric_limits<uint16_t>::max())
            throw std::ios_base::failure("index overflowed 16-bits");
        index = idx;
        READWRITE(REF(TransactionCompressor(tx)));
    }
};

class CompactBlock
{
public:
    mutable uint64_t shorttxidk0, shorttxidk1;

private:
    uint64_t nonce;

    void FillShortTxIDSelector() const;

public:
    static const int SHORTTXIDS_LENGTH = 6;

    std::vector<uint64_t> shorttxids;
    std::vector<PrefilledTransaction> prefilledtxn;

    CBlockHeader header;

    // Dummy for deserialization
    CompactBlock() {}
    CompactBlock(const CBlock &block, const CRollingFastFilter<4 * 1024 * 1024> *inventoryKnown = nullptr);

    /**
     * Handle an incoming thin block.  The block is fully validated, and if any transactions are missing, we fall
     * back to requesting a full block.
     * @param[in] vRecv        The raw binary message
     * @param[in] pFrom        The node the message was from
     * @return True if handling succeeded
     */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom);
    bool process(CNode *pfrom, uint64_t nSizeCompactBlock);
    CInv GetInv() { return CInv(MSG_BLOCK, header.GetHash()); }
    uint64_t GetShortID(const uint256 &txhash) const;

    size_t BlockTxCount() const { return shorttxids.size() + prefilledtxn.size(); }
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(header);
        READWRITE(nonce);

        uint64_t shorttxids_size = (uint64_t)shorttxids.size();
        READWRITE(COMPACTSIZE(shorttxids_size));
        if (ser_action.ForRead())
        {
            size_t i = 0;
            while (shorttxids.size() < shorttxids_size)
            {
                shorttxids.resize(std::min((uint64_t)(1000 + shorttxids.size()), shorttxids_size));
                for (; i < shorttxids.size(); i++)
                {
                    uint32_t lsb = 0;
                    uint16_t msb = 0;
                    READWRITE(lsb);
                    READWRITE(msb);
                    shorttxids[i] = (uint64_t(msb) << 32) | uint64_t(lsb);
                    static_assert(SHORTTXIDS_LENGTH == 6, "shorttxids serialization assumes 6-byte shorttxids");
                }
            }
        }
        else
        {
            for (size_t i = 0; i < shorttxids.size(); i++)
            {
                uint32_t lsb = shorttxids[i] & 0xffffffff;
                uint16_t msb = (shorttxids[i] >> 32) & 0xffff;
                READWRITE(lsb);
                READWRITE(msb);
            }
        }

        READWRITE(prefilledtxn);

        if (ser_action.ForRead())
            FillShortTxIDSelector();
    }
};

void validateCompactBlock(const CompactBlock &cmpctblock);


// This struct is so we can obtain a quick summar of stats for UI display purposes
// without needing to take the lock more than once
struct CompactBlockQuickStats
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
class CCompactBlockData
{
private:
    /* The sum total of all bytes for thinblocks currently in process of being reconstructed */
    std::atomic<uint64_t> nCompactBlockBytes{0};

    CCriticalSection cs_compactblockstats; // locks everything below this point

    CStatHistory<uint64_t> nOriginalSize;
    CStatHistory<uint64_t> nCompactSize;
    CStatHistory<uint64_t> nInBoundBlocks;
    CStatHistory<uint64_t> nOutBoundBlocks;
    CStatHistory<uint64_t> nMempoolLimiterBytesSaved;
    CStatHistory<uint64_t> nTotalCompactBlockBytes;
    CStatHistory<uint64_t> nTotalFullTxBytes;
    std::map<int64_t, std::pair<uint64_t, uint64_t> > mapCompactBlocksInBound;
    std::map<int64_t, std::pair<uint64_t, uint64_t> > mapCompactBlocksOutBound;
    std::map<int64_t, double> mapCompactBlockResponseTime;
    std::map<int64_t, double> mapCompactBlockValidationTime;
    std::map<int64_t, int> mapCompactBlocksInBoundReRequestedTx;
    std::map<int64_t, uint64_t> mapCompactBlock;
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

      Side-effect: This method calls expireStats() on mapCompactBlocks

      @param [mapCompactBlocks] a statistics array of inbound/outbound XThin blocks
     */
    double compute24hAverageCompressionInternal(std::map<int64_t, std::pair<uint64_t, uint64_t> > &mapCompactBlocks)
        EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats);

    /**
      Calculate last 24-hour transaction re-request percent for inbound XThin
      Requires lock on cs_thinblockstats be held external to this call.

      Side-effect: This method calls expireStats() on mapCompactBlocksInBoundReRequestedTx */
    double compute24hInboundRerequestTxPercentInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats);

protected:
    //! Virtual method so it can be overridden for better unit testing
    virtual int64_t getTimeForStats() { return GetTimeMillis(); }
public:
    void UpdateInBound(uint64_t nCompactBlockSize, uint64_t nOriginalBlockSize);
    void UpdateOutBound(uint64_t nCompactBlockSize, uint64_t nOriginalBlockSize);
    void UpdateResponseTime(double nResponseTime);
    void UpdateValidationTime(double nValidationTime);
    void UpdateInBoundReRequestedTx(int nReRequestedTx);
    void UpdateMempoolLimiterBytesSaved(unsigned int nBytesSaved);
    void UpdateCompactBlock(uint64_t nCompactBlockSize);
    void UpdateFullTx(uint64_t nFullTxSize);
    std::string ToString();
    std::string InBoundPercentToString();
    std::string OutBoundPercentToString();
    std::string ResponseTimeToString();
    std::string ValidationTimeToString();
    std::string ReRequestedTxToString();
    std::string MempoolLimiterBytesSavedToString();
    std::string CompactBlockToString();
    std::string FullTxToString();

    void ClearCompactBlockData(CNode *pfrom);
    void ClearCompactBlockData(CNode *pfrom, const uint256 &hash);
    void ClearCompactBlockStats();

    uint64_t AddCompactBlockBytes(uint64_t, CNode *pfrom);
    void DeleteCompactBlockBytes(uint64_t, CNode *pfrom);
    void ResetCompactBlockBytes();
    uint64_t GetCompactBlockBytes();

    void FillCompactBlockQuickStats(CompactBlockQuickStats &stats);
};
extern CCompactBlockData compactdata; // Singleton class


bool IsCompactBlocksEnabled();
bool ClearLargestCompactBlockAndDisconnect(CNode *pfrom);
void SendCompactBlock(ConstCBlockRef pblock, CNode *pfrom, const CInv &inv);
bool IsCompactBlockValid(CNode *pfrom, const CompactBlock &cmpctblock);

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

#endif // BITCOIN_COMPACTBLOCK_H
