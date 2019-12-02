// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"
#include "util.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <list>
#include <vector>

extern CCoinsViewCache *pcoinsTip;

struct MempoolData
{
    uint256 hash;

    // Ancestor counters
    uint64_t nCountWithAncestors = 0;
    uint64_t nSizeWithAncestors = 0;
    uint64_t nSigopsWithAncestors = 0;
    uint64_t nFeesWithAncestors = 0;

    // Descendant counters
    uint64_t nCountWithDescendants = 0;
    uint64_t nSizeWithDescendants = 0;
    uint64_t nFeesWithDescendants = 0;
};

void CheckAncestors(MempoolData &expected_result, CTxMemPool &pool)
{
    CTxMemPool::txiter iter = pool.mapTx.find(expected_result.hash);
    if (iter == pool.mapTx.end())
    {
        printf("ERROR: tx %s not found in mempool\n", expected_result.hash.ToString().c_str());
        throw;
    }

    BOOST_CHECK_EQUAL(iter->GetCountWithAncestors(), expected_result.nCountWithAncestors);
    BOOST_CHECK_EQUAL(iter->GetSizeWithAncestors(), expected_result.nSizeWithAncestors);
    BOOST_CHECK_EQUAL(iter->GetSigOpCountWithAncestors(), expected_result.nSigopsWithAncestors);
    BOOST_CHECK_EQUAL(iter->GetModFeesWithAncestors(), expected_result.nFeesWithAncestors);
    BOOST_CHECK_EQUAL(iter->GetCountWithDescendants(), expected_result.nCountWithDescendants);
    BOOST_CHECK_EQUAL(iter->GetSizeWithDescendants(), expected_result.nSizeWithDescendants);
    BOOST_CHECK_EQUAL(iter->GetModFeesWithDescendants(), expected_result.nFeesWithDescendants);
}

void VerifyTxNotInMempool(MempoolData &expected_result, CTxMemPool &pool)
{
    CTxMemPool::txiter iter = pool.mapTx.find(expected_result.hash);
    if (iter != pool.mapTx.end())
    {
        printf("ERROR: tx %s was found in mempool when it should not be\n", expected_result.hash.ToString().c_str());
        throw;
    }
}

