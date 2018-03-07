// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRAPHENE_SET_H
#define BITCOIN_GRAPHENE_SET_H

#include "bloom.h"
#include "boost/dynamic_bitset.hpp"
#include "hash.h"
#include "iblt.h"
#include "random.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

#include <atomic>
#include <cmath>
#include <vector>

using namespace std;

// c from graphene paper
const double BLOOM_OVERHEAD_FACTOR = 8 * pow(log(2.0), 2.0);
// tau from graphene paper
const double IBLT_OVERHEAD_FACTOR = 16.5;
const uint8_t IBLT_CELL_MINIMUM = 3;
const uint8_t IBLT_VALUE_SIZE = 0;
const std::vector<uint8_t> IBLT_NULL_VALUE = {};

template <typename T>
class CGrapheneSet
{
private:
    std::vector<size_t> ArgSort(const std::vector<uint64_t> &items)
    {
        std::vector<size_t> idxs(items.size());
        iota(idxs.begin(), idxs.end(), 0);

        sort(idxs.begin(), idxs.end(), [&items](size_t idx1, size_t idx2) { return items[idx1] < items[idx2]; });

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
    CGrapheneSet(size_t _nReceiverUniverseItems, const std::vector<T> &_items, bool _ordered = false)
    {
        ordered = _ordered;
        nReceiverUniverseItems = _nReceiverUniverseItems;
        uint64_t nItems = _items.size();

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
        pSetFilter = new CBloomFilter(nItems, fpr, insecure_rand(), BLOOM_UPDATE_ALL, numeric_limits<uint32_t>::max());
        LOG(GRAPHENE, "fp rate: %f Num elements in bloom filter: %d\n", fpr, nItems);

        // Construct IBLT
        uint64_t nIbltCells = max((int)IBLT_CELL_MINIMUM, (int)ceil(optSymDiff));
        pSetIblt = new CIblt(nIbltCells, IBLT_VALUE_SIZE);
        std::map<uint64_t, T> mapCheapHashes;

        for (const T &item : _items)
        {
            uint256 hash = SerializeHash(item);
            uint64_t cheapHash = hash.GetCheapHash();

            pSetFilter->insert(hash);

            if (mapCheapHashes.count(cheapHash))
                throw error("Cheap hash collision while encoding graphene set");

            pSetIblt->insert(cheapHash, IBLT_NULL_VALUE);
            mapCheapHashes[cheapHash] = item;
        }

        // Record transaction order
        if (ordered)
        {
            std::map<T, uint64_t> mapItems;
            for (const pair<uint64_t, T> &kv : mapCheapHashes)
                mapItems[kv.second] = kv.first;

            mapCheapHashes.clear();

            std::vector<uint64_t> cheapHashes;
            for (const T &item : _items)
                cheapHashes.push_back(mapItems[item]);

            std::vector<size_t> sortedIdxs = ArgSort(cheapHashes);
            uint8_t nBits = ceil(log2(cheapHashes.size()));

            for (size_t i = 0; i < cheapHashes.size(); i++)
                itemRank.push_back(boost::dynamic_bitset<>(nBits, sortedIdxs[i]));
        }
    }

    std::vector<uint64_t> Reconcile(const std::vector<uint256> &receiverItemHashes)
    {
        set<uint64_t> receiverSet;
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
        set<pair<uint64_t, std::vector<uint8_t> > > senderHas;
        set<pair<uint64_t, std::vector<uint8_t> > > receiverHas;

        if (!((*pSetIblt) - localIblt).listEntries(senderHas, receiverHas))
            throw error("Graphene set IBLT did not decode");

        // Remove false positives from receiverSet
        for (const pair<uint64_t, std::vector<uint8_t> > &kv : receiverHas)
            receiverSet.erase(kv.first);

        // Restore missing items recovered from sender
        for (const pair<uint64_t, std::vector<uint8_t> > &kv : senderHas)
            receiverSet.insert(kv.first);

        std::vector<uint64_t> receiverSetItems(receiverSet.begin(), receiverSet.end());

        if (!ordered)
            return receiverSetItems;

        // Place items in order
        sort(receiverSetItems.begin(), receiverSetItems.end(), [](uint64_t i1, uint64_t i2) { return i1 < i2; });
        std::vector<uint64_t> orderedSetItems(itemRank.size(), 0);
        for (size_t i = 0; i < itemRank.size(); i++)
            orderedSetItems[itemRank[i].to_ulong()] = receiverSetItems[i];

        return orderedSetItems;
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
        READWRITE(itemRank);
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
    std::vector<boost::dynamic_bitset<> > itemRank;
    CBloomFilter *pSetFilter;
    CIblt *pSetIblt;
};

#endif // BITCOIN_GRAPHENE_SET_H
