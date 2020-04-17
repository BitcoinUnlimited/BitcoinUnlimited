// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/graphene_set.h"
#include "bloom.h"
#include "hashwrapper.h"
#include "iblt.h"
#include "random.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

#include <cfenv>
#include <cmath>
#include <numeric>

extern CTweak<uint64_t> grapheneIbltSizeOverride;
extern CTweak<double> grapheneBloomFprOverride;

CGrapheneSet::CGrapheneSet(size_t _nReceiverUniverseItems,
    uint64_t nSenderUniverseItems,
    const std::vector<uint256> &_itemHashes,
    uint64_t _shorttxidk0,
    uint64_t _shorttxidk1,
    uint64_t _version,
    uint32_t ibltEntropy,
    bool _computeOptimized,
    bool _ordered,
    bool fDeterministic)
    : ordered(_ordered), nReceiverUniverseItems(_nReceiverUniverseItems), shorttxidk0(_shorttxidk0),
      shorttxidk1(_shorttxidk1), version(_version), ibltSalt(ibltEntropy), computeOptimized(_computeOptimized),
      pSetFilter(nullptr), pFastFilter(nullptr), pSetIblt(nullptr), bloomFPR(1.0)
{
    // Below is the parameter "n" from the graphene paper
    uint64_t nItems = _itemHashes.size();

    FastRandomContext insecure_rand(fDeterministic);

    GrapheneSetOptimizationParams params =
        DetermineGrapheneSetOptimizationParams(nReceiverUniverseItems, nSenderUniverseItems, nItems, version);
    double optSymDiff = params.optSymDiff;
    bloomFPR = params.bloomFPR;

    // For testing stage 2, allow FPR to be set to specific value
    if (grapheneBloomFprOverride.Value() > 0.0)
        bloomFPR = grapheneBloomFprOverride.Value();

    // Construct Bloom filter
    if (computeOptimized)
    {
        LOG(GRAPHENE, "using compute-optimized Bloom filter\n");
        pFastFilter = std::make_shared<CVariableFastFilter>(CVariableFastFilter(nItems, bloomFPR));
    }
    else
    {
        LOG(GRAPHENE, "using regular Bloom filter\n");
        pSetFilter = std::make_shared<CBloomFilter>(CBloomFilter(
            nItems, bloomFPR, insecure_rand.rand32(), BLOOM_UPDATE_ALL, true, std::numeric_limits<uint32_t>::max()));
    }
    LOG(GRAPHENE, "fp rate: %f Num elements in bloom filter: %d\n", bloomFPR, nItems);

    pSetIblt = std::make_shared<CIblt>(CGrapheneSet::ConstructIblt(
        nReceiverUniverseItems, optSymDiff, bloomFPR, ibltSalt, version, grapheneIbltSizeOverride.Value()));

    std::map<uint64_t, uint256> mapCheapHashes;

    for (const uint256 &itemHash : _itemHashes)
    {
        uint64_t cheapHash = GetShortID(itemHash);

        if (computeOptimized)
        {
            pFastFilter->insert(itemHash);
        }
        else
        {
            pSetFilter->insert(itemHash);
        }

        if (mapCheapHashes.count(cheapHash))
            throw std::runtime_error("Cheap hash collision while encoding graphene set");

        pSetIblt->insert(cheapHash, IBLT_NULL_VALUE);
        mapCheapHashes[cheapHash] = itemHash;
    }

    // Record transaction order
    if (ordered)
    {
        std::map<uint256, uint64_t> mapItemHashes;
        for (const std::pair<uint64_t, uint256> &kv : mapCheapHashes)
            mapItemHashes[kv.second] = kv.first;

        mapCheapHashes.clear();

        std::vector<uint64_t> cheapHashes;
        for (const uint256 &itemHash : _itemHashes)
            cheapHashes.push_back(mapItemHashes[itemHash]);

        std::vector<uint64_t> sortedIdxs = ArgSort(cheapHashes);
        uint8_t nBits = ceil(log2(cheapHashes.size()));

        encodedRank = CGrapheneSet::EncodeRank(sortedIdxs, nBits);
    }
}