BOOST_FIXTURE_TEST_SUITE(mempool_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(MempoolUpdateChainStateTest)
{
    TestMemPoolEntryHelper entry;
    CTxMemPool pool(CFeeRate(0));
    pool.clear();

    /* Create a complex set of chained transactions and then update their state after removing some from the mempool.
       (The numbers indicate the tx number, ie. 1 == tx1)

    Chain1:

    1      2   3      4
    |      |   |      |
    5      6   7      8
     \    /     \    /
      \  /       \  /
       9          10      20
       | \        |       /
       |  \______ 11 ____/
       |          |\
       12         | \
      /|\        13 14      19
     / | \        | /       /
    15 16 17      18 ______/

    */

    // Chain:1 Transactions

    // tx1
    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx1.GetHash(), entry.Fee(1000LL).Priority(10.0).SigOps(1).FromTx(tx1));

    // tx2
    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * COIN;
    pool.addUnchecked(tx2.GetHash(), entry.Fee(2000LL).Priority(10.0).SigOps(1).FromTx(tx2));

    // tx3
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 3 * COIN;
    pool.addUnchecked(tx3.GetHash(), entry.Fee(3000LL).Priority(10.0).SigOps(1).FromTx(tx3));

    // tx4
    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 4 * COIN;
    pool.addUnchecked(tx4.GetHash(), entry.Fee(4000LL).Priority(10.0).SigOps(1).FromTx(tx4));


    // tx5 - child of tx1
    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vin.resize(1);
    tx5.vin[0].scriptSig = CScript() << OP_11;
    tx5.vin[0].prevout.hash = tx1.GetHash();
    tx5.vin[0].prevout.n = 0;
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx5.GetHash(), entry.Fee(1000LL).Priority(10.0).SigOps(1).FromTx(tx5));


    // tx6 - child of tx2
    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vin.resize(1);
    tx6.vin[0].scriptSig = CScript() << OP_11;
    tx6.vin[0].prevout.hash = tx2.GetHash();
    tx6.vin[0].prevout.n = 0;
    tx6.vout.resize(1);
    tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx6.vout[0].nValue = 2 * COIN;
    pool.addUnchecked(tx6.GetHash(), entry.Fee(2000LL).Priority(10.0).SigOps(1).FromTx(tx6));

    // tx7 - child of tx3
    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(1);
    tx7.vin[0].scriptSig = CScript() << OP_11;
    tx7.vin[0].prevout.hash = tx3.GetHash();
    tx7.vin[0].prevout.n = 0;
    tx7.vout.resize(1);
    tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[0].nValue = 3 * COIN;
    pool.addUnchecked(tx7.GetHash(), entry.Fee(3000LL).Priority(10.0).SigOps(1).FromTx(tx7));


    // tx8 - child of tx4
    CMutableTransaction tx8 = CMutableTransaction();
    tx8.vin.resize(1);
    tx8.vin[0].scriptSig = CScript() << OP_11;
    tx8.vin[0].prevout.hash = tx4.GetHash();
    tx8.vin[0].prevout.n = 0;
    tx8.vout.resize(1);
    tx8.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx8.vout[0].nValue = 4 * COIN;
    pool.addUnchecked(tx8.GetHash(), entry.Fee(4000LL).Priority(10.0).SigOps(1).FromTx(tx8));

    // tx9 - child of tx5 and tx6 and has two outputs
    CMutableTransaction tx9 = CMutableTransaction();
    tx9.vin.resize(2);
    tx9.vin[0].scriptSig = CScript() << OP_11;
    tx9.vin[0].prevout.hash = tx5.GetHash();
    tx9.vin[0].prevout.n = 0;
    tx9.vin[1].scriptSig = CScript() << OP_11;
    tx9.vin[1].prevout.hash = tx6.GetHash();
    tx9.vin[1].prevout.n = 0;
    tx9.vout.resize(2);
    tx9.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx9.vout[0].nValue = 1 * COIN;
    tx9.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx9.vout[1].nValue = 2 * COIN;
    pool.addUnchecked(tx9.GetHash(), entry.Fee(3000LL).Priority(10.0).SigOps(1).FromTx(tx9));

    // tx10 - child of tx7 and tx8 and has one output
    CMutableTransaction tx10 = CMutableTransaction();
    tx10.vin.resize(2);
    tx10.vin[0].scriptSig = CScript() << OP_11;
    tx10.vin[0].prevout.hash = tx7.GetHash();
    tx10.vin[0].prevout.n = 0;
    tx10.vin[1].scriptSig = CScript() << OP_11;
    tx10.vin[1].prevout.hash = tx8.GetHash();
    tx10.vin[1].prevout.n = 0;
    tx10.vout.resize(1);
    tx10.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx10.vout[0].nValue = 7 * COIN;
    pool.addUnchecked(tx10.GetHash(), entry.Fee(7000LL).SigOps(1).FromTx(tx10));

    // tx20
    CMutableTransaction tx20 = CMutableTransaction();
    tx20.vout.resize(1);
    tx20.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx20.vout[0].nValue = 5 * COIN;
    pool.addUnchecked(tx20.GetHash(), entry.Fee(5000LL).Priority(10.0).SigOps(1).FromTx(tx20));

    // tx11 - child of tx9, tx10 and tx20, and has two outputs
    CMutableTransaction tx11 = CMutableTransaction();
    tx11.vin.resize(3);
    tx11.vin[0].scriptSig = CScript() << OP_11;
    tx11.vin[0].prevout.hash = tx9.GetHash();
    tx11.vin[0].prevout.n = 1;
    tx11.vin[1].scriptSig = CScript() << OP_11;
    tx11.vin[1].prevout.hash = tx10.GetHash();
    tx11.vin[1].prevout.n = 0;
    tx11.vin[2].scriptSig = CScript() << OP_11;
    tx11.vin[2].prevout.hash = tx20.GetHash();
    tx11.vin[2].prevout.n = 0;
    tx11.vout.resize(2);
    tx11.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx11.vout[0].nValue = 1 * COIN;
    tx11.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx11.vout[1].nValue = 2 * COIN;
    pool.addUnchecked(tx11.GetHash(), entry.Fee(10000LL).SigOps(1).FromTx(tx11));

    // tx12 - child of tx9 and has three outputs
    CMutableTransaction tx12 = CMutableTransaction();
    tx12.vin.resize(1);
    tx12.vin[0].scriptSig = CScript() << OP_11;
    tx12.vin[0].prevout.hash = tx9.GetHash();
    tx12.vin[0].prevout.n = 0;
    tx12.vout.resize(3);
    tx12.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx12.vout[0].nValue = .5 * COIN;
    tx12.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx12.vout[1].nValue = .2 * COIN;
    tx12.vout[2].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx12.vout[2].nValue = .3 * COIN;
    pool.addUnchecked(tx12.GetHash(), entry.Fee(1000LL).Priority(10.0).SigOps(1).FromTx(tx12));

    // tx13 - child of tx11 and has one output
    CMutableTransaction tx13 = CMutableTransaction();
    tx13.vin.resize(1);
    tx13.vin[0].scriptSig = CScript() << OP_11;
    tx13.vin[0].prevout.hash = tx11.GetHash();
    tx13.vin[0].prevout.n = 0;
    tx13.vout.resize(1);
    tx13.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx13.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx13.GetHash(), entry.Fee(1000LL).SigOps(1).FromTx(tx13));


    // tx14 - child of tx11 and has one output
    CMutableTransaction tx14 = CMutableTransaction();
    tx14.vin.resize(1);
    tx14.vin[0].scriptSig = CScript() << OP_11;
    tx14.vin[0].prevout.hash = tx11.GetHash();
    tx14.vin[0].prevout.n = 1;
    tx14.vout.resize(1);
    tx14.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx14.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx14.GetHash(), entry.Fee(1000LL).SigOps(1).FromTx(tx14));

    // tx15 - child of tx12
    CMutableTransaction tx15 = CMutableTransaction();
    tx15.vin.resize(1);
    tx15.vin[0].scriptSig = CScript() << OP_11;
    tx15.vin[0].prevout.hash = tx12.GetHash();
    tx15.vin[0].prevout.n = 0;
    tx15.vout.resize(1);
    tx15.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx15.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx15.GetHash(), entry.Fee(500LL).SigOps(1).FromTx(tx15));

    // tx16 - child of tx12
    CMutableTransaction tx16 = CMutableTransaction();
    tx16.vin.resize(1);
    tx16.vin[0].scriptSig = CScript() << OP_11;
    tx16.vin[0].prevout.hash = tx12.GetHash();
    tx16.vin[0].prevout.n = 1;
    tx16.vout.resize(1);
    tx16.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx16.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx16.GetHash(), entry.Fee(200LL).SigOps(1).FromTx(tx16));

    // tx17 - child of tx12
    CMutableTransaction tx17 = CMutableTransaction();
    tx17.vin.resize(1);
    tx17.vin[0].scriptSig = CScript() << OP_11;
    tx17.vin[0].prevout.hash = tx12.GetHash();
    tx17.vin[0].prevout.n = 2;
    tx17.vout.resize(1);
    tx17.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx17.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx17.GetHash(), entry.Fee(300LL).SigOps(1).FromTx(tx17));

    // tx19
    CMutableTransaction tx19 = CMutableTransaction();
    tx19.vout.resize(1);
    tx19.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx19.vout[0].nValue = 6 * COIN;
    pool.addUnchecked(tx19.GetHash(), entry.Fee(6000LL).Priority(10.0).SigOps(1).FromTx(tx19));


    // tx18 - child of tx13, tx14 and 19, and has two outputs
    CMutableTransaction tx18 = CMutableTransaction();
    tx18.vin.resize(3);
    tx18.vin[0].scriptSig = CScript() << OP_11;
    tx18.vin[0].prevout.hash = tx13.GetHash();
    tx18.vin[0].prevout.n = 0;
    tx18.vin[1].scriptSig = CScript() << OP_11;
    tx18.vin[1].prevout.hash = tx14.GetHash();
    tx18.vin[1].prevout.n = 0;
    tx18.vin[2].scriptSig = CScript() << OP_11;
    tx18.vin[2].prevout.hash = tx19.GetHash();
    tx18.vin[2].prevout.n = 0;
    tx18.vout.resize(2);
    tx18.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx18.vout[0].nValue = 1 * COIN;
    tx18.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx18.vout[1].nValue = 2 * COIN;
    pool.addUnchecked(tx18.GetHash(), entry.Fee(2000LL).SigOps(1).FromTx(tx18));


    // Chain:2 Transactions
    /*

                            21                     27
                            |                      /
            ________________22_______________     /
           /         /      |      \         \   28
          /         /       23      \         \  /
         /         /       / \       \         \/
        31        32      24 25      33        29
         \         \       \ /       /         /\
          \         \       26      /         /  \
           \         \      |      /         /    \
           34_________\_____35____/_________/     30
                           / \
                          36 37
                           \ /
                            38
    */

    // tx21
    CMutableTransaction tx21 = CMutableTransaction();
    tx21.vout.resize(1);
    tx21.vout[0].scriptPubKey = CScript();
    tx21.vout[0].nValue = 100 * COIN;
    pool.addUnchecked(tx21.GetHash(), entry.Fee(100000LL).Priority(10.0).SigOps(1).FromTx(tx21));


    // tx22 - child of tx21 and has 5 outputs
    CMutableTransaction tx22 = CMutableTransaction();
    tx22.vin.resize(1);
    tx22.vin[0].scriptSig = CScript();
    tx22.vin[0].prevout.hash = tx21.GetHash();
    tx22.vin[0].prevout.n = 0;
    tx22.vout.resize(5);
    tx22.vout[0].scriptPubKey = CScript();
    tx22.vout[0].nValue = 20 * COIN;
    tx22.vout[1].scriptPubKey = CScript();
    tx22.vout[1].nValue = 20 * COIN;
    tx22.vout[2].scriptPubKey = CScript();
    tx22.vout[2].nValue = 20 * COIN;
    tx22.vout[3].scriptPubKey = CScript();
    tx22.vout[3].nValue = 20 * COIN;
    tx22.vout[4].scriptPubKey = CScript();
    tx22.vout[4].nValue = 20 * COIN;
    pool.addUnchecked(tx22.GetHash(), entry.Fee(100000LL).Priority(10.0).SigOps(1).FromTx(tx22));


    // tx23 - child of tx22 and has two outputs
    CMutableTransaction tx23 = CMutableTransaction();
    tx23.vin.resize(1);
    tx23.vin[0].scriptSig = CScript();
    tx23.vin[0].prevout.hash = tx22.GetHash();
    tx23.vin[0].prevout.n = 2;
    tx23.vout.resize(2);
    tx23.vout[0].scriptPubKey = CScript();
    tx23.vout[0].nValue = 10 * COIN;
    tx23.vout[1].scriptPubKey = CScript();
    tx23.vout[1].nValue = 10 * COIN;
    pool.addUnchecked(tx23.GetHash(), entry.Fee(20000LL).Priority(10.0).SigOps(1).FromTx(tx23));

    // tx24 - child of tx23 and has one output
    CMutableTransaction tx24 = CMutableTransaction();
    tx24.vin.resize(1);
    tx24.vin[0].scriptSig = CScript();
    tx24.vin[0].prevout.hash = tx23.GetHash();
    tx24.vin[0].prevout.n = 0;
    tx24.vout.resize(1);
    tx24.vout[0].scriptPubKey = CScript();
    tx24.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx24.GetHash(), entry.Fee(10000LL).SigOps(1).FromTx(tx24));

    // tx25 - child of tx23 and has one output
    CMutableTransaction tx25 = CMutableTransaction();
    tx25.vin.resize(1);
    tx25.vin[0].scriptSig = CScript();
    tx25.vin[0].prevout.hash = tx23.GetHash();
    tx25.vin[0].prevout.n = 1;
    tx25.vout.resize(1);
    tx25.vout[0].scriptPubKey = CScript();
    tx25.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx25.GetHash(), entry.Fee(10000LL).SigOps(1).FromTx(tx25));

    // tx26 - child of tx24 and tx25 and has one output
    CMutableTransaction tx26 = CMutableTransaction();
    tx26.vin.resize(2);
    tx26.vin[0].scriptSig = CScript();
    tx26.vin[0].prevout.hash = tx24.GetHash();
    tx26.vin[0].prevout.n = 0;
    tx26.vin[1].scriptSig = CScript();
    tx26.vin[1].prevout.hash = tx25.GetHash();
    tx26.vin[1].prevout.n = 0;
    tx26.vout.resize(1);
    tx26.vout[0].scriptPubKey = CScript();
    tx26.vout[0].nValue = 20 * COIN;
    pool.addUnchecked(tx26.GetHash(), entry.Fee(20000LL).SigOps(1).FromTx(tx26));

    // tx27
    CMutableTransaction tx27 = CMutableTransaction();
    tx27.vout.resize(1);
    tx27.vout[0].scriptPubKey = CScript();
    tx27.vout[0].nValue = 101 * COIN;
    pool.addUnchecked(tx27.GetHash(), entry.Fee(101000LL).Priority(10.0).SigOps(1).FromTx(tx27));

    // tx28 - child of tx27 and has one output
    CMutableTransaction tx28 = CMutableTransaction();
    tx28.vin.resize(1);
    tx28.vin[0].scriptSig = CScript();
    tx28.vin[0].prevout.hash = tx27.GetHash();
    tx28.vin[0].prevout.n = 0;
    tx28.vout.resize(1);
    tx28.vout[0].scriptPubKey = CScript();
    tx28.vout[0].nValue = 101 * COIN;
    pool.addUnchecked(tx28.GetHash(), entry.Fee(101000LL).SigOps(1).FromTx(tx28));

    // tx29 - child of tx22 and tx28 and has two outputs
    CMutableTransaction tx29 = CMutableTransaction();
    tx29.vin.resize(2);
    tx29.vin[0].scriptSig = CScript();
    tx29.vin[0].prevout.hash = tx22.GetHash();
    tx29.vin[0].prevout.n = 4;
    tx29.vin[1].scriptSig = CScript();
    tx29.vin[1].prevout.hash = tx28.GetHash();
    tx29.vin[1].prevout.n = 0;
    tx29.vout.resize(2);
    tx29.vout[0].scriptPubKey = CScript();
    tx29.vout[0].nValue = 100 * COIN;
    tx29.vout[1].scriptPubKey = CScript();
    tx29.vout[1].nValue = 101 * COIN;
    pool.addUnchecked(tx29.GetHash(), entry.Fee(201000LL).Priority(10.0).SigOps(1).FromTx(tx29));

    // tx30 - child of tx29 and has one output
    CMutableTransaction tx30 = CMutableTransaction();
    tx30.vin.resize(1);
    tx30.vin[0].scriptSig = CScript();
    tx30.vin[0].prevout.hash = tx29.GetHash();
    tx30.vin[0].prevout.n = 1;
    tx30.vout.resize(1);
    tx30.vout[0].scriptPubKey = CScript();
    tx30.vout[0].nValue = 101 * COIN;
    pool.addUnchecked(tx30.GetHash(), entry.Fee(101000LL).SigOps(1).FromTx(tx30));

    // tx31 - child of tx22 and has one output
    CMutableTransaction tx31 = CMutableTransaction();
    tx31.vin.resize(1);
    tx31.vin[0].scriptSig = CScript();
    tx31.vin[0].prevout.hash = tx22.GetHash();
    tx31.vin[0].prevout.n = 0;
    tx31.vout.resize(1);
    tx31.vout[0].scriptPubKey = CScript();
    tx31.vout[0].nValue = 20 * COIN;
    pool.addUnchecked(tx31.GetHash(), entry.Fee(20000LL).SigOps(1).FromTx(tx31));

    // tx32 - child of tx22 and has one output
    CMutableTransaction tx32 = CMutableTransaction();
    tx32.vin.resize(1);
    tx32.vin[0].scriptSig = CScript();
    tx32.vin[0].prevout.hash = tx22.GetHash();
    tx32.vin[0].prevout.n = 1;
    tx32.vout.resize(1);
    tx32.vout[0].scriptPubKey = CScript();
    tx32.vout[0].nValue = 20 * COIN;
    pool.addUnchecked(tx32.GetHash(), entry.Fee(20000LL).SigOps(1).FromTx(tx32));

    // tx33 - child of tx22 and has one output
    CMutableTransaction tx33 = CMutableTransaction();
    tx33.vin.resize(1);
    tx33.vin[0].scriptSig = CScript();
    tx33.vin[0].prevout.hash = tx22.GetHash();
    tx33.vin[0].prevout.n = 3;
    tx33.vout.resize(1);
    tx33.vout[0].scriptPubKey = CScript();
    tx33.vout[0].nValue = 20 * COIN;
    pool.addUnchecked(tx33.GetHash(), entry.Fee(20000LL).SigOps(1).FromTx(tx33));

    // tx34 - child of tx31 and has one output
    CMutableTransaction tx34 = CMutableTransaction();
    tx34.vin.resize(1);
    tx34.vin[0].scriptSig = CScript();
    tx34.vin[0].prevout.hash = tx31.GetHash();
    tx34.vin[0].prevout.n = 0;
    tx34.vout.resize(1);
    tx34.vout[0].scriptPubKey = CScript();
    tx34.vout[0].nValue = 20 * COIN;
    pool.addUnchecked(tx34.GetHash(), entry.Fee(20000LL).SigOps(1).FromTx(tx34));

    // tx35 - child of tx26, tx29, tx32, tx33, tx34 and has two outputs
    CMutableTransaction tx35 = CMutableTransaction();
    tx35.vin.resize(5);
    tx35.vin[0].scriptSig = CScript();
    tx35.vin[0].prevout.hash = tx26.GetHash();
    tx35.vin[0].prevout.n = 0;
    tx35.vin[1].scriptSig = CScript();
    tx35.vin[1].prevout.hash = tx29.GetHash();
    tx35.vin[1].prevout.n = 0;
    tx35.vin[2].scriptSig = CScript();
    tx35.vin[2].prevout.hash = tx32.GetHash();
    tx35.vin[2].prevout.n = 0;
    tx35.vin[3].scriptSig = CScript();
    tx35.vin[3].prevout.hash = tx33.GetHash();
    tx35.vin[3].prevout.n = 0;
    tx35.vin[4].scriptSig = CScript();
    tx35.vin[4].prevout.hash = tx34.GetHash();
    tx35.vin[4].prevout.n = 0;
    tx35.vout.resize(2);
    tx35.vout[0].scriptPubKey = CScript();
    tx35.vout[0].nValue = 200 * COIN;
    tx35.vout[1].scriptPubKey = CScript();
    tx35.vout[1].nValue = 81 * COIN;
    pool.addUnchecked(tx35.GetHash(), entry.Fee(281000LL).Priority(10.0).SigOps(1).FromTx(tx35));

    // tx36 - child of tx35 and has one output
    CMutableTransaction tx36 = CMutableTransaction();
    tx36.vin.resize(1);
    tx36.vin[0].scriptSig = CScript();
    tx36.vin[0].prevout.hash = tx35.GetHash();
    tx36.vin[0].prevout.n = 0;
    tx36.vout.resize(1);
    tx36.vout[0].scriptPubKey = CScript();
    tx36.vout[0].nValue = 200 * COIN;
    pool.addUnchecked(tx36.GetHash(), entry.Fee(200000LL).SigOps(1).FromTx(tx36));

    // tx37 - child of tx35 and has one output
    CMutableTransaction tx37 = CMutableTransaction();
    tx37.vin.resize(1);
    tx37.vin[0].scriptSig = CScript();
    tx37.vin[0].prevout.hash = tx35.GetHash();
    tx37.vin[0].prevout.n = 1;
    tx37.vout.resize(1);
    tx37.vout[0].scriptPubKey = CScript();
    tx37.vout[0].nValue = 81 * COIN;
    pool.addUnchecked(tx37.GetHash(), entry.Fee(81000LL).SigOps(1).FromTx(tx37));

    // tx38 - child of tx36 and tx37 and has one output
    CMutableTransaction tx38 = CMutableTransaction();
    tx38.vin.resize(2);
    tx38.vin[0].scriptSig = CScript();
    tx38.vin[0].prevout.hash = tx36.GetHash();
    tx38.vin[0].prevout.n = 0;
    tx38.vin[1].scriptSig = CScript();
    tx38.vin[1].prevout.hash = tx37.GetHash();
    tx38.vin[1].prevout.n = 0;
    tx38.vout.resize(1);
    tx38.vout[0].scriptPubKey = CScript();
    tx38.vout[0].nValue = 281 * COIN;
    pool.addUnchecked(tx38.GetHash(), entry.Fee(2810000LL).SigOps(1).FromTx(tx38));

    /*  Simple chain with the purpose to test and edge condition where txchaintips become decendants of other
        txchaintips. We will mine tx39, tx44, and tx47 which means that there will be
        three txchaintips, one at tx41, one at tx42, and one at 45, with both 42 and 45 in the same chain as 41,
        thus being an edge condition we have to account for.

    Chain3:  we will mine txn 39, 44 and 47

           44
           /
    39 40 41
     \ | / \
      \|/   \   47
       42   46  /
       |     \ /
       43    45
       |       \
       48      49

    */

    // tx39
    CMutableTransaction tx39 = CMutableTransaction();
    tx39.vout.resize(1);
    tx39.vout[0].scriptPubKey = CScript() << OP_11;
    tx39.vout[0].nValue = 1 * COIN;
    pool.addUnchecked(tx39.GetHash(), entry.Fee(1000LL).Priority(10.0).SigOps(1).FromTx(tx39));

    // tx40
    CMutableTransaction tx40 = CMutableTransaction();
    tx40.vout.resize(1);
    tx40.vout[0].scriptPubKey = CScript() << OP_11;
    tx40.vout[0].nValue = 2 * COIN;
    pool.addUnchecked(tx40.GetHash(), entry.Fee(2000LL).Priority(10.0).SigOps(1).FromTx(tx40));

    // tx44
    CMutableTransaction tx44 = CMutableTransaction();
    tx44.vout.resize(1);
    tx44.vout[0].scriptPubKey = CScript() << OP_11;
    tx44.vout[0].nValue = 4 * COIN;
    pool.addUnchecked(tx44.GetHash(), entry.Fee(4000LL).Priority(10.0).SigOps(1).FromTx(tx44));

    // tx41
    CMutableTransaction tx41 = CMutableTransaction();
    tx41.vin.resize(1);
    tx41.vin[0].scriptSig = CScript();
    tx41.vin[0].prevout.hash = tx44.GetHash();
    tx41.vin[0].prevout.n = 0;
    tx41.vout.resize(2);
    tx41.vout[0].scriptPubKey = CScript() << OP_11;
    tx41.vout[0].nValue = 3 * COIN;
    tx41.vout[1].scriptPubKey = CScript() << OP_11;
    tx41.vout[1].nValue = 3 * COIN;
    pool.addUnchecked(tx41.GetHash(), entry.Fee(6000LL).Priority(10.0).SigOps(1).FromTx(tx41));

    // tx42 - child of tx39, tx40 and tx41 and has one output
    CMutableTransaction tx42 = CMutableTransaction();
    tx42.vin.resize(3);
    tx42.vin[0].scriptSig = CScript();
    tx42.vin[0].prevout.hash = tx39.GetHash();
    tx42.vin[0].prevout.n = 0;
    tx42.vin[1].scriptSig = CScript();
    tx42.vin[1].prevout.hash = tx40.GetHash();
    tx42.vin[1].prevout.n = 0;
    tx42.vin[2].scriptSig = CScript();
    tx42.vin[2].prevout.hash = tx41.GetHash();
    tx42.vin[2].prevout.n = 0;
    tx42.vout.resize(1);
    tx42.vout[0].scriptPubKey = CScript();
    tx42.vout[0].nValue = 6 * COIN;
    pool.addUnchecked(tx42.GetHash(), entry.Fee(6000LL).SigOps(1).FromTx(tx42));

    // tx43 - child of tx42 and has one output
    CMutableTransaction tx43 = CMutableTransaction();
    tx43.vin.resize(1);
    tx43.vin[0].scriptSig = CScript();
    tx43.vin[0].prevout.hash = tx42.GetHash();
    tx43.vin[0].prevout.n = 0;
    tx43.vout.resize(1);
    tx43.vout[0].scriptPubKey = CScript();
    tx43.vout[0].nValue = 12 * COIN;
    pool.addUnchecked(tx43.GetHash(), entry.Fee(12000LL).SigOps(1).FromTx(tx43));

    // tx48 child of tx43
    CMutableTransaction tx48 = CMutableTransaction();
    tx48.vin.resize(1);
    tx48.vin[0].scriptSig = CScript();
    tx48.vin[0].prevout.hash = tx43.GetHash();
    tx48.vin[0].prevout.n = 0;
    tx48.vout.resize(1);
    tx48.vout[0].scriptPubKey = CScript();
    tx48.vout[0].nValue = 15 * COIN;
    pool.addUnchecked(tx48.GetHash(), entry.Fee(15000LL).Priority(10.0).SigOps(1).FromTx(tx48));

    // tx47
    CMutableTransaction tx47 = CMutableTransaction();
    tx47.vout.resize(1);
    tx47.vout[0].scriptPubKey = CScript();
    tx47.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx47.GetHash(), entry.Fee(10000LL).Priority(10.0).SigOps(1).FromTx(tx47));

    // tx46 child of tx41
    CMutableTransaction tx46 = CMutableTransaction();
    tx46.vin.resize(1);
    tx46.vin[0].scriptSig = CScript();
    tx46.vin[0].prevout.hash = tx41.GetHash();
    tx46.vin[0].prevout.n = 1;
    tx46.vout.resize(1);
    tx46.vout[0].scriptPubKey = CScript();
    tx46.vout[0].nValue = 3 * COIN;
    pool.addUnchecked(tx46.GetHash(), entry.Fee(3000LL).Priority(10.0).SigOps(1).FromTx(tx46));


    // tx45 - child of tx46 and tx47 and has one output
    CMutableTransaction tx45 = CMutableTransaction();
    tx45.vin.resize(2);
    tx45.vin[0].scriptSig = CScript();
    tx45.vin[0].prevout.hash = tx46.GetHash();
    tx45.vin[0].prevout.n = 0;
    tx45.vin[1].scriptSig = CScript();
    tx45.vin[1].prevout.hash = tx47.GetHash();
    tx45.vin[1].prevout.n = 0;
    tx45.vout.resize(1);
    tx45.vout[0].scriptPubKey = CScript();
    tx45.vout[0].nValue = 12 * COIN;
    pool.addUnchecked(tx45.GetHash(), entry.Fee(12000LL).SigOps(1).FromTx(tx45));

    // tx49 child of tx45
    CMutableTransaction tx49 = CMutableTransaction();
    tx49.vin.resize(1);
    tx49.vin[0].scriptSig = CScript();
    tx49.vin[0].prevout.hash = tx45.GetHash();
    tx49.vin[0].prevout.n = 0;
    tx49.vout.resize(1);
    tx49.vout[0].scriptPubKey = CScript();
    tx49.vout[0].nValue = 14 * COIN;
    pool.addUnchecked(tx49.GetHash(), entry.Fee(14000LL).Priority(10.0).SigOps(1).FromTx(tx49));

    // Validate the current state is correct
    /* clang-format off */
    BOOST_CHECK_EQUAL(pool.size(), 49);
    std::vector<MempoolData> txns_expected =
    {
        // Chain1:
        {tx1.GetHash(), 1, 21, 1, 1000, 11, 916, 21000},
        {tx2.GetHash(), 1, 21, 1, 2000, 11, 916, 23000},
        {tx3.GetHash(), 1, 21, 1, 3000, 7, 631, 27000},
        {tx4.GetHash(), 1, 21, 1, 4000, 7, 631, 29000},
        {tx5.GetHash(), 2, 84, 2, 2000, 10, 895, 20000},
        {tx6.GetHash(), 2, 84, 2, 4000, 10, 895, 21000},
        {tx7.GetHash(), 2, 84, 2, 6000, 6, 610, 24000},
        {tx8.GetHash(), 2, 84, 2, 8000, 6, 610, 25000},
        {tx9.GetHash(), 5, 284, 5, 9000, 9, 832, 19000},
        {tx10.GetHash(), 5, 273, 5, 21000, 5, 547, 21000},
        {tx11.GetHash(), 12, 736, 12, 45000, 4, 442, 14000},
        {tx12.GetHash(), 6, 369, 6, 10000, 4, 274, 2000},
        {tx13.GetHash(), 13, 799, 13, 46000, 2, 221, 3000},
        {tx14.GetHash(), 13, 799, 13, 46000, 2, 221, 3000},
        {tx15.GetHash(), 7, 432, 7, 10500, 1, 63, 500},
        {tx16.GetHash(), 7, 432, 7, 10200, 1, 63, 200},
        {tx17.GetHash(), 7, 432, 7, 10300, 1, 63, 300},
        {tx18.GetHash(), 16, 1041, 16, 55000, 1, 158, 2000},
        {tx19.GetHash(), 1, 21, 1, 6000, 2, 179, 8000},
        {tx20.GetHash(), 1, 21, 1, 5000, 5, 463, 19000},

        // Chain2:
        {tx21.GetHash(), 1, 19, 1, 100000, 16, 1269, 4014000},
        {tx22.GetHash(), 2, 115, 2, 200000, 15, 1250, 3914000},
        {tx23.GetHash(), 3, 184, 3, 220000, 8, 744, 3432000},
        {tx24.GetHash(), 4, 244, 4, 230000, 6, 615, 3402000},
        {tx25.GetHash(), 4, 244, 4, 230000, 6, 615, 3402000},
        {tx26.GetHash(), 6, 405, 6, 260000, 5, 555, 3392000},
        {tx27.GetHash(), 1, 19, 1, 101000, 8, 703, 3876000},
        {tx28.GetHash(), 2, 79, 2, 202000, 7, 684, 3775000},
        {tx29.GetHash(), 5, 304, 5, 603000, 6, 624, 3674000},
        {tx30.GetHash(), 6, 364, 6, 704000, 1, 60, 101000},
        {tx31.GetHash(), 3, 175, 3, 220000, 6, 574, 3412000},
        {tx32.GetHash(), 3, 175, 3, 220000, 5, 514, 3392000},
        {tx33.GetHash(), 3, 175, 3, 220000, 5, 514, 3392000},
        {tx34.GetHash(), 4, 235, 4, 240000, 5, 514, 3392000},
        {tx35.GetHash(), 14, 1067, 14, 1024000, 4, 454, 3372000},
        {tx36.GetHash(), 15, 1127, 15, 1224000, 2, 161, 3010000},
        {tx37.GetHash(), 15, 1127, 15, 1105000, 2, 161, 2891000},
        {tx38.GetHash(), 17, 1288, 17, 4115000, 1, 101, 2810000},

        // Chain3:
        {tx39.GetHash(), 1, 20, 1, 1000, 4, 282, 34000},
        {tx40.GetHash(), 1, 20, 1, 2000, 4, 282, 35000},
        {tx41.GetHash(), 2, 91, 2, 10000, 7, 554, 68000},
        {tx42.GetHash(), 5, 273, 5, 19000, 3, 262, 33000},
        {tx43.GetHash(), 6, 333, 6, 31000, 2, 120, 27000},
        {tx44.GetHash(), 1, 20, 1, 4000, 8, 574, 72000},
        {tx45.GetHash(), 5, 271, 5, 35000, 2, 161, 26000},
        {tx46.GetHash(), 3, 151, 3, 13000, 3, 221, 29000},
        {tx47.GetHash(), 1, 19, 1, 10000, 3, 180, 36000},
        {tx48.GetHash(), 7, 393, 7, 46000, 1, 60, 15000},
        {tx49.GetHash(), 6, 331, 6, 49000, 1, 60, 14000}
    };
    /* clang-format on */
    for (size_t i = 0; i < txns_expected.size(); i++)
    {
        CTxMemPool::txiter iter = pool.mapTx.find(txns_expected[i].hash);
        if (iter == pool.mapTx.end())
        {
            printf("ERROR: tx %s not found in mempool\n", txns_expected[i].hash.ToString().c_str());
            throw;
        }

        /*
        printf(
            "tx%ld countwanc %ld sizewanc %ld sigopswanc %u feeswanc %ld countwdesc %ld sizewdesc %ld feeswdesc %ld\n",
            i + 1, iter->GetCountWithAncestors(), iter->GetSizeWithAncestors(), iter->GetSigOpCountWithAncestors(),
            iter->GetModFeesWithAncestors(), iter->GetCountWithDescendants(), iter->GetSizeWithDescendants(),
            iter->GetModFeesWithDescendants());
        printf("tx%d: hash %s\n", (int)(i + 1), iter->GetTx().GetHash().ToString().c_str());
        */

        CheckAncestors(txns_expected[i], pool);
    }


    /* Do a removeforblock using tx1,tx2,tx3 and tx4 as having been mined and are in the block

    The Resulting in mempool chain should appear as shown below:

    Chain1:

    5      6   7      8
     \    /     \    /
      \  /       \  /
       9          10      20 (unmined chain so it has no entry in txnchaintips)
       | \        |       /
       |  \______ 11 ____/
       |          |\
       12         | \
      /|\        13 14     19 (unmined chain so it has no entry in txnchaintips)
     / | \        | /       /
    15 16 17      18 ______/


    Chain2:
                                                   27 (unmined chain so it has no entry in txnchaintips)
                                                   /
            ________________22_______________     /
           /         /      |      \         \   28
          /         /       23      \         \  /
         /         /       / \       \         \/
        31        32      24 25      33        29
         \         \       \ /       /         /\
          \         \       26      /         /  \
           \         \      |      /         /    \
           34_________\_____35____/_________/     30
                           / \
                          36 37
                           \ /
                            38


    Chain3:


       40 41   (39,44 and 47 were mined with 40 remaining an unmined chain, and 42,41,and 45 become txnchaintips)
       | / \
       |/   \
       42   46
       |     \
       43    45
       |       \
       48      49

    */

    // Add txns that will be mined
    std::vector<CTransactionRef> vtx;
    // Chain1:
    vtx.push_back(MakeTransactionRef(tx1));
    vtx.push_back(MakeTransactionRef(tx2));
    vtx.push_back(MakeTransactionRef(tx3));
    vtx.push_back(MakeTransactionRef(tx4));
    // Chain2:
    vtx.push_back(MakeTransactionRef(tx21));
    // Chain3:
    vtx.push_back(MakeTransactionRef(tx39));
    vtx.push_back(MakeTransactionRef(tx44));
    vtx.push_back(MakeTransactionRef(tx47));

    // Now assume they were mined and do a removeForBlock()
    std::list<CTransactionRef> dummy;
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 41);

    // Validate the new state is correct
    /* clang-format off */
    std::vector<MempoolData> txns_result =
    {
        // Chain1:
        {tx1.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx2.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx3.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx4.GetHash(), 0, 0, 0, 0, 0, 0, 0},

        {tx5.GetHash(), 1, 63, 1, 1000, 10, 895, 20000},
        {tx6.GetHash(), 1, 63, 1, 2000, 10, 895, 21000},
        {tx7.GetHash(), 1, 63, 1, 3000, 6, 610, 24000},
        {tx8.GetHash(), 1, 63, 1, 4000, 6, 610, 25000},
        {tx9.GetHash(), 3, 242, 3, 6000, 9, 832, 19000},
        {tx10.GetHash(), 3, 231, 3, 14000, 5, 547, 21000},
        {tx11.GetHash(), 8, 652, 8, 35000, 4, 442, 14000},
        {tx12.GetHash(), 4, 327, 4, 7000, 4, 274, 2000},
        {tx13.GetHash(), 9, 715, 9, 36000, 2, 221, 3000},
        {tx14.GetHash(), 9, 715, 9, 36000, 2, 221, 3000},
        {tx15.GetHash(), 5, 390, 5, 7500, 1, 63, 500},
        {tx16.GetHash(), 5, 390, 5, 7200, 1, 63, 200},
        {tx17.GetHash(), 5, 390, 5, 7300, 1, 63, 300},
        {tx18.GetHash(), 12, 957, 12, 45000, 1, 158, 2000},
        {tx19.GetHash(), 1, 21, 1, 6000, 2, 179, 8000},
        {tx20.GetHash(), 1, 21, 1, 5000, 5, 463, 19000},

        // Chain2:
        {tx21.GetHash(), 0, 0, 0, 0, 0, 0, 0},

        {tx22.GetHash(), 1, 96, 1, 100000, 15, 1250, 3914000},
        {tx23.GetHash(), 2, 165, 2, 120000, 8, 744, 3432000},
        {tx24.GetHash(), 3, 225, 3, 130000, 6, 615, 3402000},
        {tx25.GetHash(), 3, 225, 3, 130000, 6, 615, 3402000},
        {tx26.GetHash(), 5, 386, 5, 160000, 5, 555, 3392000},
        {tx27.GetHash(), 1, 19, 1, 101000, 8, 703, 3876000},
        {tx28.GetHash(), 2, 79, 2, 202000, 7, 684, 3775000},
        {tx29.GetHash(), 4, 285, 4, 503000, 6, 624, 3674000},
        {tx30.GetHash(), 5, 345, 5, 604000, 1, 60, 101000},
        {tx31.GetHash(), 2, 156, 2, 120000, 6, 574, 3412000},
        {tx32.GetHash(), 2, 156, 2, 120000, 5, 514, 3392000},
        {tx33.GetHash(), 2, 156, 2, 120000, 5, 514, 3392000},
        {tx34.GetHash(), 3, 216, 3, 140000, 5, 514, 3392000},
        {tx35.GetHash(), 13, 1048, 13, 924000, 4, 454, 3372000},
        {tx36.GetHash(), 14, 1108, 14, 1124000, 2, 161, 3010000},
        {tx37.GetHash(), 14, 1108, 14, 1005000, 2, 161, 2891000},
        {tx38.GetHash(), 16, 1269, 16, 4015000, 1, 101, 2810000},

        // Chain3:
        {tx39.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx40.GetHash(), 1, 20, 1, 2000, 4, 282, 35000},
        {tx41.GetHash(), 1, 71, 1, 6000, 7, 554, 68000},
        {tx42.GetHash(), 3, 233, 3, 14000, 3, 262, 33000},
        {tx43.GetHash(), 4, 293, 4, 26000, 2, 120, 27000},
        {tx44.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx45.GetHash(), 3, 232, 3, 21000, 2, 161, 26000},
        {tx46.GetHash(), 2, 131, 2, 9000, 3, 221, 29000},
        {tx47.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx48.GetHash(), 5, 353, 5, 41000, 1, 60, 15000},
        {tx49.GetHash(), 4, 292, 4, 35000, 1, 60, 14000}
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result.size(); i++)
    {
        if (i < 4 || i == 20 || i == 38 || i == 43 || i == 46)
        {
            VerifyTxNotInMempool(txns_result[i], pool);
            continue;
        }
        CheckAncestors(txns_result[i], pool);
    }

    // Mine two transactions which end up giving us the same txnchaintip.
    vtx.push_back(MakeTransactionRef(tx40));
    vtx.push_back(MakeTransactionRef(tx41));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 39);


    /* clang-format off */
    std::vector<MempoolData> txns_result2 =
    {
        // Chain3:
        {tx39.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx40.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx41.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx42.GetHash(), 1, 142, 1, 6000, 3, 262, 33000},
        {tx43.GetHash(), 2, 202, 2, 18000, 2, 120, 27000},
        {tx44.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx45.GetHash(), 2, 161, 2, 15000, 2, 161, 26000},
        {tx46.GetHash(), 1, 60, 1, 3000, 3, 221, 29000},
        {tx47.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx48.GetHash(), 3, 262, 3, 33000, 1, 60, 15000},
        {tx49.GetHash(), 3, 221, 3, 29000, 1, 60, 14000}
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result2.size(); i++)
    {
        if (i <= 2 || i == 5 || i == 8)
        {
            VerifyTxNotInMempool(txns_result2[i], pool);
            continue;
        }

        CheckAncestors(txns_result2[i], pool);
    }

    // Starting to simulate mining all the rest of the transactions in the chains defined in the
    // above tests and following that with a mempool consistency
    vtx.push_back(MakeTransactionRef(tx5));
    vtx.push_back(MakeTransactionRef(tx6));
    vtx.push_back(MakeTransactionRef(tx7));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 36);

    /* clang-format off */
    std::vector<MempoolData> txns_result3 =
    {
        // Chain1:
        {tx1.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx2.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx3.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx4.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx5.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx6.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx7.GetHash(), 0, 0, 0, 0, 0, 0, 0},

        {tx8.GetHash(), 1,63, 1, 4000, 6, 610, 25000},
        {tx9.GetHash(), 1,116 , 1, 3000, 9, 832, 19000},
        {tx10.GetHash(), 2, 168, 2, 11000, 5, 547, 21000},
        {tx11.GetHash(), 5, 463, 5, 29000, 4, 442, 14000},
        {tx12.GetHash(), 2, 201, 2, 4000, 4, 274, 2000},
        {tx13.GetHash(), 6, 526, 6, 30000, 2, 221, 3000},
        {tx14.GetHash(), 6, 526, 6, 30000, 2, 221, 3000},
        {tx15.GetHash(), 3, 264, 3, 4500, 1, 63, 500},
        {tx16.GetHash(), 3, 264, 3, 4200, 1, 63, 200},
        {tx17.GetHash(), 3, 264, 3, 4300, 1, 63, 300},
        {tx18.GetHash(), 9, 768, 9, 39000, 1, 158, 2000},
        {tx19.GetHash(), 1, 21, 1, 6000, 2, 179, 8000},
        {tx20.GetHash(), 1, 21, 1, 5000, 5, 463, 19000},
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result3.size(); i++)
    {
        if (i < 7)
        {
            VerifyTxNotInMempool(txns_result3[i], pool);
            continue;
        }
        CheckAncestors(txns_result3[i], pool);
    }


    vtx.push_back(MakeTransactionRef(tx8));
    vtx.push_back(MakeTransactionRef(tx9));
    vtx.push_back(MakeTransactionRef(tx10));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 33);

    /* clang-format off */
    std::vector<MempoolData> txns_result4 =
    {
        // Chain1:
        {tx1.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx2.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx3.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx4.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx5.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx6.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx7.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx8.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx9.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx10.GetHash(), 0, 0, 0, 0, 0, 0, 0},

        {tx11.GetHash(), 2, 179, 2, 15000, 4, 442, 14000},
        {tx12.GetHash(), 1, 85, 1, 1000, 4, 274, 2000},
        {tx13.GetHash(), 3, 242, 3, 16000, 2, 221, 3000},
        {tx14.GetHash(), 3, 242, 3, 16000, 2, 221, 3000},
        {tx15.GetHash(), 2, 148, 2, 1500, 1, 63, 500},
        {tx16.GetHash(), 2, 148, 2, 1200, 1, 63, 200},
        {tx17.GetHash(), 2, 148, 2, 1300, 1, 63, 300},
        {tx18.GetHash(), 6, 484, 6, 25000, 1, 158, 2000},
        {tx19.GetHash(), 1, 21, 1, 6000, 2, 179, 8000},
        {tx20.GetHash(), 1, 21, 1, 5000, 5, 463, 19000},
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result4.size(); i++)
    {
        if (i < 10)
        {
            VerifyTxNotInMempool(txns_result4[i], pool);
            continue;
        }
        CheckAncestors(txns_result4[i], pool);
    }


    vtx.push_back(MakeTransactionRef(tx11));
    vtx.push_back(MakeTransactionRef(tx14));
    vtx.push_back(MakeTransactionRef(tx20));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 30);

    /* clang-format off */
    std::vector<MempoolData> txns_result5 =
    {
        // Chain1:
        {tx1.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx2.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx3.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx4.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx5.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx6.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx7.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx8.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx9.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx10.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx11.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx12.GetHash(), 1, 85, 1, 1000, 4, 274, 2000},
        {tx13.GetHash(), 1, 63, 1, 1000, 2, 221, 3000},
        {tx14.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx15.GetHash(), 2, 148, 2, 1500, 1, 63, 500},
        {tx16.GetHash(), 2, 148, 2, 1200, 1, 63, 200},
        {tx17.GetHash(), 2, 148, 2, 1300, 1, 63, 300},
        {tx18.GetHash(), 3, 242, 3, 9000, 1, 158, 2000},
        {tx19.GetHash(), 1, 21, 1, 6000, 2, 179, 8000},
        {tx20.GetHash(), 0, 0, 0, 0, 0, 0, 0}
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result5.size(); i++)
    {
        if (i < 11 || i == 13 || i == 19)
        {
            VerifyTxNotInMempool(txns_result5[i], pool);
            continue;
        }
        CheckAncestors(txns_result5[i], pool);
    }

    vtx.push_back(MakeTransactionRef(tx12));
    vtx.push_back(MakeTransactionRef(tx13));
    vtx.push_back(MakeTransactionRef(tx15));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 27);

    /* clang-format off */
    std::vector<MempoolData> txns_result6 =
    {
        // Chain1:
        {tx1.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx2.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx3.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx4.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx5.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx6.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx7.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx8.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx9.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx10.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx11.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx12.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx13.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx14.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx15.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx16.GetHash(), 1, 63, 1, 200, 1, 63, 200},
        {tx17.GetHash(), 1, 63, 1, 300, 1, 63, 300},
        {tx18.GetHash(), 2, 179, 2, 8000, 1, 158, 2000},
        {tx19.GetHash(), 1, 21, 1, 6000, 2, 179, 8000},
        {tx20.GetHash(), 0, 0, 0, 0, 0, 0, 0}
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result6.size(); i++)
    {
        if (i < 15 || i == 19)
        {
            VerifyTxNotInMempool(txns_result6[i], pool);
            continue;
        }
        CheckAncestors(txns_result6[i], pool);
    }

    // The following is one of the most important edge conditions. Where we remove the first transaction
    // in a graph that has an enclosure. An enclosure being where a transaction has many outputs
    // and eventually results in other transactions that are inputs to a single transactions.
    /*
       for example:
                            22
                            |
                            23
                           / \
                          24 25
                           \ /
                            26

       after mining 22 and 23 becomes:


                          24 25
                           \ /
                            26
    */

    vtx.push_back(MakeTransactionRef(tx22));
    vtx.push_back(MakeTransactionRef(tx23));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 25);

    /* clang-format off */
    std::vector<MempoolData> txns_result7 =
    {
        // Chain2:
        {tx21.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx22.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx23.GetHash(), 0, 0, 0, 0, 0, 0, 0},

        {tx24.GetHash(), 1, 60, 1, 10000, 6, 615, 3402000},
        {tx25.GetHash(), 1, 60, 1, 10000, 6, 615, 3402000},
        {tx26.GetHash(), 3, 221, 3, 40000, 5, 555, 3392000},
        {tx27.GetHash(), 1, 19, 1, 101000, 8, 703, 3876000},
        {tx28.GetHash(), 2, 79, 2, 202000, 7, 684, 3775000},
        {tx29.GetHash(), 3, 189, 3, 403000, 6, 624, 3674000},
        {tx30.GetHash(), 4, 249, 4, 504000, 1, 60, 101000},
        {tx31.GetHash(), 1, 60, 1, 20000, 6, 574, 3412000},
        {tx32.GetHash(), 1, 60, 1, 20000, 5, 514, 3392000},
        {tx33.GetHash(), 1, 60, 1, 20000, 5, 514, 3392000},
        {tx34.GetHash(), 2, 120, 2, 40000, 5, 514, 3392000},
        {tx35.GetHash(), 11, 883, 11, 804000, 4, 454, 3372000},
        {tx36.GetHash(), 12, 943, 12, 1004000, 2, 161, 3010000},
        {tx37.GetHash(), 12, 943, 12, 885000, 2, 161, 2891000},
        {tx38.GetHash(), 14, 1104, 14, 3895000, 1, 101, 2810000},
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result7.size(); i++)
    {
        if (i <= 2)
        {
            VerifyTxNotInMempool(txns_result7[i], pool);
            continue;
        }
        CheckAncestors(txns_result7[i], pool);
    }


    vtx.push_back(MakeTransactionRef(tx31));
    vtx.push_back(MakeTransactionRef(tx33));
    vtx.push_back(MakeTransactionRef(tx34));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 22);

    /* clang-format off */
    std::vector<MempoolData> txns_result8 =
    {
        // Chain2:
        {tx21.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx22.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx23.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx24.GetHash(), 1, 60, 1, 10000, 6, 615, 3402000},
        {tx25.GetHash(), 1, 60, 1, 10000, 6, 615, 3402000},
        {tx26.GetHash(), 3, 221, 3, 40000, 5, 555, 3392000},
        {tx27.GetHash(), 1, 19, 1, 101000, 8, 703, 3876000},
        {tx28.GetHash(), 2, 79, 2, 202000, 7, 684, 3775000},
        {tx29.GetHash(), 3, 189, 3, 403000, 6, 624, 3674000},
        {tx30.GetHash(), 4, 249, 4, 504000, 1, 60, 101000},
        {tx31.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx32.GetHash(), 1, 60, 1, 20000, 5, 514, 3392000},
        {tx33.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx34.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx35.GetHash(), 8, 703, 8, 744000, 4, 454, 3372000},
        {tx36.GetHash(), 9, 763, 9, 944000, 2, 161, 3010000},
        {tx37.GetHash(), 9, 763, 9, 825000, 2, 161, 2891000},
        {tx38.GetHash(), 11, 924, 11, 3835000, 1, 101, 2810000},
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result8.size(); i++)
    {
        if (i <= 2 || i == 10 || i == 12 || i == 13)
        {
            VerifyTxNotInMempool(txns_result8[i], pool);
            continue;
        }
        CheckAncestors(txns_result8[i], pool);
    }

    vtx.push_back(MakeTransactionRef(tx24));
    vtx.push_back(MakeTransactionRef(tx25));
    vtx.push_back(MakeTransactionRef(tx27));
    vtx.push_back(MakeTransactionRef(tx28));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 18);

    /* clang-format off */
    std::vector<MempoolData> txns_result9 =
    {
        // Chain2:
        {tx21.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx22.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx23.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx24.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx25.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx26.GetHash(), 1, 101, 1, 20000, 5, 555, 3392000},
        {tx27.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx28.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx29.GetHash(), 1, 110, 1, 201000, 6, 624, 3674000},
        {tx30.GetHash(), 2, 170, 2, 302000, 1, 60, 101000},
        {tx31.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx32.GetHash(), 1, 60, 1, 20000, 5, 514, 3392000},
        {tx33.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx34.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx35.GetHash(), 4, 504, 4, 522000, 4, 454, 3372000},
        {tx36.GetHash(), 5, 564, 5, 722000, 2, 161, 3010000},
        {tx37.GetHash(), 5, 564, 5, 603000, 2, 161, 2891000},
        {tx38.GetHash(), 7, 725, 7, 3613000, 1, 101, 2810000},
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result9.size(); i++)
    {
        if (i <= 4 || i == 6 || i == 7 || i == 10 || i == 12 || i == 13)
        {
            VerifyTxNotInMempool(txns_result9[i], pool);
            continue;
        }
        CheckAncestors(txns_result9[i], pool);
    }

    vtx.push_back(MakeTransactionRef(tx26));
    vtx.push_back(MakeTransactionRef(tx29));
    vtx.push_back(MakeTransactionRef(tx32));
    vtx.push_back(MakeTransactionRef(tx35));
    pool.removeForBlock(vtx, 1, dummy, false);
    BOOST_CHECK_EQUAL(pool.size(), 14);

    /* clang-format off */
    std::vector<MempoolData> txns_result10 =
    {
        // Chain2:
        {tx21.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx22.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx23.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx24.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx25.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx26.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx27.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx28.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx29.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx30.GetHash(), 1, 60, 1, 101000, 1, 60, 101000},
        {tx31.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx32.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx33.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx34.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx35.GetHash(), 0, 0, 0, 0, 0, 0, 0},
        {tx36.GetHash(), 1, 60, 1, 200000, 2, 161, 3010000},
        {tx37.GetHash(), 1, 60, 1, 81000, 2, 161, 2891000},
        {tx38.GetHash(), 3, 221, 3, 3091000, 1, 101, 2810000},
    };
    /* clang-format on */

    for (size_t i = 0; i < txns_result10.size(); i++)
    {
        if (i <= 8 || (i >= 10 && i <= 14))
        {
            VerifyTxNotInMempool(txns_result10[i], pool);
            continue;
        }

        /*
        printf(
            "tx%ld countwanc %ld sizewanc %ld sigopswanc %u feeswanc %ld countwdesc %ld sizewdesc %ld feeswdesc %ld\n",
            i + 1, iter->GetCountWithAncestors(), iter->GetSizeWithAncestors(), iter->GetSigOpCountWithAncestors(),
            iter->GetModFeesWithAncestors(), iter->GetCountWithDescendants(), iter->GetSizeWithDescendants(),
            iter->GetModFeesWithDescendants());
        printf("tx%d: hash %s\n", (int)(i + 1), iter->GetTx().GetHash().ToString().c_str());
        */

        CheckAncestors(txns_result10[i], pool);
    }
}


