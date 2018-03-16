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

// c from graphene paper
const double BLOOM_OVERHEAD_FACTOR = 8 * pow(log(2.0), 2.0);
// tau from graphene paper
const double IBLT_OVERHEAD_FACTOR = 16.5;
const uint8_t IBLT_CELL_MINIMUM = 3;
const uint8_t IBLT_VALUE_SIZE = 0;
const std::vector<uint8_t> IBLT_NULL_VALUE = {};
const unsigned char WORD_BITS = 8;

class CGrapheneSet
{
private:
    std::vector<uint64_t> ArgSort(const std::vector<uint64_t> &items)
    {
        std::vector<uint64_t> idxs(items.size());
        iota(idxs.begin(), idxs.end(), 0);

        std::sort(idxs.begin(), idxs.end(), [&items](size_t idx1, size_t idx2) { return items[idx1] < items[idx2]; });

        return idxs;
    }

    double OptimalSymDiff(uint64_t nBlockTxs, uint64_t receiverMemPoolInfo)
    {
        // This is the "a" parameter from the graphene paper
        double symDiff = nBlockTxs / (BLOOM_OVERHEAD_FACTOR * IBLT_OVERHEAD_FACTOR);

        assert(symDiff > 0.0);

        return symDiff;
    }

public:
    CGrapheneSet() : pSetFilter(nullptr), pSetIblt(nullptr) {}
    CGrapheneSet(size_t _nReceiverUniverseItems,
        const std::vector<uint256> &_itemHashes,
        bool _ordered = false,
        bool fDeterministic = false)
    {
        ordered = _ordered;
        nReceiverUniverseItems = _nReceiverUniverseItems;
        uint64_t nItems = _itemHashes.size();
        FastRandomContext insecure_rand(fDeterministic);

        // Determine constants
        double optSymDiff = OptimalSymDiff(nItems, nReceiverUniverseItems);
        double sizeDiff = nReceiverUniverseItems - nItems;
        double fpr;

        if (sizeDiff <= 0 || optSymDiff > sizeDiff)
        {
            // Just use a very small filter and pass almost everything through
            fpr = 0.99;
        }
        else
            fpr = optSymDiff / float(sizeDiff);

        // Construct Bloom filter
        pSetFilter = new CBloomFilter(
            nItems, fpr, insecure_rand.rand32(), BLOOM_UPDATE_ALL, std::numeric_limits<uint32_t>::max());
        LOG(GRAPHENE, "fp rate: %f Num elements in bloom filter: %d\n", fpr, nItems);

        // Construct IBLT
        uint64_t nIbltCells = std::max((int)IBLT_CELL_MINIMUM, (int)ceil(optSymDiff));
        pSetIblt = new CIblt(nIbltCells, IBLT_VALUE_SIZE);
        std::map<uint64_t, uint256> mapCheapHashes;

        for (const uint256 &itemHash : _itemHashes)
        {
            uint64_t cheapHash = itemHash.GetCheapHash();

            pSetFilter->insert(itemHash);

            if (mapCheapHashes.count(cheapHash))
                throw error("Cheap hash collision while encoding graphene set");

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

    std::vector<uint64_t> Reconcile(const std::vector<uint256> &receiverItemHashes)
    {
        std::set<uint64_t> receiverSet;
        std::map<uint64_t, uint256> mapCheapHashes;
        CIblt localIblt((*pSetIblt));
        localIblt.reset();

        for (const uint256 &itemHash : receiverItemHashes)
        {
            uint64_t cheapHash = itemHash.GetCheapHash();

            if (mapCheapHashes.count(cheapHash))
                throw error("Cheap hash collision while decoding graphene set");

            if ((*pSetFilter).contains(itemHash))
            {
                receiverSet.insert(cheapHash);
                localIblt.insert(cheapHash, IBLT_NULL_VALUE);
            }

            mapCheapHashes[cheapHash] = itemHash;
        }

        mapCheapHashes.clear();

        // Determine difference between sender and receiver IBLTs
        CIblt diffIblt = (*pSetIblt) - localIblt;
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > senderHas;
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > receiverHas;

        if (!((*pSetIblt) - localIblt).listEntries(senderHas, receiverHas))
            throw error("Graphene set IBLT did not decode");

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

    static std::vector<unsigned char> EncodeRank(std::vector<uint64_t> items, uint16_t nBitsPerItem)
    {
        size_t nItems = items.size();
        size_t nEncodedWords = int(ceil(nBitsPerItem * nItems / float(WORD_BITS)));
        std::vector<unsigned char> encoded(nEncodedWords, 0);

        // form boolean array (low-order first)
        bool bits[nEncodedWords * WORD_BITS];
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

    static std::vector<uint64_t> DecodeRank(std::vector<unsigned char> encoded, size_t nItems, uint16_t nBitsPerItem)
    {
        size_t nEncodedWords = int(ceil(nBitsPerItem * nItems / float(WORD_BITS)));

        // decode into boolean array (low-order first)
        bool bits[nEncodedWords * WORD_BITS];
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

    ~CGrapheneSet()
    {
        if (pSetFilter)
        {
            delete pSetFilter;
            pSetFilter = NULL;
        }

        if (pSetIblt)
        {
            delete pSetIblt;
            pSetIblt = NULL;
        }
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(ordered);
        READWRITE(nReceiverUniverseItems);
        READWRITE(encodedRank);
        if (!pSetFilter)
            pSetFilter = new CBloomFilter();
        READWRITE(*pSetFilter);
        if (!pSetIblt)
            pSetIblt = new CIblt();
        READWRITE(*pSetIblt);
    }

private:
    bool ordered;
    size_t nReceiverUniverseItems;
    std::vector<unsigned char> encodedRank;
    CBloomFilter *pSetFilter;
    CIblt *pSetIblt;
};

#endif // BITCOIN_GRAPHENE_SET_H
