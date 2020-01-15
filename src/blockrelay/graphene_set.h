// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRAPHENE_SET_H
#define BITCOIN_GRAPHENE_SET_H

#include "bloom.h"
#include "fastfilter.h"
#include "hashwrapper.h"
#include "iblt.h"
#include "random.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

#include <cmath>
#include <numeric>

#define LN2SQUARED 0.4804530139182014246671025263266649717305529515945455

const uint8_t FILTER_CELL_SIZE = 1;
const uint8_t IBLT_FIXED_CELL_SIZE = 13;
const uint32_t LARGE_MEM_POOL_SIZE = 10000000;
const float FILTER_FPR_MAX = 0.999;
const uint8_t IBLT_CELL_MINIMUM = 2;
const std::vector<uint8_t> IBLT_NULL_VALUE = {};
const unsigned char WORD_BITS = 8;
const uint16_t APPROX_ITEMS_THRESH = 600;
const uint16_t APPROX_ITEMS_THRESH_REDUCE_CHECK = 500;
const uint8_t APPROX_EXCESS_RATE = 4;
const float IBLT_DEFAULT_OVERHEAD = 1.5;
const float UNCHECKED_ERROR_TOL = 0.001;
const uint8_t MIN_CHECKSUM_BITS = 10;
const uint8_t MAX_CHECKSUM_BITS = 32;


struct GrapheneSetOptimizationParams
{
    uint64_t nReceiverExcessItems;
    uint64_t nReceiverMissingItems;
    double optSymDiff;
    double bloomFPR;
};

class CGrapheneSet
{
private:
    bool ordered;
    uint64_t nReceiverUniverseItems;
    mutable uint64_t shorttxidk0, shorttxidk1;
    uint64_t version;
    uint32_t ibltSalt;
    bool computeOptimized;
    std::vector<unsigned char> encodedRank;
    std::shared_ptr<CBloomFilter> pSetFilter;
    std::shared_ptr<CVariableFastFilter> pFastFilter;
    std::shared_ptr<CIblt> pSetIblt;
    double bloomFPR;

    static const uint8_t SHORTTXIDS_LENGTH = 8;

    std::vector<uint64_t> ArgSort(const std::vector<uint64_t> &items)
    {
        std::vector<uint64_t> idxs(items.size());
        std::iota(idxs.begin(), idxs.end(), 0);

        std::sort(idxs.begin(), idxs.end(), [&items](size_t idx1, size_t idx2) { return items[idx1] < items[idx2]; });

        return idxs;
    }

public:
    // The default constructor is for 2-phase construction via deserialization
    CGrapheneSet()
        : ordered(false), nReceiverUniverseItems(0), shorttxidk0(0), shorttxidk1(0), version(1), ibltSalt(0),
          computeOptimized(false), pSetFilter(nullptr), pFastFilter(nullptr), pSetIblt(nullptr), bloomFPR(1.0)
    {
    }
    CGrapheneSet(uint64_t _version)
        : ordered(false), nReceiverUniverseItems(0), shorttxidk0(0), shorttxidk1(0), version(_version), ibltSalt(0),
          computeOptimized(false), pSetFilter(nullptr), pFastFilter(nullptr), pSetIblt(nullptr), bloomFPR(1.0)
    {
    }
    CGrapheneSet(uint64_t _version, bool _computeOptimized)
        : ordered(false), nReceiverUniverseItems(0), shorttxidk0(0), shorttxidk1(0), version(_version), ibltSalt(0),
          computeOptimized(_computeOptimized), pSetFilter(nullptr), pFastFilter(nullptr), pSetIblt(nullptr),
          bloomFPR(1.0)
    {
    }
    CGrapheneSet(size_t _nReceiverUniverseItems,
        uint64_t nSenderUniverseItems,
        const std::vector<uint256> &_itemHashes,
        uint64_t _shorttxidk0,
        uint64_t _shorttxidk1,
        uint64_t _version = 1,
        uint32_t ibltEntropy = 0,
        bool _computeOptimized = false,
        bool _ordered = false,
        bool fDeterministic = false);