BOOST_AUTO_TEST_CASE(MempoolRemoveTest)
{
    // Test CTxMemPool::remove functionality

    TestMemPoolEntryHelper entry;
    // Parent transaction with three children,
    // and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++)
    {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = 33000LL;
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++)
    {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout.hash = txParent.GetHash();
        txChild[i].vin[0].prevout.n = i;
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = 11000LL;
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++)
    {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout.hash = txChild[i].GetHash();
        txGrandChild[i].vin[0].prevout.n = 0;
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = 11000LL;
    }


    CTxMemPool testPool(CFeeRate(0));
    std::list<CTransactionRef> removed;

    // Nothing in pool, remove should do nothing:
    testPool.removeRecursive(txParent, removed);
    BOOST_CHECK_EQUAL(removed.size(), 0);

    // Just the parent:
    testPool.addUnchecked(txParent.GetHash(), entry.FromTx(txParent));
    testPool.removeRecursive(txParent, removed);
    BOOST_CHECK_EQUAL(removed.size(), 1);
    removed.clear();

    // Parent, children, grandchildren:
    testPool.addUnchecked(txParent.GetHash(), entry.FromTx(txParent));
    for (int i = 0; i < 3; i++)
    {
        testPool.addUnchecked(txChild[i].GetHash(), entry.FromTx(txChild[i]));
        testPool.addUnchecked(txGrandChild[i].GetHash(), entry.FromTx(txGrandChild[i]));
    }
    // Remove Child[0], GrandChild[0] should be removed:
    testPool.removeRecursive(txChild[0], removed);
    BOOST_CHECK_EQUAL(removed.size(), 2);
    removed.clear();
    // ... make sure grandchild and child are gone:
    testPool.removeRecursive(txGrandChild[0], removed);
    BOOST_CHECK_EQUAL(removed.size(), 0);
    testPool.removeRecursive(txChild[0], removed);
    BOOST_CHECK_EQUAL(removed.size(), 0);
    // Remove parent, all children/grandchildren should go:
    testPool.removeRecursive(txParent, removed);
    BOOST_CHECK_EQUAL(removed.size(), 5);
    BOOST_CHECK_EQUAL(testPool.size(), 0);
    removed.clear();

    // Add children and grandchildren, but NOT the parent (simulate the parent being in a block)
    for (int i = 0; i < 3; i++)
    {
        testPool.addUnchecked(txChild[i].GetHash(), entry.FromTx(txChild[i]));
        testPool.addUnchecked(txGrandChild[i].GetHash(), entry.FromTx(txGrandChild[i]));
    }
    // Now remove the parent, as might happen if a block-re-org occurs but the parent cannot be
    // put into the mempool (maybe because it is non-standard):
    testPool.removeRecursive(txParent, removed);
    BOOST_CHECK_EQUAL(removed.size(), 6);
    BOOST_CHECK_EQUAL(testPool.size(), 0);
    removed.clear();
}