uint64_t CGrapheneSet::GetShortID(const uint256 &txhash) const
{
    if (version == 0)
        return txhash.GetCheapHash();

    static_assert(SHORTTXIDS_LENGTH == 8, "shorttxids calculation assumes 8-byte shorttxids");
    return SipHashUint256(shorttxidk0, shorttxidk1, txhash) & 0xffffffffffffffL;
}


double CGrapheneSet::OptimalSymDiff(uint64_t version,
    uint64_t nBlockTxs,
    uint64_t nReceiverPoolTx,
    uint64_t nReceiverExcessTxs,
    uint64_t nReceiverMissingTxs)
{
    /* Optimal symmetric difference between block txs and receiver mempool txs passing
     * though filter to use for IBLT.
     */
    uint16_t approx_items_thresh = version >= 4 ? APPROX_ITEMS_THRESH_REDUCE_CHECK : APPROX_ITEMS_THRESH;

    // First calculate optimal symmetric difference assuming the maximum number of checksum bits
    double optSymDiff;
    if (nBlockTxs >= approx_items_thresh && nReceiverExcessTxs >= nBlockTxs / APPROX_EXCESS_RATE)
        optSymDiff = CGrapheneSet::ApproxOptimalSymDiff(version, nBlockTxs, MAX_CHECKSUM_BITS);
    else
        optSymDiff = CGrapheneSet::BruteForceSymDiff(
            nBlockTxs, nReceiverPoolTx, nReceiverExcessTxs, nReceiverMissingTxs, MAX_CHECKSUM_BITS);

    if (version < 4)
        return optSymDiff;

    // Calculate optimal number of checksum bits assuming optimal symmetric difference
    uint64_t nIbltCells = std::max((int)IBLT_CELL_MINIMUM, (int)ceil(optSymDiff));
    uint8_t nChecksumBits =
        CGrapheneSet::NChecksumBits(nIbltCells * CIblt::OptimalOverhead(nIbltCells), CIblt::OptimalNHash(nIbltCells),
            nReceiverPoolTx, CGrapheneSet::BloomFalsePositiveRate(optSymDiff, nReceiverExcessTxs), UNCHECKED_ERROR_TOL);

    // Recalculate optimal symmetric difference assuming optimal checksum bits
    if (nBlockTxs >= approx_items_thresh && nReceiverExcessTxs >= nBlockTxs / APPROX_EXCESS_RATE)
        return CGrapheneSet::ApproxOptimalSymDiff(version, nBlockTxs, nChecksumBits);
    else
        return CGrapheneSet::BruteForceSymDiff(
            nBlockTxs, nReceiverPoolTx, nReceiverExcessTxs, nReceiverMissingTxs, nChecksumBits);
}


double CGrapheneSet::ApproxOptimalSymDiff(uint64_t version, uint64_t nBlockTxs, uint8_t nChecksumBits)
{
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
    if (version >= 4)
        assert(nBlockTxs >= APPROX_ITEMS_THRESH_REDUCE_CHECK);
    else
        assert(nBlockTxs >= APPROX_ITEMS_THRESH);

    return std::max(1.0, std::round(FILTER_CELL_SIZE * nBlockTxs /
                                    ((nChecksumBits + 8 * IBLT_FIXED_CELL_SIZE) * IBLT_DEFAULT_OVERHEAD * LN2SQUARED)));
}