    // Generate cheap hash from seeds using SipHash
    uint64_t GetShortID(const uint256 &txhash) const;
    uint64_t GetNReceiverUniverseItems() const { return nReceiverUniverseItems; }
    bool GetOrdered() const { return ordered; }
    bool GetComputeOptimized() const { return computeOptimized; }
    std::vector<unsigned char> GetEncodedRank() const { return encodedRank; }
    std::shared_ptr<CIblt> GetIblt() const { return pSetIblt; }
    std::shared_ptr<CBloomFilter> GetRegularFilter() const { return pSetFilter; }
    std::shared_ptr<CVariableFastFilter> GetFastFilter() const { return pFastFilter; }
    /* Optimal symmetric difference between block txs and receiver mempool txs passing
     * though filter to use for IBLT.
     */
    static double OptimalSymDiff(uint64_t version,
        uint64_t nBlockTxs,
        uint64_t nReceiverPoolTx,
        uint64_t nReceiverExcessTxs = 0,
        uint64_t nReceiverMissingTxs = 1);

    /* Approximation to the optimal symmetric difference between block txs and receiver
     * mempool txs passing through filter to use for IBLT.
     *
     * This method is called by OptimalSymDiff provided that:
     * 1) nBlockTxs >= APPROX_ITEMS_THRESH
     * 2) nReceiverExcessTxs >= nBlockTxs / APPROX_EXCESS_RATE
     *
     * For details see
     * https://github.com/bissias/graphene-experiments/blob/master/jupyter/graphene_size_optimization.ipynb
     */
    static double ApproxOptimalSymDiff(uint64_t version, uint64_t nBlockTxs, uint8_t nChecksumBits = MAX_CHECKSUM_BITS);

    /* Brute force search for optimal symmetric difference between block txs and receiver
     * mempool txs passing though filter to use for IBLT.
     *
     * Let a be defined as the size of the symmetric difference between items in the
     * sender and receiver IBLTs.
     *
     * The total size in bytes of a graphene block is given by T(a) = F(a) + L(a) as defined
     * in the code below. (Note that meta parameters for the Bloom Filter and IBLT are ignored).
     */
    static double BruteForceSymDiff(uint64_t nBlockTxs,
        uint64_t nReceiverPoolTx,
        uint64_t nReceiverExcessTxs,
        uint64_t nReceiverMissingTxs,
        uint8_t nChecksumBits = MAX_CHECKSUM_BITS);

    // Pass the transaction hashes that the local machine has to reconcile with the remote and return a list
    // of cheap hashes in the block in the correct order
    std::vector<uint64_t> Reconcile(const std::vector<uint256> &receiverItemHashes);

    // This version assumes that we know the set that have passed through the sender Bloom filter
    std::vector<uint64_t> Reconcile(const std::set<uint64_t> &setSenderFilterPositiveCheapHashes);

    // Pass a map of cheap hash to transaction hashes that the local machine has to reconcile with the remote and
    // return a list of cheap hashes in the block in the correct order
    std::vector<uint64_t> Reconcile(const std::map<uint64_t, uint256> &mapCheapHashes);

    static std::vector<uint64_t> Reconcile(const std::map<uint64_t, uint256> &mapCheapHashes,
        std::shared_ptr<CIblt> _pSetIblt,
        std::shared_ptr<CBloomFilter> _pSetFilter,
        std::shared_ptr<CVariableFastFilter> _pFastFilter,
        std::vector<unsigned char> _encodedRank,
        bool _computeOptimized,
        bool _ordered);

    static std::vector<uint64_t> Reconcile(const std::set<uint64_t> &setSenderFilterPositiveCheapHashes,
        const CIblt &localIblt,
        std::shared_ptr<CIblt> _pSetIblt,
        std::vector<unsigned char> _encodedRank,
        bool _ordered);

    static GrapheneSetOptimizationParams DetermineGrapheneSetOptimizationParams(uint64_t nReceiverUniverseItems,
        uint64_t nSenderUniverseItems,
        uint64_t nItems,
        uint64_t version);

    static CIblt ConstructIblt(uint64_t nReceiverUniverseItems,
        double optSymDiff,
        double bloomFPR,
        uint32_t ibltSalt,
        uint64_t graphenSetVersion,
        uint64_t nOverrideValue);

    static std::vector<unsigned char> EncodeRank(std::vector<uint64_t> items, uint16_t nBitsPerItem);

    static std::vector<uint64_t> DecodeRank(std::vector<unsigned char> encoded, size_t nItems, uint16_t nBitsPerItem);

    static double BloomFalsePositiveRate(double optSymDiff, uint64_t nReceiverExcessItems);