template <typename name>
void CheckSort(CTxMemPool &pool, std::vector<std::string> &sortedOrder)
{
    BOOST_CHECK_EQUAL(pool.size(), sortedOrder.size());
    typename CTxMemPool::indexed_transaction_set::index<name>::type::iterator it = pool.mapTx.get<name>().begin();
    int count = 0;
    for (; it != pool.mapTx.get<name>().end(); ++it, ++count)
    {
        BOOST_CHECK_EQUAL(it->GetTx().GetHash().ToString(), sortedOrder[count]);
    }
}

template <typename name>
void _CheckSort(CTxMemPool &pool, std::vector<std::string> &sortedOrder)
{
    BOOST_CHECK_EQUAL(pool._size(), sortedOrder.size());
    typename CTxMemPool::indexed_transaction_set::index<name>::type::iterator it = pool.mapTx.get<name>().begin();
    int count = 0;
    for (; it != pool.mapTx.get<name>().end(); ++it, ++count)
    {
        BOOST_CHECK_EQUAL(it->GetTx().GetHash().ToString(), sortedOrder[count]);
    }
}

BOOST_AUTO_TEST_CASE(MempoolIndexingTest)
{
    CTxMemPool pool(CFeeRate(0));
    TestMemPoolEntryHelper entry;
    entry.hadNoDependencies = true;

    /* 3rd highest fee */
    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx1.GetHash(), entry.Fee(10000LL).Priority(10.0).FromTx(tx1));

    /* highest fee */
    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * COIN;
    pool.addUnchecked(tx2.GetHash(), entry.Fee(20000LL).Priority(9.0).FromTx(tx2));

    /* lowest fee */
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 5 * COIN;
    pool.addUnchecked(tx3.GetHash(), entry.Fee(0LL).Priority(100.0).FromTx(tx3));

    /* 2nd highest fee */
    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 6 * COIN;
    pool.addUnchecked(tx4.GetHash(), entry.Fee(15000LL).Priority(1.0).FromTx(tx4));

    /* equal fee rate to tx1, but newer */
    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 11 * COIN;
    entry.nTime = 1;
    entry.dPriority = 10.0;
    pool.addUnchecked(tx5.GetHash(), entry.Fee(10000LL).FromTx(tx5));
    BOOST_CHECK_EQUAL(pool.size(), 5);

    std::vector<std::string> sortedOrder;
    sortedOrder.resize(5);
    sortedOrder[0] = tx3.GetHash().ToString(); // 0
    sortedOrder[1] = tx5.GetHash().ToString(); // 10000
    sortedOrder[2] = tx1.GetHash().ToString(); // 10000
    sortedOrder[3] = tx4.GetHash().ToString(); // 15000
    sortedOrder[4] = tx2.GetHash().ToString(); // 20000
    CheckSort<descendant_score>(pool, sortedOrder);

    /* low fee but with high fee child */
    /* tx6 -> tx7 -> tx8, tx9 -> tx10 */
    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vout.resize(1);
    tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx6.vout[0].nValue = 20 * COIN;
    pool.addUnchecked(tx6.GetHash(), entry.Fee(0LL).FromTx(tx6));
    BOOST_CHECK_EQUAL(pool.size(), 6);
    // Check that at this point, tx6 is sorted low
    sortedOrder.insert(sortedOrder.begin(), tx6.GetHash().ToString());
    CheckSort<descendant_score>(pool, sortedOrder);

    CTxMemPool::setEntries setAncestors;
    setAncestors.insert(pool.mapTx.find(tx6.GetHash()));
    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(1);
    tx7.vin[0].prevout = COutPoint(tx6.GetHash(), 0);
    tx7.vin[0].scriptSig = CScript() << OP_11;
    tx7.vout.resize(2);
    tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * COIN;
    tx7.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[1].nValue = 1 * COIN;

    std::string dummy;
    CMutableTransaction tx10 = CMutableTransaction();
    CMutableTransaction tx9 = CMutableTransaction();
    CMutableTransaction tx8 = CMutableTransaction();
    std::vector<std::string> snapshotOrder;
    {
        CTxMemPool::setEntries setAncestorsCalculated;
        WRITELOCK(pool.cs_txmempool);
        BOOST_CHECK_EQUAL(pool._CalculateMemPoolAncestors(entry.Fee(2000000LL).FromTx(tx7), setAncestorsCalculated, 100,
                              1000000, 1000, 1000000, dummy),
            true);
        BOOST_CHECK(setAncestorsCalculated == setAncestors);

        pool.addUnchecked(tx7.GetHash(), entry.FromTx(tx7), setAncestors);

        BOOST_CHECK_EQUAL(pool._size(), 7);

        // Now tx6 should be sorted higher (high fee child): tx7, tx6, tx2, ...
        sortedOrder.erase(sortedOrder.begin());
        sortedOrder.push_back(tx6.GetHash().ToString());
        sortedOrder.push_back(tx7.GetHash().ToString());
        _CheckSort<descendant_score>(pool, sortedOrder);

        /* low fee child of tx7 */
        tx8.vin.resize(1);
        tx8.vin[0].prevout = COutPoint(tx7.GetHash(), 0);
        tx8.vin[0].scriptSig = CScript() << OP_11;
        tx8.vout.resize(1);
        tx8.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx8.vout[0].nValue = 10 * COIN;
        setAncestors.insert(pool.mapTx.find(tx7.GetHash()));
        pool.addUnchecked(tx8.GetHash(), entry.Fee(0LL).Time(2).FromTx(tx8), setAncestors);

        // Now tx8 should be sorted low, but tx6/tx both high
        sortedOrder.insert(sortedOrder.begin(), tx8.GetHash().ToString());
        _CheckSort<descendant_score>(pool, sortedOrder);

        /* low fee child of tx7 */
        tx9.vin.resize(1);
        tx9.vin[0].prevout = COutPoint(tx7.GetHash(), 1);
        tx9.vin[0].scriptSig = CScript() << OP_11;
        tx9.vout.resize(1);
        tx9.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx9.vout[0].nValue = 1 * COIN;
        pool.addUnchecked(tx9.GetHash(), entry.Fee(0LL).Time(3).FromTx(tx9), setAncestors);

        // tx9 should be sorted low
        BOOST_CHECK_EQUAL(pool._size(), 9);
        sortedOrder.insert(sortedOrder.begin(), tx9.GetHash().ToString());
        _CheckSort<descendant_score>(pool, sortedOrder);

        snapshotOrder = sortedOrder;

        setAncestors.insert(pool.mapTx.find(tx8.GetHash()));
        setAncestors.insert(pool.mapTx.find(tx9.GetHash()));
        /* tx10 depends on tx8 and tx9 and has a high fee*/
        tx10.vin.resize(2);
        tx10.vin[0].prevout = COutPoint(tx8.GetHash(), 0);
        tx10.vin[0].scriptSig = CScript() << OP_11;
        tx10.vin[1].prevout = COutPoint(tx9.GetHash(), 0);
        tx10.vin[1].scriptSig = CScript() << OP_11;
        tx10.vout.resize(1);
        tx10.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx10.vout[0].nValue = 10 * COIN;

        setAncestorsCalculated.clear();
        BOOST_CHECK_EQUAL(pool._CalculateMemPoolAncestors(entry.Fee(200000LL).Time(4).FromTx(tx10),
                              setAncestorsCalculated, 100, 1000000, 1000, 1000000, dummy),
            true);
        BOOST_CHECK(setAncestorsCalculated == setAncestors);

        pool.addUnchecked(tx10.GetHash(), entry.FromTx(tx10), setAncestors);
    }

    /**
     *  tx8 and tx9 should both now be sorted higher
     *  Final order after tx10 is added:
     *
     *  tx3 = 0 (1)
     *  tx5 = 10000 (1)
     *  tx1 = 10000 (1)
     *  tx4 = 15000 (1)
     *  tx2 = 20000 (1)
     *  tx9 = 200k (2 txs)
     *  tx8 = 200k (2 txs)
     *  tx10 = 200k (1 tx)
     *  tx6 = 2.2M (5 txs)
     *  tx7 = 2.2M (4 txs)
     */
    sortedOrder.erase(sortedOrder.begin(), sortedOrder.begin() + 2); // take out tx9, tx8 from the beginning
    sortedOrder.insert(sortedOrder.begin() + 5, tx9.GetHash().ToString());
    sortedOrder.insert(sortedOrder.begin() + 6, tx8.GetHash().ToString());
    sortedOrder.insert(sortedOrder.begin() + 7, tx10.GetHash().ToString()); // tx10 is just before tx6
    CheckSort<descendant_score>(pool, sortedOrder);

    // there should be 10 transactions in the mempool
    BOOST_CHECK_EQUAL(pool.size(), 10);

    // Now try removing tx10 and verify the sort order returns to normal
    std::list<CTransactionRef> removed;
    pool.removeRecursive(pool.mapTx.find(tx10.GetHash())->GetTx(), removed);
    CheckSort<descendant_score>(pool, snapshotOrder);

    pool.removeRecursive(pool.mapTx.find(tx9.GetHash())->GetTx(), removed);
    pool.removeRecursive(pool.mapTx.find(tx8.GetHash())->GetTx(), removed);
    /* Now check the sort on the mining score index.
     * Final order should be:
     *
     * tx7 (2M)
     * tx2 (20k)
     * tx4 (15000)
     * tx1/tx5 (10000)
     * tx3/6 (0)
     * (Ties resolved by hash)
     */
    sortedOrder.clear();
    sortedOrder.push_back(tx7.GetHash().ToString());
    sortedOrder.push_back(tx2.GetHash().ToString());
    sortedOrder.push_back(tx4.GetHash().ToString());
    if (tx1.GetHash() < tx5.GetHash())
    {
        sortedOrder.push_back(tx5.GetHash().ToString());
        sortedOrder.push_back(tx1.GetHash().ToString());
    }
    else
    {
        sortedOrder.push_back(tx1.GetHash().ToString());
        sortedOrder.push_back(tx5.GetHash().ToString());
    }
    if (tx3.GetHash() < tx6.GetHash())
    {
        sortedOrder.push_back(tx6.GetHash().ToString());
        sortedOrder.push_back(tx3.GetHash().ToString());
    }
    else
    {
        sortedOrder.push_back(tx3.GetHash().ToString());
        sortedOrder.push_back(tx6.GetHash().ToString());
    }
    CheckSort<mining_score>(pool, sortedOrder);
}

