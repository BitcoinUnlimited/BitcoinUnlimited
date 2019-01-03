#include "blockrelay/graphene_set.h"
#include "bloom.h"
#include "hash.h"
#include "serialize.h"
#include "streams.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <cassert>
#include <cmath>
#include <iostream>

// Create a deterministic hash by providing an index
uint256 GetHash(unsigned int nIndex)
{
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(32) << nIndex;
    return uint256S(ss.str());
}

BOOST_FIXTURE_TEST_SUITE(graphene_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(graphene_set_encodes_and_decodes)
{
    uint256 senderArr[] = {
        SerializeHash(3), SerializeHash(1), SerializeHash(2), SerializeHash(7), SerializeHash(11), SerializeHash(4)};
    std::vector<uint256> senderItems(senderArr, senderArr + sizeof(senderArr) / sizeof(uint256));
    uint256 receiverArr[] = {
        SerializeHash(7), SerializeHash(2), SerializeHash(4), SerializeHash(-1), SerializeHash(1), SerializeHash(11)};
    std::vector<uint256> receiverItems(receiverArr, receiverArr + sizeof(receiverArr) / sizeof(uint256));

    // unordered graphene sets
    {
        CGrapheneSet senderGrapheneSet(6, 6, senderItems, false, true);
        std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItems);

        std::vector<uint64_t> senderCheapHashes;
        for (uint256 item : senderItems)
            senderCheapHashes.push_back(item.GetCheapHash());

        std::sort(senderCheapHashes.begin(), senderCheapHashes.end(), [](uint64_t i1, uint64_t i2) { return i1 < i2; });
        std::sort(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
            [](uint64_t i1, uint64_t i2) { return i1 < i2; });

        BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
            senderCheapHashes.begin(), senderCheapHashes.end());
    }

    // ordered graphene sets
    {
        CGrapheneSet senderGrapheneSet(6, 6, senderItems, true, true);
        std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItems);

        std::vector<uint64_t> senderCheapHashes;
        for (uint256 item : senderItems)
            senderCheapHashes.push_back(item.GetCheapHash());

        BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
            senderCheapHashes.begin(), senderCheapHashes.end());
    }
}

BOOST_AUTO_TEST_CASE(graphene_set_decodes_multiple_sizes)
{
    size_t nItemList[] = {1, 10, 50, 500, 5000, 10000};
    int nNumHashes = 0;
    for (size_t nItems : nItemList)
    {
        std::vector<uint256> senderItems;
        std::vector<uint64_t> senderCheapHashes;
        std::vector<uint256> baseReceiverItems;
        for (size_t i = 1; i <= nItems; i++)
        {
            nNumHashes++;
            const uint256 &hash = GetHash(nNumHashes);
            senderItems.push_back(hash);
            senderCheapHashes.push_back(hash.GetCheapHash());
            baseReceiverItems.push_back(hash);
        }

        // Add 10 more items to receiver mempool
        {
            std::vector<uint256> receiverItems = baseReceiverItems;
            for (size_t j = 1; j < 11; j++)
            {
                nNumHashes++;
                receiverItems.push_back(SerializeHash(GetHash(nNumHashes)));
            }

            CGrapheneSet senderGrapheneSet(receiverItems.size(), receiverItems.size(), senderItems, true, true);
            std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItems);

            BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
                senderCheapHashes.begin(), senderCheapHashes.end());
        }

        // Add 100 more items to receiver mempool
        {
            std::vector<uint256> receiverItems = baseReceiverItems;
            for (size_t j = 1; j < 101; j++)
            {
                nNumHashes++;
                receiverItems.push_back(SerializeHash(GetHash(nNumHashes)));
            }

            CGrapheneSet senderGrapheneSet(receiverItems.size(), receiverItems.size(), senderItems, true, true);
            std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItems);

            BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
                senderCheapHashes.begin(), senderCheapHashes.end());
        }
    }
}

