// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRAPHENE_SET_H
#define BITCOIN_GRAPHENE_SET_H

#include "bloom.h"
#include "hash.h"
#include "iblt.h"
#include "random.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

#include <cmath>
#include <numeric>

#define LN2SQUARED 0.4804530139182014246671025263266649717305529515945455

const uint8_t FILTER_CELL_SIZE = 1;
const uint8_t IBLT_CELL_SIZE = 17;
const uint32_t LARGE_MEM_POOL_SIZE = 10000000;
const float FILTER_FPR_MAX = 0.999;
const uint8_t IBLT_CELL_MINIMUM = 2;
const std::vector<uint8_t> IBLT_NULL_VALUE = {};
const unsigned char WORD_BITS = 8;
const uint16_t APPROX_ITEMS_THRESH = 600;
const uint8_t APPROX_EXCESS_RATE = 4;
const float IBLT_DEFAULT_OVERHEAD = 1.5;


class CGrapheneSet
{
private:
    bool ordered;
    uint64_t nReceiverUniverseItems;
    mutable uint64_t shorttxidk0, shorttxidk1;
    uint64_t version;
    uint32_t ibltSalt;
    std::vector<unsigned char> encodedRank;
    CBloomFilter *pSetFilter;
    CIblt *pSetIblt;

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
          pSetFilter(nullptr), pSetIblt(nullptr)
    {
    }
    CGrapheneSet(uint64_t _version)
        : ordered(false), nReceiverUniverseItems(0), shorttxidk0(0), shorttxidk1(0), ibltSalt(0), pSetFilter(nullptr),
          pSetIblt(nullptr)
    {
        version = _version;
    }
    CGrapheneSet(size_t _nReceiverUniverseItems,
        uint64_t nSenderUniverseItems,
        const std::vector<uint256> &_itemHashes,
        uint64_t _shorttxidk0,
        uint64_t _shorttxidk1,
        uint64_t _version = 1,
        uint32_t ibltEntropy = 0,
        bool _ordered = false,
        bool fDeterministic = false);

    // Generate cheap hash from seeds using SipHash
    uint64_t GetShortID(const uint256 &txhash) const;

    /* Optimal symmetric difference between block txs and receiver mempool txs passing
     * though filter to use for IBLT.
     */
    double OptimalSymDiff(uint64_t nBlockTxs,
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
    double ApproxOptimalSymDiff(uint64_t nBlockTxs);

    /* Brute force search for optimal symmetric difference between block txs and receiver
     * mempool txs passing though filter to use for IBLT.
     *
     * Let a be defined as the size of the symmetric difference between items in the
     * sender and receiver IBLTs.
     *
     * The total size in bytes of a graphene block is given by T(a) = F(a) + L(a) as defined
     * in the code below. (Note that meta parameters for the Bloom Filter and IBLT are ignored).
     */
    double BruteForceSymDiff(uint64_t nBlockTxs,
        uint64_t nReceiverPoolTx,
        uint64_t nReceiverExcessTxs,
        uint64_t nReceiverMissingTxs);

    // Pass the transaction hashes that the local machine has to reconcile with the remote and return a list
    // of cheap hashes in the block in the correct order
    std::vector<uint64_t> Reconcile(const std::vector<uint256> &receiverItemHashes);

    // Pass a map of cheap hash to transaction hashes that the local machine has to reconcile with the remote and
    // return a list of cheap hashes in the block in the correct order
    std::vector<uint64_t> Reconcile(const std::map<uint64_t, uint256> &mapCheapHashes);

    std::vector<uint64_t> Reconcile(std::set<uint64_t> &receiverSet, const CIblt &localIblt);

    static std::vector<unsigned char> EncodeRank(std::vector<uint64_t> items, uint16_t nBitsPerItem);

    static std::vector<uint64_t> DecodeRank(std::vector<unsigned char> encoded, size_t nItems, uint16_t nBitsPerItem);

    uint64_t GetFilterSerializationSize() { return ::GetSerializeSize(*pSetFilter, SER_NETWORK, PROTOCOL_VERSION); }
    uint64_t GetIbltSerializationSize() { return ::GetSerializeSize(*pSetIblt, SER_NETWORK, PROTOCOL_VERSION); }
    uint64_t GetRankSerializationSize() { return ::GetSerializeSize(encodedRank, SER_NETWORK, PROTOCOL_VERSION); }
    ~CGrapheneSet()
    {
        if (pSetFilter)
        {
            delete pSetFilter;
            pSetFilter = nullptr;
        }

        if (pSetIblt)
        {
            delete pSetIblt;
            pSetIblt = nullptr;
        }
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(ordered);
        READWRITE(nReceiverUniverseItems);
        if (version > 0)
        {
            READWRITE(shorttxidk0);
            READWRITE(shorttxidk1);
        }
        if (version >= 2)
            READWRITE(ibltSalt);
        if (nReceiverUniverseItems > LARGE_MEM_POOL_SIZE)
            throw std::runtime_error("nReceiverUniverseItems exceeds threshold for excessive mempool size");
        READWRITE(encodedRank);
        if (!pSetFilter)
            pSetFilter = new CBloomFilter();
        READWRITE(*pSetFilter);
        if (!pSetIblt)
            pSetIblt = new CIblt();
        READWRITE(*pSetIblt);
    }
};

#endif // BITCOIN_GRAPHENE_SET_H