double CGrapheneSet::BruteForceSymDiff(uint64_t nBlockTxs,
    uint64_t nReceiverPoolTx,
    uint64_t nReceiverExcessTxs,
    uint64_t nReceiverMissingTxs,
    uint8_t nChecksumBits)
{
    /* Brute force search for optimal symmetric difference between block txs and receiver
     * mempool txs passing though filter to use for IBLT.
     *
     * Let a be defined as the size of the symmetric difference between items in the
     * sender and receiver IBLTs.
     *
     * The total size in bytes of a graphene block is given by T(a) = F(a) + L(a) as defined
     * in the code below. (Note that meta parameters for the Bloom Filter and IBLT are ignored).
     */
    assert(nReceiverExcessTxs <= nReceiverPoolTx); // Excess contained in mempool
    assert(nReceiverMissingTxs <= nBlockTxs); // Can't be missing more txs than are in block

    if (nReceiverPoolTx > LARGE_MEM_POOL_SIZE)
        throw std::runtime_error("Receiver mempool is too large for optimization");

    auto fpr = [nReceiverExcessTxs](uint64_t a) {
        if (nReceiverExcessTxs == 0)
            return FILTER_FPR_MAX;

        float _fpr = a / float(nReceiverExcessTxs);

        return _fpr < FILTER_FPR_MAX ? _fpr : FILTER_FPR_MAX;
    };

    auto F = [nBlockTxs, fpr](
        uint64_t a) { return floor(FILTER_CELL_SIZE * (-1 / LN2SQUARED * nBlockTxs * log(fpr(a)) / 8)); };

    auto L = [nChecksumBits](uint64_t a) {
        uint8_t n_iblt_hash = CIblt::OptimalNHash(a);
        float iblt_overhead = CIblt::OptimalOverhead(a);
        uint64_t padded_cells = (int)(iblt_overhead * a);
        uint64_t cells = n_iblt_hash * int(ceil(padded_cells / float(n_iblt_hash)));

        return (nChecksumBits / 8 + IBLT_FIXED_CELL_SIZE) * cells;
    };

    uint64_t optSymDiff = 1;
    double optT = std::numeric_limits<double>::max();
    for (uint64_t a = 1; a < nReceiverPoolTx; a++)
    {
        double T = F(a) + L(a);

        if (T < optT)
        {
            optSymDiff = a;
            optT = T;
        }
    }

    return optSymDiff;
}


// Pass the transaction hashes that the local machine has to reconcile with the remote and return a list
// of cheap hashes in the block in the correct order
std::vector<uint64_t> CGrapheneSet::Reconcile(const std::vector<uint256> &receiverItemHashes)
{
    std::set<uint64_t> receiverSet;
    std::map<uint64_t, uint256> mapCheapHashes;
    CIblt localIblt((*pSetIblt));
    localIblt.reset();

    int passedFilter = 0;
    for (const uint256 &itemHash : receiverItemHashes)
    {
        uint64_t cheapHash = GetShortID(itemHash);

        auto ir = mapCheapHashes.insert(std::make_pair(cheapHash, itemHash));
        if (!ir.second)
        {
            throw std::runtime_error("Cheap hash collision while decoding graphene set");
        }

        if ((computeOptimized && pFastFilter->contains(itemHash)) ||
            (!computeOptimized && pSetFilter->contains(itemHash)))
        {
            receiverSet.insert(cheapHash);
            localIblt.insert(cheapHash, IBLT_NULL_VALUE);
            passedFilter += 1;
        }
    }
    LOG(GRAPHENE, "%d txs passed receiver Bloom filter\n", passedFilter);

    mapCheapHashes.clear();
    return CGrapheneSet::Reconcile(receiverSet, localIblt, this->GetIblt(), GetEncodedRank(), GetOrdered());
}

// This version assumes that we know the set that have passed through the sender Bloom filter
std::vector<uint64_t> CGrapheneSet::Reconcile(const std::set<uint64_t> &setSenderFilterPositiveCheapHashes)
{
    CIblt localIblt((*pSetIblt));
    localIblt.reset();

    for (const auto &cheapHash : setSenderFilterPositiveCheapHashes)
    {
        localIblt.insert(cheapHash, IBLT_NULL_VALUE);
    }

    return Reconcile(setSenderFilterPositiveCheapHashes, localIblt, pSetIblt, encodedRank, ordered);
}

// Pass a map of cheap hash to transaction hashes that the local machine has to reconcile with the remote and
// return a list of cheap hashes in the block in the correct order
std::vector<uint64_t> CGrapheneSet::Reconcile(const std::map<uint64_t, uint256> &mapCheapHashes)
{
    return CGrapheneSet::Reconcile(mapCheapHashes, this->GetIblt(), this->GetRegularFilter(), this->GetFastFilter(),
        this->GetEncodedRank(), this->GetComputeOptimized(), this->GetOrdered());
}