BOOST_AUTO_TEST_CASE(MempoolAncestorIndexingTest)
{
    CTxMemPool pool(CFeeRate(0));
    TestMemPoolEntryHelper entry;
    entry.hadNoDependencies = true;

    /* 3rd highest fee */
    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx1.GetHash(), entry.Fee(10000LL).Time(GetTime() + 1).Priority(10.0).FromTx(tx1));

    /* highest fee */
    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * COIN;
    pool.addUnchecked(tx2.GetHash(), entry.Fee(20000LL).Time(GetTime() + 2).Priority(9.0).FromTx(tx2));
    uint64_t tx2Size = ::GetSerializeSize(tx2, SER_NETWORK, PROTOCOL_VERSION);

    /* lowest fee */
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 5 * COIN;
    pool.addUnchecked(tx3.GetHash(), entry.Fee(0LL).Time(GetTime() + 3).Priority(100.0).FromTx(tx3));

    /* 2nd highest fee */
    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 6 * COIN;
    pool.addUnchecked(tx4.GetHash(), entry.Fee(15000LL).Time(GetTime() + 4).Priority(1.0).FromTx(tx4));

    /* equal fee rate to tx1, but newer */
    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 11 * COIN;
    pool.addUnchecked(tx5.GetHash(), entry.Fee(10000LL).Time(GetTime() + 5).FromTx(tx5));
    BOOST_CHECK_EQUAL(pool.size(), 5);

    std::vector<std::string> sortedOrder;
    sortedOrder.resize(5);
    sortedOrder[0] = tx2.GetHash().ToString(); // 20000
    sortedOrder[1] = tx4.GetHash().ToString(); // 15000
    // tx1 and tx5 are both 10000
    // Ties are broken by timestamp, so determine which
    // time comes first.
    sortedOrder[2] = tx1.GetHash().ToString();
    sortedOrder[3] = tx5.GetHash().ToString();
    sortedOrder[4] = tx3.GetHash().ToString(); // 0

    CheckSort<ancestor_score>(pool, sortedOrder);

    /* low fee parent with high fee child */
    /* tx6 (0) -> tx7 (high) */
    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vout.resize(1);
    tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx6.vout[0].nValue = 20 * COIN;
    uint64_t tx6Size = ::GetSerializeSize(tx6, SER_NETWORK, PROTOCOL_VERSION);

    pool.addUnchecked(tx6.GetHash(), entry.Fee(0LL).Time(GetTime() + 6).FromTx(tx6));
    BOOST_CHECK_EQUAL(pool.size(), 6);
    // Ties are broken by time
    sortedOrder.push_back(tx6.GetHash().ToString());
    CheckSort<ancestor_score>(pool, sortedOrder);

    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(1);
    tx7.vin[0].prevout = COutPoint(tx6.GetHash(), 0);
    tx7.vin[0].scriptSig = CScript() << OP_11;
    tx7.vout.resize(1);
    tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * COIN;
    uint64_t tx7Size = ::GetSerializeSize(tx7, SER_NETWORK, PROTOCOL_VERSION);

    /* set the fee to just below tx2's feerate when including ancestor */
    CAmount fee = (20000 / tx2Size) * (tx7Size + tx6Size) - 1;

    // CTxMemPoolEntry entry7(tx7, fee, 2, 10.0, 1, true);
    pool.addUnchecked(tx7.GetHash(), entry.Fee(fee).Time(GetTime() + 7).FromTx(tx7));
    BOOST_CHECK_EQUAL(pool.size(), 7);
    sortedOrder.insert(sortedOrder.begin() + 1, tx7.GetHash().ToString());
    CheckSort<ancestor_score>(pool, sortedOrder);

    /* after tx6 is mined, tx7 should move up in the sort */
    std::vector<CTransactionRef> vtx;
    vtx.push_back(MakeTransactionRef(tx6));
    std::list<CTransactionRef> dummy;
    pool.removeForBlock(vtx, 1, dummy, false);

    sortedOrder.erase(sortedOrder.begin() + 1);
    // Ties are broken by time
    sortedOrder.pop_back();
    sortedOrder.insert(sortedOrder.begin(), tx7.GetHash().ToString());
    CheckSort<ancestor_score>(pool, sortedOrder);
}


