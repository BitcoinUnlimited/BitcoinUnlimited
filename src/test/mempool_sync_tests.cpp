// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "blockrelay/mempool_sync.h"
#include "serialize.h"
#include "streams.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <cassert>
#include <iostream>

BOOST_FIXTURE_TEST_SUITE(mempool_sync_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(mempool_sync_can_serde)
{
    uint64_t sync_version = DEFAULT_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED;
    uint64_t nReceiverMemPoolTx = 0;
    uint64_t nSenderMempoolPlusBlock = 1;
    uint64_t shorttxidk0 = 7;
    uint64_t shorttxidk1 = 11;

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
    std::vector<uint256> senderMempoolTxHashes;
    std::vector<uint256> receiverMempoolTxHashes;
    senderMempoolTxHashes.push_back(tx.GetHash());
    CMempoolSync senderMempoolSync(
        senderMempoolTxHashes, nReceiverMemPoolTx, nSenderMempoolPlusBlock, shorttxidk0, shorttxidk1, sync_version);
    CMempoolSync receiverMempoolSync(sync_version);
    CDataStream ss(SER_DISK, 0);

    ss << senderMempoolSync;
    ss >> receiverMempoolSync;

    receiverMempoolSync.pGrapheneSet->Reconcile(receiverMempoolTxHashes);
}

BOOST_AUTO_TEST_SUITE_END()