std::vector<uint64_t> CGrapheneSet::Reconcile(const std::map<uint64_t, uint256> &mapCheapHashes,
    std::shared_ptr<CIblt> _pSetIblt,
    std::shared_ptr<CBloomFilter> _pSetFilter,
    std::shared_ptr<CVariableFastFilter> _pFastFilter,
    std::vector<unsigned char> _encodedRank,
    bool _computeOptimized,
    bool _ordered)
{
    std::set<uint64_t> receiverSet;
    CIblt localIblt((*_pSetIblt));
    localIblt.reset();

    for (const auto &entry : mapCheapHashes)
    {
        if ((_computeOptimized && _pFastFilter->contains(entry.second)) ||
            (!_computeOptimized && _pSetFilter->contains(entry.second)))
        {
            receiverSet.insert(entry.first);
            localIblt.insert(entry.first, IBLT_NULL_VALUE);
        }
    }

    return Reconcile(receiverSet, localIblt, _pSetIblt, _encodedRank, _ordered);
}

std::vector<uint64_t> CGrapheneSet::Reconcile(const std::set<uint64_t> &setSenderFilterPositiveCheapHashes,
    const CIblt &localIblt,
    std::shared_ptr<CIblt> _pSetIblt,
    std::vector<unsigned char> _encodedRank,
    bool _ordered)
{
    std::set<uint64_t> receiverSet = std::set<uint64_t>(setSenderFilterPositiveCheapHashes);
    // Determine difference between sender and receiver IBLTs
    std::set<std::pair<uint64_t, std::vector<uint8_t> > > senderHas;
    std::set<std::pair<uint64_t, std::vector<uint8_t> > > receiverHas;

    if (!((*_pSetIblt) - localIblt).listEntries(senderHas, receiverHas))
        throw std::runtime_error("Graphene set IBLT did not decode");

    LOG(GRAPHENE, "senderHas: %d, receiverHas: %d\n", senderHas.size(), receiverHas.size());

    // Remove false positives from receiverSet
    for (const std::pair<uint64_t, std::vector<uint8_t> > &kv : receiverHas)
        receiverSet.erase(kv.first);

    // Restore missing items recovered from sender
    for (const std::pair<uint64_t, std::vector<uint8_t> > &kv : senderHas)
        receiverSet.insert(kv.first);

    std::vector<uint64_t> receiverSetItems(receiverSet.begin(), receiverSet.end());

    if (!_ordered)
        return receiverSetItems;

    // Place items in order
    uint8_t nBits = ceil(log2(receiverSetItems.size()));
    std::vector<uint64_t> itemRank = CGrapheneSet::DecodeRank(_encodedRank, receiverSetItems.size(), nBits);
    std::sort(receiverSetItems.begin(), receiverSetItems.end(), [](uint64_t i1, uint64_t i2) { return i1 < i2; });
    std::vector<uint64_t> orderedSetItems(itemRank.size(), 0);
    for (size_t i = 0; i < itemRank.size(); i++)
        orderedSetItems[itemRank[i]] = receiverSetItems[i];

    return orderedSetItems;
}

double CGrapheneSet::TruePositiveMargin(uint64_t nSenderFilterPositiveItems,
    uint64_t nReceiverUniverseItems,
    double senderBloomFpr,
    uint64_t nLowerBoundTruePositives)
{
    double denominator = (nReceiverUniverseItems - nLowerBoundTruePositives) * senderBloomFpr;

    if (denominator == 0.0)
        return 0.0;

    return (nSenderFilterPositiveItems - nLowerBoundTruePositives) / denominator - 1;
}

double CGrapheneSet::TruePositiveProbability(uint64_t nSenderFilterPositiveItems,
    uint64_t nReceiverUniverseItems,
    double senderBloomFpr,
    uint64_t nLowerBoundTruePositives)
{
    double margin = TruePositiveMargin(
        nSenderFilterPositiveItems, nReceiverUniverseItems, senderBloomFpr, nLowerBoundTruePositives);
    double margin_plus_1 = margin + 1;

    std::feclearexcept(FE_ALL_EXCEPT);
    double denominator = 1.0;
    if (margin_plus_1 != 0)
        denominator = pow(margin_plus_1, margin_plus_1);

    if (denominator == 0.0)
        return 0.0;

    double probability =
        pow(exp(margin) / denominator, (nReceiverUniverseItems - nLowerBoundTruePositives) * senderBloomFpr);

    if (std::fetestexcept(FE_DIVBYZERO) || std::fetestexcept(FE_OVERFLOW) || std::fetestexcept(FE_UNDERFLOW) ||
        std::fetestexcept(FE_INVALID))
        return 0.0;

    return probability;
}

