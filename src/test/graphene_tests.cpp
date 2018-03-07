#include "graphene_set.h"
#include "hash.h"
#include "serialize.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <cassert>
#include <iostream>

BOOST_FIXTURE_TEST_SUITE(graphene_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(graphene_encodes_and_decodes)
{
    int senderArr[] = {3, 1, 2, 7, 11, 4};
    std::vector<int> senderItems(senderArr, senderArr + sizeof(senderArr) / sizeof(int));
    int receiverArr[] = {7, 2, 4, -1, 1, 11};
    std::vector<int> receiverItems(receiverArr, receiverArr + sizeof(receiverArr) / sizeof(int));

    std::vector<uint256> receiverItemHashes;
    for (int &item : receiverItems)
        receiverItemHashes.push_back(SerializeHash(item));

    // unordered graphene sets
    {
        CGrapheneSet<int> senderGrapheneSet(10, senderItems);
        std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItemHashes);

        vector<uint64_t> senderCheapHashes;

        for (int item : senderItems)
            senderCheapHashes.push_back(SerializeHash(item).GetCheapHash());

        sort(senderCheapHashes.begin(), senderCheapHashes.end(), [](uint64_t i1, uint64_t i2) { return i1 < i2; });
        sort(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
            [](uint64_t i1, uint64_t i2) { return i1 < i2; });

        BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
            senderCheapHashes.begin(), senderCheapHashes.end());
    }

    // ordered graphene sets
    {
        CGrapheneSet<int> senderGrapheneSet(10, senderItems, true);
        std::vector<uint64_t> reconciledCheapHashes = senderGrapheneSet.Reconcile(receiverItemHashes);

        vector<uint64_t> senderCheapHashes;

        for (int item : senderItems)
            senderCheapHashes.push_back(SerializeHash(item).GetCheapHash());

        BOOST_CHECK_EQUAL_COLLECTIONS(reconciledCheapHashes.begin(), reconciledCheapHashes.end(),
            senderCheapHashes.begin(), senderCheapHashes.end());
    }
}

BOOST_AUTO_TEST_SUITE_END()
