#include "bloom.h"
#include "graphene_set.h"
#include "hash.h"
#include "serialize.h"
#include "streams.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <cassert>
#include <cmath>
#include <iostream>

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
        CGrapheneSet senderGrapheneSet(10, senderItems, false, true);
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
        CGrapheneSet senderGrapheneSet(10, senderItems, true, true);
        std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItems);

        std::vector<uint64_t> senderCheapHashes;
        for (uint256 item : senderItems)
            senderCheapHashes.push_back(item.GetCheapHash());

        BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
            senderCheapHashes.begin(), senderCheapHashes.end());
    }
}

BOOST_AUTO_TEST_CASE(graphene_set_finds_optimal_settings)
{
    const int SERIALIZATION_OVERHEAD = 10;
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
        CBloomFilter filter(n, fpr(a), insecure_rand.rand32(), BLOOM_UPDATE_ALL, std::numeric_limits<uint32_t>::max());
        CIblt iblt(a, IBLT_VALUE_SIZE);

        size_t filterBytes = ::GetSerializeSize(filter, SER_NETWORK, PROTOCOL_VERSION) - SERIALIZATION_OVERHEAD;
        size_t ibltBytes = ::GetSerializeSize(iblt, SER_NETWORK, PROTOCOL_VERSION) - SERIALIZATION_OVERHEAD;
        size_t total = filterBytes + ibltBytes;

        if (total < best_size)
        {
            best_size = total;
            best_a = a;
        }
    }

    BOOST_CHECK_EQUAL(grapheneSet.OptimalSymDiff(n, m), best_a);
}

BOOST_AUTO_TEST_CASE(graphene_set_can_serde)
{
    std::vector<uint256> senderItems;
    CDataStream ss(SER_DISK, 0);

    senderItems.push_back(SerializeHash(3));
    CGrapheneSet sentGrapheneSet(1, senderItems, true);
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