uint64_t CGrapheneSet::LowerBoundTruePositives(uint64_t nTargetItems,
    uint64_t nSenderFilterPositiveItems,
    uint64_t nReceiverUniverseItems,
    double senderBloomFpr,
    double successRate)
{
    // x* in the graphene paper

    if (nSenderFilterPositiveItems == 0)
        return 0;

    uint64_t nLowerBoundTruePositives = 0;
    double prob = TruePositiveProbability(
        nSenderFilterPositiveItems, nReceiverUniverseItems, senderBloomFpr, nLowerBoundTruePositives);

    while (prob <= (1 - successRate) &&
           ((int)nLowerBoundTruePositives <= (int)std::min(nSenderFilterPositiveItems, nTargetItems)))
    {
        nLowerBoundTruePositives += 1;
        prob += TruePositiveProbability(
            nSenderFilterPositiveItems, nReceiverUniverseItems, senderBloomFpr, nLowerBoundTruePositives);
    }

    return (uint64_t)std::max(0, (int)(nLowerBoundTruePositives - 1));
}

double CGrapheneSet::FalsePositiveMargin(uint64_t nLowerBoundTruePositives,
    uint64_t nReceiverUniverseItems,
    double senderBloomFpr,
    double successRate)
{
    // delta in the graphene paper

    double denominator = (nReceiverUniverseItems - nLowerBoundTruePositives) * senderBloomFpr;
    if (denominator == 0.0)
        return 0.0;

    std::feclearexcept(FE_ALL_EXCEPT);
    double log_b = log(1 - successRate);
    double s = -log_b / denominator;
    double result = 0.5 * (s + sqrt(pow(s, 2) + 8 * s));

    if (std::fetestexcept(FE_DIVBYZERO) || std::fetestexcept(FE_OVERFLOW) || std::fetestexcept(FE_UNDERFLOW) ||
        std::fetestexcept(FE_INVALID))
        return 0.0;

    return result;
}

uint64_t CGrapheneSet::UpperBoundFalsePositives(uint64_t nTargetItems,
    uint64_t nSenderFilterPositiveItems,
    uint64_t nReceiverUniverseItems,
    double senderBloomFpr,
    double successRate)
{
    // y* in the graphene paper
    uint64_t nLowerBoundTruePositives = LowerBoundTruePositives(
        nTargetItems, nSenderFilterPositiveItems, nReceiverUniverseItems, senderBloomFpr, successRate);
    double margin = FalsePositiveMargin(nLowerBoundTruePositives, nReceiverUniverseItems, senderBloomFpr, successRate);

    return (uint64_t)std::min((double)nSenderFilterPositiveItems,
        (1 + margin) * (nReceiverUniverseItems - nLowerBoundTruePositives) * senderBloomFpr);
}

CVariableFastFilter CGrapheneSet::FailureRecoveryFilter(std::vector<uint256> &relevantHashes,
    uint64_t nItems,
    uint64_t nSenderFilterPositiveItems,
    uint64_t nReceiverRevisedUniverseItems,
    double successRate,
    double senderBloomFpr,
    uint64_t grapheneSetVersion)
{
    uint64_t nLowerBoundTruePositives = LowerBoundTruePositives(
        nItems, nSenderFilterPositiveItems, nReceiverRevisedUniverseItems, senderBloomFpr, successRate);
    GrapheneSetOptimizationParams params = DetermineGrapheneSetOptimizationParams(
        nSenderFilterPositiveItems, nItems, nLowerBoundTruePositives, grapheneSetVersion);
    CVariableFastFilter filter(relevantHashes.size(), params.bloomFPR);

    for (auto &hash : relevantHashes)
        filter.insert(hash);

    return filter;
}

