#include "blockrelay/graphene.h"
#include "blockrelay/graphene_set.h"
#include "bloom.h"
#include "fastfilter.h"
#include "hash.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "streams.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>

#define MAX_GRAPHENE_SET_VERSION 1

size_t ProjectedGrapheneSizeBytes(uint64_t nBlockTxs, uint64_t nExcessTxs, uint64_t nSymDiff, bool computeOptimized=false)
{
    const int SERIALIZATION_OVERHEAD = 11;
    FastRandomContext insecure_rand(true);
    auto fpr = [nExcessTxs](int a) { return a / float(nExcessTxs); };

    CIblt iblt(nSymDiff, 0);
    size_t ibltBytes = ::GetSerializeSize(iblt, SER_NETWORK, PROTOCOL_VERSION) - SERIALIZATION_OVERHEAD;

    size_t filterBytes;
    if (computeOptimized)
    {
        CVariableFastFilter filter(nBlockTxs, fpr(nSymDiff));
        filterBytes = ::GetSerializeSize(filter, SER_NETWORK, PROTOCOL_VERSION) - SERIALIZATION_OVERHEAD;
    }
    else
    {
        CBloomFilter filter(
            nBlockTxs, fpr(nSymDiff), insecure_rand.rand32(), BLOOM_UPDATE_ALL, true, std::numeric_limits<uint32_t>::max());
        filterBytes = ::GetSerializeSize(filter, SER_NETWORK, PROTOCOL_VERSION) - SERIALIZATION_OVERHEAD;
    }

    return filterBytes + ibltBytes;
}

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
        CGrapheneSet senderGrapheneSet(6, 6, senderItems, 0, 0, 0, 0, false, false, true);
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
        CGrapheneSet senderGrapheneSet(6, 6, senderItems, 0, 0, 0, 0, false, true, true);
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

            CGrapheneSet senderGrapheneSet(
                receiverItems.size(), receiverItems.size(), senderItems, 0, 0, 0, 0, false, true, true);
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

            CGrapheneSet senderGrapheneSet(
                receiverItems.size(), receiverItems.size(), senderItems, 0, 0, 0, 0, false, true, true);
            std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItems);

            BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
                senderCheapHashes.begin(), senderCheapHashes.end());
        }
    }
}

BOOST_AUTO_TEST_CASE(graphene_set_finds_brute_force_opt_for_small_blocks)
{
    CGrapheneSet grapheneSet(0);

    int n = (int)std::floor(APPROX_ITEMS_THRESH / 2);
    int mu = 100;
    int m = (int)std::floor(n / 8) + mu;

    int best_a = 1;
    size_t best_size = std::numeric_limits<size_t>::max();
    int a = 1;
    for (a = 1; a < m - mu; a++)
    {
        size_t totalBytes = ProjectedGrapheneSizeBytes(n, m - mu, a);
        size_t totalBytesOpt = ProjectedGrapheneSizeBytes(n, m - mu, a, true);

        BOOST_CHECK_EQUAL(totalBytes, totalBytesOpt);

        if (totalBytes < best_size)
        {
            best_size = totalBytes;
            best_a = a;
        }
    }

    BOOST_CHECK_EQUAL(grapheneSet.OptimalSymDiff(n, m, m - mu, 1), best_a);
}

BOOST_AUTO_TEST_CASE(graphene_set_finds_approx_opt_for_large_blocks)
{
    int n = 4 * APPROX_ITEMS_THRESH;
    int mu = 1000;
    int m = APPROX_ITEMS_THRESH + mu;
    CGrapheneSet grapheneSet(0);
    auto approxSymDiff = [n]() {
        return std::max(
            1.0, std::round(FILTER_CELL_SIZE * n / (8 * IBLT_CELL_SIZE * IBLT_DEFAULT_OVERHEAD * LN2SQUARED)));
    };

    BOOST_CHECK_EQUAL(approxSymDiff(), grapheneSet.OptimalSymDiff(n, m, m - mu, 0));
}

