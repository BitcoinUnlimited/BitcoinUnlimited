// Copyright (c) 2016-2019 The Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockstorage/blockcache.h"
#include "chainparams.h"
#include "main.h"
#include "miner.h"
#include "primitives/block.h"
#include "random.h"
#include "serialize.h"
#include "streams.h"
#include "txmempool.h"
#include "uint256.h"
#include "unlimited.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <sstream>
#include <string.h>

CBlock cache_testblock1()
{
    CDataStream stream(
        ParseHex("0000002055f2f98205ef364b786942ae89f449299e844be2eb5e73207a9981449d53e3112ebc8c5858fd4b2b699283595a035"
                 "e12bb885792564c881cc4eea4dd5aca29d0bb45ca5fffff7f2000000000010100000001000000000000000000000000000000"
                 "0000000000000000000000000000000000ffffffff0e52510b2f454233322f414431322fffffffff0100f2052a01000000232"
                 "10265a5fd1dbd257fb37edfbb187098f73514d85568dda8781a2771dd303cc11708ac00000000"),
        SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    stream >> block;
    return block;
};

CBlock cache_testblock2()
{
    // Block taken from block 10 on mainnet
    CDataStream stream(
        ParseHex("00000020f8a5eea9efecd942699f91b46853f45d11627df992f7018a634f8f554fe6ec463531a01895b3b661e13be88db72ee"
                 "1949c46f5de28ad4e522efbde2ba1bf76f6bb45ca5fffff7f2001000000010100000001000000000000000000000000000000"
                 "0000000000000000000000000000000000ffffffff0e53510b2f454233322f414431322fffffffff0100f2052a01000000232"
                 "10265a5fd1dbd257fb37edfbb187098f73514d85568dda8781a2771dd303cc11708ac00000000"),
        SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    stream >> block;
    return block;
};

CBlock cache_testblock3()
{
    CDataStream stream(
        ParseHex("00000020b85e0e167f0836c6c82ab88da177a5fddf38738affb9728ac588bda8e0faa33b4ea18e67df5426c820a385acfa1de"
                 "391f32812b3faec46103e74d42cb6155052bc45ca5fffff7f2002000000010100000001000000000000000000000000000000"
                 "0000000000000000000000000000000000ffffffff0e54510b2f454233322f414431322fffffffff0100f2052a01000000232"
                 "10265a5fd1dbd257fb37edfbb187098f73514d85568dda8781a2771dd303cc11708ac00000000"),
        SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    stream >> block;
    return block;
};

BOOST_FIXTURE_TEST_SUITE(blockcache_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(cache_tests)
{
    CBlockCache localcache;
    localcache.Init();
    IsChainNearlySyncdSet(false);

    // Create a new block and add it to the block cache
    CBlockRef pNewBlock1 = MakeBlockRef(cache_testblock1());
    localcache.AddBlock(pNewBlock1, 1);

    // Retrieve the block from the cache
    CBlockRef pBlockCache1 = localcache.GetBlock(pNewBlock1->GetHash());
    if (pBlockCache1)
        BOOST_CHECK(pBlockCache1->GetHash() == pNewBlock1->GetHash());
    else
        throw std::runtime_error(
            std::string("Could not find block1 in blockcache for ") + HexStr(pNewBlock1->GetHash()));

    // Create two new blocks and add it to the block cache
    CBlockRef pNewBlock2 = MakeBlockRef(cache_testblock2());
    localcache.AddBlock(pNewBlock2, 2);
    CBlockRef pNewBlock3 = MakeBlockRef(cache_testblock3());
    localcache.AddBlock(pNewBlock3, 3);

    // Retrieve block2 from the cache
    CBlockRef pBlockCache2 = localcache.GetBlock(pNewBlock2->GetHash());
    if (pBlockCache2)
        BOOST_CHECK(pBlockCache2->GetHash() == pNewBlock2->GetHash());
    else
        throw std::runtime_error(
            std::string("Could not find block2 in blockcache for ") + HexStr(pNewBlock2->GetHash()));

    // Retrieve block3 from the cache
    CBlockRef pBlockCache3 = localcache.GetBlock(pNewBlock3->GetHash());
    if (pBlockCache3)
        BOOST_CHECK(pBlockCache3->GetHash() == pNewBlock3->GetHash());
    else
        throw std::runtime_error(
            std::string("Could not find block3 in blockcache for ") + HexStr(pNewBlock3->GetHash()));

    // Check all blocks are not the same
    BOOST_CHECK(pBlockCache1->GetHash() != pBlockCache2->GetHash());
    BOOST_CHECK(pBlockCache1->GetHash() != pBlockCache3->GetHash());
    BOOST_CHECK(pBlockCache2->GetHash() != pBlockCache3->GetHash());

    // Erase a block and check it is erased
    localcache.EraseBlock(pNewBlock1->GetHash());
    CBlockRef pBlockCacheNull = localcache.GetBlock(pNewBlock1->GetHash());
    BOOST_CHECK(pBlockCacheNull == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