CIblt CGrapheneSet::FailureRecoveryIblt(std::set<uint64_t> &relevantCheapHashes,
    uint64_t nItems,
    uint64_t nSenderFilterPositiveItems,
    uint64_t nReceiverRevisedUniverseItems,
    double successRate,
    double senderBloomFpr,
    uint64_t grapheneSetVersion,
    uint32_t ibltSaltRevised)
{
    GrapheneSetOptimizationParams params = DetermineGrapheneSetOptimizationParams(
        nSenderFilterPositiveItems, nItems, relevantCheapHashes.size(), grapheneSetVersion);
    uint64_t nUpperBoundFalsePositives = UpperBoundFalsePositives(
        nItems, nSenderFilterPositiveItems, nReceiverRevisedUniverseItems, senderBloomFpr, successRate);
    CIblt iblt = CGrapheneSet::ConstructIblt(nReceiverRevisedUniverseItems,
        params.optSymDiff + nUpperBoundFalsePositives, params.bloomFPR, ibltSaltRevised, version, 0);

    for (auto &cheapHash : relevantCheapHashes)
    {
        iblt.insert(cheapHash, IBLT_NULL_VALUE);
    }

    return iblt;
}

std::vector<unsigned char> CGrapheneSet::EncodeRank(std::vector<uint64_t> items, uint16_t nBitsPerItem)
{
    size_t nItems = items.size();
    size_t nEncodedWords = int(ceil(nBitsPerItem * nItems / float(WORD_BITS)));
    std::vector<unsigned char> encoded(nEncodedWords, 0);

    // form boolean array (low-order first)
    std::unique_ptr<bool[]> bits(new bool[nEncodedWords * WORD_BITS]);
    for (size_t i = 0; i < items.size(); i++)
    {
        uint64_t item = items[i];

        assert(ceil(log2(item)) <= nBitsPerItem);

        for (uint16_t j = 0; j < nBitsPerItem; j++)
            bits[j + i * nBitsPerItem] = (item >> j) & 1;
    }

    // encode boolean array
    for (size_t i = 0; i < nEncodedWords; i++)
    {
        encoded[i] = 0;
        for (size_t j = 0; j < WORD_BITS; j++)
            encoded[i] |= bits[j + i * WORD_BITS] << j;
    }

    return encoded;
}

std::vector<uint64_t> CGrapheneSet::DecodeRank(std::vector<unsigned char> encoded, size_t nItems, uint16_t nBitsPerItem)
{
    size_t nEncodedWords = int(ceil(nBitsPerItem * nItems / float(WORD_BITS)));

    // decode into boolean array (low-order first)
    std::unique_ptr<bool[]> bits(new bool[nEncodedWords * WORD_BITS]);

    for (size_t i = 0; i < nEncodedWords; i++)
    {
        unsigned char word = encoded[i];

        for (size_t j = 0; j < WORD_BITS; j++)
            bits[j + i * WORD_BITS] = (word >> j) & 1;
    }

    // convert boolean to item array
    std::vector<uint64_t> items(nItems, 0);
    for (size_t i = 0; i < nItems; i++)
    {
        for (size_t j = 0; j < nBitsPerItem; j++)
            items[i] |= bits[j + i * nBitsPerItem] << j;
    }
    return items;
}

double CGrapheneSet::BloomFalsePositiveRate(double optSymDiff, uint64_t nReceiverExcessItems)
{
    double fpr;
    if (optSymDiff >= nReceiverExcessItems || nReceiverExcessItems == 0)
        fpr = FILTER_FPR_MAX;
    else
        fpr = optSymDiff / float(nReceiverExcessItems);

    return fpr;
}

