// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/graphene_set.h"
#include "bloom.h"
#include "hash.h"
#include "iblt.h"
#include "random.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

#include <cmath>
#include <numeric>

CGrapheneSet::CGrapheneSet(size_t _nReceiverUniverseItems,
    uint64_t nSenderUniverseItems,
    const std::vector<uint256> &_itemHashes,
    bool _ordered,
    bool fDeterministic)
{
    ordered = _ordered;

    // Below is the parameter "m" from the graphene paper
    nReceiverUniverseItems = _nReceiverUniverseItems;

    // Below is the parameter "n" from the graphene paper
    uint64_t nItems = _itemHashes.size();

    FastRandomContext insecure_rand(fDeterministic);

    // Infer various receiver quantities
    uint64_t nReceiverExcessItems =
        std::min((int)nReceiverUniverseItems, std::max(0, (int)(nSenderUniverseItems - nItems)));
    uint64_t nReceiverMissingItems = std::max(1, (int)(nItems - (nReceiverUniverseItems - nReceiverExcessItems)));

    LOG(GRAPHENE, "receiver expected to have at most %d excess txs in mempool\n", nReceiverExcessItems);
    LOG(GRAPHENE, "receiver expected to be missing at most %d txs from block\n", nReceiverMissingItems);

    // Optimal symmetric differences between receiver and sender IBLTs
    // This is the parameter "a" from the graphene paper
    double optSymDiff = std::max(1, (int)nReceiverMissingItems);
    try
    {
        if (nItems <= nReceiverUniverseItems + nReceiverMissingItems)
            optSymDiff = OptimalSymDiff(nItems, nReceiverUniverseItems, nReceiverExcessItems, nReceiverMissingItems);
    }
    catch (const std::runtime_error &e)
    {
        LOG(GRAPHENE, "failed to optimize symmetric difference for graphene: %s\n", e.what());
    }

    // Set false positive rate for Bloom filter based on optSymDiff
    double fpr;
    if (optSymDiff >= nReceiverExcessItems)
        fpr = FILTER_FPR_MAX;
    else
        fpr = optSymDiff / float(nReceiverExcessItems);

    // So far we have only made room for false positives in the IBLT
    // Make more room for missing items
    optSymDiff += nReceiverMissingItems;

    // Construct Bloom filter
    pSetFilter = new CBloomFilter(
        nItems, fpr, insecure_rand.rand32(), BLOOM_UPDATE_ALL, true, std::numeric_limits<uint32_t>::max());
    LOG(GRAPHENE, "fp rate: %f Num elements in bloom filter: %d\n", fpr, nItems);

    // Construct IBLT
    uint64_t nIbltCells = std::max((int)IBLT_CELL_MINIMUM, (int)ceil(optSymDiff));
    pSetIblt = new CIblt(nIbltCells);
    std::map<uint64_t, uint256> mapCheapHashes;

    LOG(GRAPHENE, "constructed IBLT with %d cells\n", nIbltCells);

    for (const uint256 &itemHash : _itemHashes)
    {
        uint64_t cheapHash = itemHash.GetCheapHash();

        pSetFilter->insert(itemHash);

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


double CGrapheneSet::OptimalSymDiff(uint64_t nBlockTxs,
    uint64_t nReceiverPoolTx,
    uint64_t nReceiverExcessTxs,
    uint64_t nReceiverMissingTxs)
{
    /* Optimal symmetric difference between block txs and receiver mempool txs passing
     * though filter to use for IBLT.
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

    auto L = [](uint64_t a) {
        uint8_t n_iblt_hash = CIblt::OptimalNHash(a);
        float iblt_overhead = CIblt::OptimalOverhead(a);
        uint64_t padded_cells = (int)(iblt_overhead * a);
        uint64_t cells = n_iblt_hash * int(ceil(padded_cells / float(n_iblt_hash)));

        return IBLT_CELL_SIZE * cells;
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
        uint64_t cheapHash = itemHash.GetCheapHash();

        auto ir = mapCheapHashes.insert(std::make_pair(cheapHash, itemHash));
        if (!ir.second)
        {
            throw std::runtime_error("Cheap hash collision while decoding graphene set");
        }

        if ((*pSetFilter).contains(itemHash))
        {
            receiverSet.insert(cheapHash);
            localIblt.insert(cheapHash, IBLT_NULL_VALUE);
            passedFilter += 1;
        }
    }
    LOG(GRAPHENE, "%d txs passed receiver Bloom filter\n", passedFilter);

    mapCheapHashes.clear();
    return Reconcile(receiverSet, localIblt);
}

// Pass a map of cheap hash to transaction hashes that the local machine has to reconcile with the remote and
// return a list of cheap hashes in the block in the correct order
std::vector<uint64_t> CGrapheneSet::Reconcile(const std::map<uint64_t, uint256> &mapCheapHashes)
{
    std::set<uint64_t> receiverSet;
    CIblt localIblt((*pSetIblt));
    localIblt.reset();

    for (const auto &entry : mapCheapHashes)
    {
        if ((*pSetFilter).contains(entry.second))
        {
            receiverSet.insert(entry.first);
            localIblt.insert(entry.first, IBLT_NULL_VALUE);
        }
    }

    return Reconcile(receiverSet, localIblt);
}

std::vector<uint64_t> CGrapheneSet::Reconcile(std::set<uint64_t> &receiverSet, const CIblt &localIblt)
{
    // Determine difference between sender and receiver IBLTs
    std::set<std::pair<uint64_t, std::vector<uint8_t> > > senderHas;
    std::set<std::pair<uint64_t, std::vector<uint8_t> > > receiverHas;

    if (!((*pSetIblt) - localIblt).listEntries(senderHas, receiverHas))
        throw std::runtime_error("Graphene set IBLT did not decode");

    LOG(GRAPHENE, "senderHas: %d, receiverHas: %d\n", senderHas.size(), receiverHas.size());

    // Remove false positives from receiverSet
    for (const std::pair<uint64_t, std::vector<uint8_t> > &kv : receiverHas)
        receiverSet.erase(kv.first);

    // Restore missing items recovered from sender
    for (const std::pair<uint64_t, std::vector<uint8_t> > &kv : senderHas)
        receiverSet.insert(kv.first);

    std::vector<uint64_t> receiverSetItems(receiverSet.begin(), receiverSet.end());

    if (!ordered)
        return receiverSetItems;

    // Place items in order
    uint8_t nBits = ceil(log2(receiverSetItems.size()));
    std::vector<uint64_t> itemRank = CGrapheneSet::DecodeRank(encodedRank, receiverSetItems.size(), nBits);
    std::sort(receiverSetItems.begin(), receiverSetItems.end(), [](uint64_t i1, uint64_t i2) { return i1 < i2; });
    std::vector<uint64_t> orderedSetItems(itemRank.size(), 0);
    for (size_t i = 0; i < itemRank.size(); i++)
        orderedSetItems[itemRank[i]] = receiverSetItems[i];

    return orderedSetItems;
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