BOOST_AUTO_TEST_CASE(graphene_set_approx_opt_close_to_optimal)
{
    int n = APPROX_ITEMS_THRESH;
    int mu = 100;
    int m = (int)std::ceil(n / APPROX_EXCESS_RATE) + mu;
    CGrapheneSet grapheneSet(0);

    float totalBytesApprox = (float)ProjectedGrapheneSizeBytes(n, m - mu, grapheneSet.ApproxOptimalSymDiff(n));
    float totalBytesBrute =
        (float)ProjectedGrapheneSizeBytes(n, m - mu, grapheneSet.BruteForceSymDiff(n, m, m - mu, 0));
    float totalBytesBruteOpt =
        (float)ProjectedGrapheneSizeBytes(n, m - mu, grapheneSet.BruteForceSymDiff(n, m, m - mu, 0), true);

    BOOST_CHECK_CLOSE(totalBytesApprox, totalBytesBrute, 15);
    BOOST_CHECK_CLOSE(totalBytesApprox, totalBytesBruteOpt, 15);
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

    CGrapheneSet senderGrapheneSet(6, 12, senderItems, 0, 0, 0, 0, false, true, true);
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
    CGrapheneSet sentGrapheneSet(1, 1, senderItems, 0, 0, 0, 0, false, false, true);
    CGrapheneSet receivedGrapheneSet(0);

    ss << sentGrapheneSet;
    ss >> receivedGrapheneSet;

    BOOST_CHECK_EQUAL(receivedGrapheneSet.Reconcile(senderItems)[0], senderItems[0].GetCheapHash());
}