GrapheneSetOptimizationParams CGrapheneSet::DetermineGrapheneSetOptimizationParams(uint64_t nReceiverUniverseItems,
    uint64_t nSenderUniverseItems,
    uint64_t nItems,
    uint64_t version)
{
    // Infer various receiver quantities
    uint64_t nReceiverExcessItems =
        std::max((int)(nReceiverUniverseItems - nItems), (int)(nSenderUniverseItems - nItems));
    nReceiverExcessItems = std::max(0, (int)nReceiverExcessItems); // must be non-negative
    nReceiverExcessItems =
        std::min((int)nReceiverUniverseItems, (int)nReceiverExcessItems); // must not exceed total mempool size
    uint64_t nReceiverMissingItems = std::max(1, (int)(nItems - (nReceiverUniverseItems - nReceiverExcessItems)));

    LOG(GRAPHENE, "receiver expected to have at most %d excess txs in mempool\n", nReceiverExcessItems);
    LOG(GRAPHENE, "receiver expected to be missing at most %d txs from block\n", nReceiverMissingItems);

    if (nItems == 0)
    {
        GrapheneSetOptimizationParams params = {
            nReceiverExcessItems, nReceiverMissingItems, (double)nReceiverMissingItems, FILTER_FPR_MAX};

        return params;
    }

    // Optimal symmetric differences between receiver and sender IBLTs
    // This is the parameter "a" from the graphene paper
    double optSymDiff = nReceiverMissingItems;
    try
    {
        if (nItems <= nReceiverUniverseItems + nReceiverMissingItems)
            optSymDiff = CGrapheneSet::OptimalSymDiff(
                version, nItems, nReceiverUniverseItems, nReceiverExcessItems, nReceiverMissingItems);
    }
    catch (const std::runtime_error &e)
    {
        LOG(GRAPHENE, "failed to optimize symmetric difference for graphene: %s\n", e.what());
    }

    // Set false positive rate for Bloom filter based on optSymDiff
    double bloomFPR = CGrapheneSet::BloomFalsePositiveRate(optSymDiff, nReceiverExcessItems);

    // So far we have only made room for false positives in the IBLT
    // Make more room for missing items
    optSymDiff += nReceiverMissingItems;

    GrapheneSetOptimizationParams params = {nReceiverExcessItems, nReceiverMissingItems, optSymDiff, bloomFPR};

    return params;
}

CIblt CGrapheneSet::ConstructIblt(uint64_t nReceiverUniverseItems,
    double optSymDiff,
    double bloomFPR,
    uint32_t ibltSalt,
    uint64_t graphenSetVersion,
    uint64_t nOverrideValue)
{
    uint64_t ibltVersion = CGrapheneSet::GetCIbltVersion(graphenSetVersion);
    uint64_t nIbltCells = std::max((int)IBLT_CELL_MINIMUM, (int)ceil(optSymDiff));

    // For testing stage 2, allow IBLT size to be set to specific value
    if (nOverrideValue > 0)
        nIbltCells = nOverrideValue;

    uint8_t nCheckSumBits;
    if (ibltVersion >= 2)
    {
        nCheckSumBits = CGrapheneSet::NChecksumBits(nIbltCells * CIblt::OptimalOverhead(nIbltCells),
            CIblt::OptimalNHash(nIbltCells), nReceiverUniverseItems, bloomFPR, UNCHECKED_ERROR_TOL);
    }
    else
        nCheckSumBits = MAX_CHECKSUM_BITS;

    LOG(GRAPHENE, "using %d checksum bits in IBLT\n", nCheckSumBits);
    uint32_t keycheckMask = MAX_CHECKSUM_MASK >> (MAX_CHECKSUM_BITS - nCheckSumBits);

    CIblt iblt = CIblt(nIbltCells, ibltSalt, ibltVersion, keycheckMask);

    LOG(GRAPHENE, "constructed IBLT with %d cells\n", nIbltCells);

    return iblt;
}


uint8_t CGrapheneSet::NChecksumBits(size_t nIbltEntries,
    uint8_t nIbltHashFuncs,
    uint64_t nReceiverUniverseItems,
    double bloomFPR,
    double fUncheckedErrorTol)
{
    if (nIbltEntries < nIbltHashFuncs || nIbltEntries == 0)
        return 32;

    return (uint8_t)std::max((double)MIN_CHECKSUM_BITS,
        std::ceil(std::log2(nIbltEntries * (1 - std::pow(1 - bloomFPR * (nIbltHashFuncs / (double)nIbltEntries),
                                                    nReceiverUniverseItems))) -
                                 std::log2(fUncheckedErrorTol)));
}