BOOST_AUTO_TEST_CASE(MempoolSizeLimitTest)
{
    CTxMemPool pool(CFeeRate(1000));
    TestMemPoolEntryHelper entry;
    entry.dPriority = 10.0;

    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vin.resize(1);
    tx1.vin[0].scriptSig = CScript() << OP_1;
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_1 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx1.GetHash(), entry.Fee(10000LL).FromTx(tx1, &pool));

    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vin.resize(1);
    tx2.vin[0].scriptSig = CScript() << OP_2;
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_2 << OP_EQUAL;
    tx2.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx2.GetHash(), entry.Fee(5000LL).FromTx(tx2, &pool));

    pool.TrimToSize(pool.DynamicMemoryUsage()); // should do nothing
    BOOST_CHECK(pool.exists(tx1.GetHash()));
    BOOST_CHECK(pool.exists(tx2.GetHash()));

    pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4); // should remove the lower-feerate transaction
    BOOST_CHECK(pool.exists(tx1.GetHash()));
    BOOST_CHECK(!pool.exists(tx2.GetHash()));

    pool.addUnchecked(tx2.GetHash(), entry.FromTx(tx2, &pool));
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vin.resize(1);
    tx3.vin[0].prevout = COutPoint(tx2.GetHash(), 0);
    tx3.vin[0].scriptSig = CScript() << OP_2;
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_3 << OP_EQUAL;
    tx3.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx3.GetHash(), entry.Fee(20000LL).FromTx(tx3, &pool));

    pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4); // tx3 should pay for tx2 (CPFP)
    BOOST_CHECK(!pool.exists(tx1.GetHash()));
    BOOST_CHECK(pool.exists(tx2.GetHash()));
    BOOST_CHECK(pool.exists(tx3.GetHash()));

    // mempool is limited to tx1's size in memory usage, so nothing fits
    pool.TrimToSize(::GetSerializeSize(CTransaction(tx1), SER_NETWORK, PROTOCOL_VERSION));
    BOOST_CHECK(!pool.exists(tx1.GetHash()));
    BOOST_CHECK(!pool.exists(tx2.GetHash()));
    BOOST_CHECK(!pool.exists(tx3.GetHash()));

    CFeeRate maxFeeRateRemoved(25000, ::GetSerializeSize(CTransaction(tx3), SER_NETWORK, PROTOCOL_VERSION) +
                                          ::GetSerializeSize(CTransaction(tx2), SER_NETWORK, PROTOCOL_VERSION));
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), maxFeeRateRemoved.GetFeePerK() + 1000);

    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vin.resize(2);
    tx4.vin[0].prevout.SetNull();
    tx4.vin[0].scriptSig = CScript() << OP_4;
    tx4.vin[1].prevout.SetNull();
    tx4.vin[1].scriptSig = CScript() << OP_4;
    tx4.vout.resize(2);
    tx4.vout[0].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
    tx4.vout[0].nValue = 10 * COIN;
    tx4.vout[1].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
    tx4.vout[1].nValue = 10 * COIN;

    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vin.resize(2);
    tx5.vin[0].prevout = COutPoint(tx4.GetHash(), 0);
    tx5.vin[0].scriptSig = CScript() << OP_4;
    tx5.vin[1].prevout.SetNull();
    tx5.vin[1].scriptSig = CScript() << OP_5;
    tx5.vout.resize(2);
    tx5.vout[0].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
    tx5.vout[0].nValue = 10 * COIN;
    tx5.vout[1].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
    tx5.vout[1].nValue = 10 * COIN;

    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vin.resize(2);
    tx6.vin[0].prevout = COutPoint(tx4.GetHash(), 1);
    tx6.vin[0].scriptSig = CScript() << OP_4;
    tx6.vin[1].prevout.SetNull();
    tx6.vin[1].scriptSig = CScript() << OP_6;
    tx6.vout.resize(2);
    tx6.vout[0].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
    tx6.vout[0].nValue = 10 * COIN;
    tx6.vout[1].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
    tx6.vout[1].nValue = 10 * COIN;

    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(2);
    tx7.vin[0].prevout = COutPoint(tx5.GetHash(), 0);
    tx7.vin[0].scriptSig = CScript() << OP_5;
    tx7.vin[1].prevout = COutPoint(tx6.GetHash(), 0);
    tx7.vin[1].scriptSig = CScript() << OP_6;
    tx7.vout.resize(2);
    tx7.vout[0].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * COIN;
    tx7.vout[1].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
    tx7.vout[1].nValue = 10 * COIN;

    pool.addUnchecked(tx4.GetHash(), entry.Fee(7000LL).FromTx(tx4, &pool));
    pool.addUnchecked(tx5.GetHash(), entry.Fee(1000LL).FromTx(tx5, &pool));
    pool.addUnchecked(tx6.GetHash(), entry.Fee(1100LL).FromTx(tx6, &pool));
    pool.addUnchecked(tx7.GetHash(), entry.Fee(9000LL).FromTx(tx7, &pool));

    // we only require this remove, at max, 2 txn, because its not clear what we're really optimizing for aside from
    // that
    pool.TrimToSize(pool.DynamicMemoryUsage() - 1);
    BOOST_CHECK(pool.exists(tx4.GetHash()));
    BOOST_CHECK(pool.exists(tx6.GetHash()));
    BOOST_CHECK(!pool.exists(tx7.GetHash()));

    if (!pool.exists(tx5.GetHash()))
        pool.addUnchecked(tx5.GetHash(), entry.Fee(1000LL).FromTx(tx5, &pool));
    pool.addUnchecked(tx7.GetHash(), entry.Fee(9000LL).FromTx(tx7, &pool));

    pool.TrimToSize(pool.DynamicMemoryUsage() / 2); // should maximize mempool size by only removing 5/7
    BOOST_CHECK(pool.exists(tx4.GetHash()));
    BOOST_CHECK(!pool.exists(tx5.GetHash()));
    BOOST_CHECK(pool.exists(tx6.GetHash()));
    BOOST_CHECK(!pool.exists(tx7.GetHash()));

    pool.addUnchecked(tx5.GetHash(), entry.Fee(1000LL).FromTx(tx5, &pool));
    pool.addUnchecked(tx7.GetHash(), entry.Fee(9000LL).FromTx(tx7, &pool));

    std::vector<CTransactionRef> vtx;
    std::list<CTransactionRef> conflicts;
    SetMockTime(42);
    SetMockTime(42 + CTxMemPool::ROLLING_FEE_HALFLIFE);
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), maxFeeRateRemoved.GetFeePerK() + 1000);
    // ... we should keep the same min fee until we get a block
    pool.removeForBlock(vtx, 1, conflicts);
    SetMockTime(42 + 2 * CTxMemPool::ROLLING_FEE_HALFLIFE);
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), (maxFeeRateRemoved.GetFeePerK() + 1000) / 2);
    // ... then feerate should drop 1/2 each halflife

    SetMockTime(42 + 2 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2);
    BOOST_CHECK_EQUAL(
        pool.GetMinFee(pool.DynamicMemoryUsage() * 5 / 2).GetFeePerK(), (maxFeeRateRemoved.GetFeePerK() + 1000) / 4);
    // ... with a 1/2 halflife when mempool is < 1/2 its target size

    SetMockTime(42 + 2 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2 +
                CTxMemPool::ROLLING_FEE_HALFLIFE / 4);
    BOOST_CHECK_EQUAL(
        pool.GetMinFee(pool.DynamicMemoryUsage() * 9 / 2).GetFeePerK(), (maxFeeRateRemoved.GetFeePerK() + 1000) / 8);
    // ... with a 1/4 halflife when mempool is < 1/4 its target size

    SetMockTime(42 + 7 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2 +
                CTxMemPool::ROLLING_FEE_HALFLIFE / 4);
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), 1000);
    // ... but feerate should never drop below 1000

    SetMockTime(42 + 8 * CTxMemPool::ROLLING_FEE_HALFLIFE + CTxMemPool::ROLLING_FEE_HALFLIFE / 2 +
                CTxMemPool::ROLLING_FEE_HALFLIFE / 4);
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(), 0);
    // ... unless it has gone all the way to 0 (after getting past 1000/2)

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