BOOST_AUTO_TEST_CASE(graphene_set_version_check)
{
    for (uint64_t version = 0; version <= MAX_GRAPHENE_SET_VERSION; version++)
    {
        std::vector<uint256> senderItems;
        CDataStream ss(SER_DISK, 0);

        senderItems.push_back(SerializeHash(1));
        senderItems.push_back(SerializeHash(2));
        senderItems.push_back(SerializeHash(3));
        CGrapheneSet sentGrapheneSet(3, 3, senderItems, 0, 0, version, false, false, true);
        CGrapheneSet receivedGrapheneSet(version);

        ss << sentGrapheneSet;
        ss >> receivedGrapheneSet;
    }
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

BOOST_AUTO_TEST_CASE(compute_optimized_graphene_set_can_serde)
{
    std::vector<uint256> senderItems;
    CDataStream ss(SER_DISK, 0);

    senderItems.push_back(SerializeHash(3));
    CGrapheneSet sentGrapheneSet(1, 1, senderItems, 0, 0, 3, 0, true, false, true);
    CGrapheneSet receivedGrapheneSet(3);

    ss << sentGrapheneSet;
    ss >> receivedGrapheneSet;

    BOOST_CHECK_EQUAL(receivedGrapheneSet.Reconcile(senderItems)[0], sentGrapheneSet.GetShortID(senderItems[0]));
}

BOOST_AUTO_TEST_CASE(graphene_set_cpu_check)
{
    size_t nItems = 10000;
    int nNumHashes = 0;

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

    // Add 10000 more items to receiver mempool
    std::vector<uint256> receiverItems = baseReceiverItems;
    for (size_t j = 1; j < 10000; j++)
    {
        nNumHashes++;
        receiverItems.push_back(SerializeHash(GetHash(nNumHashes)));
    }

    auto legacyStart = std::chrono::high_resolution_clock::now();
    CGrapheneSet legacyGrapheneSet(
        receiverItems.size(), receiverItems.size(), senderItems, 0, 0, 0, 0, false, false, true);
    legacyGrapheneSet.Reconcile(receiverItems);
    auto legacyFinish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> legacyElapsed = legacyFinish - legacyStart;
    std::cout << "Legacy elapsed time: " << legacyElapsed.count() << " s\n";

    auto sipStart = std::chrono::high_resolution_clock::now();
    CGrapheneSet sipGrapheneSet(
        receiverItems.size(), receiverItems.size(), senderItems, 0, 0, 2, 0, false, false, true);
    sipGrapheneSet.Reconcile(receiverItems);
    auto sipFinish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> sipElapsed = sipFinish - sipStart;
    std::cout << "Sip elapsed time: " << sipElapsed.count() << " s\n";

    auto fastStart = std::chrono::high_resolution_clock::now();
    CGrapheneSet fastGrapheneSet(
        receiverItems.size(), receiverItems.size(), senderItems, 0, 0, 3, 0, true, false, true);
    fastGrapheneSet.Reconcile(receiverItems);
    auto fastFinish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> fastElapsed = fastFinish - fastStart;
    std::cout << "Fast elapsed time: " << fastElapsed.count() << " s\n";
}

BOOST_AUTO_TEST_CASE(graphene_block_can_serde)
{

    // regular graphene block
    {
        CBlock block;
        CTransaction tx;
        CDataStream stream(
            ParseHex("01000000010b26e9b7735eb6aabdf358bab62f9816a21ba9ebdb719d5299e88607d722c190000000008b4830450220070aca4"
                     "4506c5cef3a16ed519d7c3c39f8aab192c4e1c90d065f37b8a4af6141022100a8e160b856c2d43d27d8fba71e5aef6405b864"
                     "3ac4cb7cb3c462aced7f14711a0141046d11fee51b0e60666d5049a9101a72741df480b96ee26488a4d3466b95c9a40ac5eee"
                     "f87e10a5cd336c19a84565f80fa6c547957b7700ff4dfbdefe76036c339ffffffff021bff3d11000000001976a91404943fdd"
                     "508053c75000106d3bc6e2754dbcff1988ac2f15de00000000001976a914a266436d2965547608b9e15d9032a7b9d64fa4318"
                     "8ac00000000"),
            SER_DISK, CLIENT_VERSION);
        stream >> tx;
        const CTransactionRef ptx = MakeTransactionRef(tx);
        block.vtx.push_back(ptx);
        CGrapheneBlock senderGrapheneBlock(MakeBlockRef(block), 5, 6, 4, false);
        CGrapheneBlock receiverGrapheneBlock(4);
        CDataStream ss(SER_DISK, 0);

        ss << senderGrapheneBlock;
        ss >> receiverGrapheneBlock;
    }
    
    // compute optimized graphene block
    {
        CBlock block;
        CTransaction tx;
        CDataStream stream(
            ParseHex("01000000010b26e9b7735eb6aabdf358bab62f9816a21ba9ebdb719d5299e88607d722c190000000008b4830450220070aca4"
                     "4506c5cef3a16ed519d7c3c39f8aab192c4e1c90d065f37b8a4af6141022100a8e160b856c2d43d27d8fba71e5aef6405b864"
                     "3ac4cb7cb3c462aced7f14711a0141046d11fee51b0e60666d5049a9101a72741df480b96ee26488a4d3466b95c9a40ac5eee"
                     "f87e10a5cd336c19a84565f80fa6c547957b7700ff4dfbdefe76036c339ffffffff021bff3d11000000001976a91404943fdd"
                     "508053c75000106d3bc6e2754dbcff1988ac2f15de00000000001976a914a266436d2965547608b9e15d9032a7b9d64fa4318"
                     "8ac00000000"),
            SER_DISK, CLIENT_VERSION);
        stream >> tx;
        const CTransactionRef ptx = MakeTransactionRef(tx);
        block.vtx.push_back(ptx);
        CGrapheneBlock senderGrapheneBlock(MakeBlockRef(block), 5, 6, 4, false);
        CGrapheneBlock receiverGrapheneBlock(4);
        CDataStream ss(SER_DISK, 0);

        ss << senderGrapheneBlock;
        ss >> receiverGrapheneBlock;
    }
}


BOOST_AUTO_TEST_SUITE_END()