BOOST_AUTO_TEST_CASE(graphene_set_finds_optimal_settings)
{
    const int SERIALIZATION_OVERHEAD = 11;
    FastRandomContext insecure_rand(true);
    CGrapheneSet grapheneSet;

    int m = 5000;
    int mu = 2999;
    int n = 3000;

    auto fpr = [m, mu](int a) { return a / float(m - mu); };

    int best_a = 1;
    size_t best_size = std::numeric_limits<size_t>::max();
    int a = 1;
    for (a = 1; a < m - mu; a++)
    {
        CBloomFilter filter(
            n, fpr(a), insecure_rand.rand32(), BLOOM_UPDATE_ALL, true, std::numeric_limits<uint32_t>::max());
        CIblt iblt(a);

        size_t filterBytes = ::GetSerializeSize(filter, SER_NETWORK, PROTOCOL_VERSION) - SERIALIZATION_OVERHEAD;
        size_t ibltBytes = ::GetSerializeSize(iblt, SER_NETWORK, PROTOCOL_VERSION) - SERIALIZATION_OVERHEAD;
        size_t total = filterBytes + ibltBytes;

        if (total < best_size)
        {
            best_size = total;
            best_a = a;
        }
    }

    BOOST_CHECK_EQUAL(grapheneSet.OptimalSymDiff(n, m, m - mu, 1), best_a);
}

BOOST_AUTO_TEST_CASE(graphene_set_decodes_empty_intersection)
{
    uint256 senderArr[] = {SerializeHash(-7), SerializeHash(-2), SerializeHash(-4), SerializeHash(-1),
        SerializeHash(-5), SerializeHash(-11), SerializeHash(3), SerializeHash(1), SerializeHash(2), SerializeHash(7),
        SerializeHash(11), SerializeHash(4)};
    std::vector<uint256> senderItems(senderArr, senderArr + sizeof(senderArr) / sizeof(uint256));

    // includes no transactions from block
    uint256 receiverArr[] = {SerializeHash(-7), SerializeHash(-2), SerializeHash(-4), SerializeHash(-1),
        SerializeHash(-5), SerializeHash(-11)};
    std::vector<uint256> receiverItems(receiverArr, receiverArr + sizeof(receiverArr) / sizeof(uint256));

    CGrapheneSet senderGrapheneSet(6, 12, senderItems, true, true);
    std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItems);

    std::vector<uint64_t> senderCheapHashes;
    for (uint256 item : senderItems)
        senderCheapHashes.push_back(item.GetCheapHash());

    BOOST_CHECK_EQUAL_COLLECTIONS(
        reconciledCheapHashes.begin(), reconciledCheapHashes.end(), senderCheapHashes.begin(), senderCheapHashes.end());
}

BOOST_AUTO_TEST_CASE(graphene_set_can_serde)
{
    std::vector<uint256> senderItems;
    CDataStream ss(SER_DISK, 0);

    senderItems.push_back(SerializeHash(3));
    CGrapheneSet sentGrapheneSet(1, 1, senderItems, true);
    CGrapheneSet receivedGrapheneSet;

    ss << sentGrapheneSet;
    ss >> receivedGrapheneSet;

    BOOST_CHECK_EQUAL(receivedGrapheneSet.Reconcile(senderItems)[0], senderItems[0].GetCheapHash());
}

BOOST_AUTO_TEST_CASE(item_rank_encodes_and_decodes)
{
    uint64_t itemArr[4] = {1, 20, 500, 7000};
    std::vector<uint64_t> inputItems(itemArr, itemArr + sizeof(itemArr) / sizeof(uint64_t));
    uint16_t nBits = 13;

    std::vector<unsigned char> encoded = CGrapheneSet::EncodeRank(inputItems, nBits);
    std::vector<uint64_t> outputItems = CGrapheneSet::DecodeRank(encoded, inputItems.size(), nBits);

    BOOST_CHECK_EQUAL_COLLECTIONS(outputItems.begin(), outputItems.end(), inputItems.begin(), inputItems.end());
}

BOOST_AUTO_TEST_SUITE_END()