    static double TruePositiveMargin(uint64_t nSenderFilterPositiveItems,
        uint64_t nReceiverUniverseItems,
        double senderBloomFpr,
        uint64_t nLowerBoundTruePositives);

    static double TruePositiveProbability(uint64_t nSenderFilterPositiveItems,
        uint64_t nReceiverUniverseItems,
        double senderBloomFpr,
        uint64_t nLowerBoundTruePositives);

    static uint64_t LowerBoundTruePositives(uint64_t nTargetItems,
        uint64_t nSenderFilterPositiveItems,
        uint64_t nReceiverUniverseItems,
        double senderBloomFpr,
        double successRate);

    static double FalsePositiveMargin(uint64_t nLowerBoundTruePositives,
        uint64_t nReceiverUniverseItems,
        double senderBloomFpr,
        double successRate);

    static uint64_t UpperBoundFalsePositives(uint64_t nTargetItems,
        uint64_t nSenderFilterPositiveItems,
        uint64_t nReceiverUniverseItems,
        double senderBloomFpr,
        double successRate);

    CVariableFastFilter FailureRecoveryFilter(std::vector<uint256> &relevantHashes,
        uint64_t nItems,
        uint64_t nSenderFilterPositiveItems,
        uint64_t nReceiverRevisedUniverseItems,
        double successRate,
        double senderBloomFpr,
        uint64_t grapheneSetVersion);

    CIblt FailureRecoveryIblt(std::set<uint64_t> &relevantCheapHashes,
        uint64_t nItems,
        uint64_t nSenderFilterPositiveItems,
        uint64_t nReceiverRevisedUniverseItems,
        double successRate,
        double senderBloomFpr,
        uint64_t grapheneSetVersion,
        uint32_t ibltSaltRevised);

    /* This method calculates the number of bits required for the IBLT cell checksum in order to
     * achieve unchecked error tolerance fUncheckedErrorTol. Details can be found at the link below.
     * https://github.com/bissias/graphene-experiments/blob/master/jupyter/min_checksum_IBLT.ipynb
     */
    static uint8_t NChecksumBits(size_t nIbltEntries,
        uint8_t nIbltHashFuncs,
        uint64_t nReceiverUniverseItems,
        double bloomFPR,
        double fUncheckedErrorTol);

    uint64_t GetFilterSerializationSize()
    {
        if (computeOptimized)
            return ::GetSerializeSize(*pFastFilter, SER_NETWORK, PROTOCOL_VERSION);
        else
            return ::GetSerializeSize(*pSetFilter, SER_NETWORK, PROTOCOL_VERSION);
    }
    uint64_t GetIbltSerializationSize() { return ::GetSerializeSize(*pSetIblt, SER_NETWORK, PROTOCOL_VERSION); }
    uint64_t GetRankSerializationSize() { return ::GetSerializeSize(encodedRank, SER_NETWORK, PROTOCOL_VERSION); }
    ~CGrapheneSet()
    {
        pFastFilter = nullptr;
        pSetFilter = nullptr;
        pSetIblt = nullptr;
    }

    static inline uint64_t GetCIbltVersion(uint64_t grapheneSetVersion)
    {
        if (grapheneSetVersion < 2)
            return 0;
        else if (grapheneSetVersion < 4)
            return 1;
        else
            return 2;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(ordered);
        READWRITE(nReceiverUniverseItems);
        if (nReceiverUniverseItems > LARGE_MEM_POOL_SIZE)
            throw std::runtime_error("nReceiverUniverseItems exceeds threshold for excessive mempool size");
        if (version > 0)
        {
            READWRITE(shorttxidk0);
            READWRITE(shorttxidk1);
        }
        if (version >= 2)
            READWRITE(ibltSalt);
        READWRITE(encodedRank);
        if (version >= 3 && computeOptimized)
        {
            if (!pFastFilter)
                pFastFilter = std::make_shared<CVariableFastFilter>(CVariableFastFilter());

            READWRITE(*pFastFilter);
        }
        else
        {
            if (!pSetFilter)
                pSetFilter = std::make_shared<CBloomFilter>(CBloomFilter());

            READWRITE(*pSetFilter);
        }
        if (!pSetIblt)
            pSetIblt = std::make_shared<CIblt>(CIblt(CGrapheneSet::GetCIbltVersion(version)));
        READWRITE(*pSetIblt);
        if (version >= 5)
            READWRITE(bloomFPR);
    }
};

#endif // BITCOIN_GRAPHENE_SET_H
