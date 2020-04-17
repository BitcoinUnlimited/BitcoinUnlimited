// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "policy/fees.h"
#include "txmempool.h"
#include "uint256.h"
#include "util.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(policyestimator_tests, TestingSetup)

// used in all tests in this file, declare once here so editing is easier
static const int32_t txHashesSize = 45;

/*
 * BlockPolicyEstimates_no_fee_inc test is supposed to
 * simulate what happens when more txs are added to the mempool
 * than a block can clear, but all fees added are the min tx fee
 *
 *
 * The end result should be the fee estimator continues to recommend the
 * min tx fee regardless of mempool backup.
 */
BOOST_AUTO_TEST_CASE(BlockPolicyEstimates_no_fee_inc)
{
    CTxMemPool mpool(CFeeRate(1000));
    TestMemPoolEntryHelper entry;
    double basepri = 10;

    // Store the hashes of transactions that have been
    // added to the mempool by their associate fee
    // index is fee - basefee, for example [0] is basefee
    // and [1] is basefee + 1
    // in short, the index is the feebumper
    std::vector<uint256> txHashes[txHashesSize];

    // Create a transaction template
    CScript garbage;
    for (unsigned int i = 0; i < 128; i++)
        garbage.push_back('X');
    CMutableTransaction tx;
    std::list<CTransactionRef> dummyConflicted;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = garbage;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0LL;

    // Create a fake block
    std::vector<CTransactionRef> vtx;
    int blocknum = 0;
    int32_t curfee = 0;
    // Loop through 200 blocks with no change in submitted fee
    while (blocknum < 200)
    {
        for (int j = 0; j < 50; j++)
        {
            // add 50 tx per block, each block will mine 40 of them to slowly add a backlog
            tx.vin[0].prevout.n = 10000 * blocknum + 100 * j; // make transaction unique
            uint256 hash = tx.GetHash();
            mpool.addUnchecked(hash, entry.Fee(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION))
                                         .Time(GetTime())
                                         .Priority(basepri) // a junk priority
                                         .Height(blocknum)
                                         .FromTx(tx, &mpool));
            assert((txHashesSize - 1) >= curfee);
            txHashes[curfee].push_back(hash);
        }

        // include 40 transactions into a block
        while (vtx.size() < 40)
        {
            if (curfee >= 0) // cant access negative index in array
            {
                assert((txHashesSize - 1) >= curfee);
                size_t txAvail = txHashes[curfee].size();
                if (txAvail == 0)
                {
                    curfee--;
                    continue;
                }
                assert((txHashesSize - 1) >= curfee);
                uint256 txhash = txHashes[curfee].back();
                CTransactionRef ptx = mpool.get(txhash);
                if (ptx)
                {
                    vtx.push_back(ptx);
                }
                assert((txHashesSize - 1) >= curfee);
                txHashes[curfee].pop_back();
            }
        }
        mpool.removeForBlock(vtx, ++blocknum, dummyConflicted);
        vtx.clear();
        if (blocknum % 5 == 0)
        {
            // regardless of backlog, if everyone is only paying minTxFee, we should only pay mintxfee.
            // the assumption here is that the miners should choose to mine larger blocks to get paid more.
            BOOST_CHECK(mpool.estimateFee(1) <= CFeeRate(1000));
            BOOST_CHECK(mpool.estimateFee(2) <= CFeeRate(1000));
            BOOST_CHECK(mpool.estimateFee(3) <= CFeeRate(1000));
            BOOST_CHECK(mpool.estimateFee(4) <= CFeeRate(1000));
            BOOST_CHECK(mpool.estimateFee(5) <= CFeeRate(1000));
        }
    }
}

/*
 * BlockPolicyEstimates_gradual_fee_inc test is supposed to
 * simulate what happens when more txs are added to the mempool
 * than a block can clear in a competative market.
 * We want our tx in the next block
 * so we should get a recommendation on the lowest possible fee that
 * will get included into the next block
 *
 * The end result should be the fee estimator recommends the
 * next lowest possible fee (+1 sat)
 */

BOOST_AUTO_TEST_CASE(BlockPolicyEstimates_gradual_fee_inc)
{
    CTxMemPool mpool(CFeeRate(1000));
    TestMemPoolEntryHelper entry;
    double basepri = 10;

    // Store the hashes of transactions that have been
    // added to the mempool by their associate fee
    // index is fee - basefee, for example [0] is basefee
    // and [1] is basefee + 1
    // in short, the index is the feebumper
    std::vector<uint256> txHashes[txHashesSize];

    // Create a transaction template
    CScript garbage;
    for (unsigned int i = 0; i < 128; i++)
        garbage.push_back('X');
    CMutableTransaction tx;
    std::list<CTransactionRef> dummyConflicted;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = garbage;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0LL;
    // Create a fake block
    std::vector<CTransactionRef> vtx;
    int blocknum = 0;
    int32_t curfee = 0;
    // Loop through some blocks to test increasing fee
    int feebumper = 0;
    while (blocknum < 200)
    {
        if (blocknum > 0 && (blocknum % 5) == 0)
        {
            feebumper = feebumper + 1;
            curfee = curfee + 1;
        }
        // start with the current feebumper since it should be the highest fee
        for (int j = 0; j < 100; j++)
        {
            // add 100 tx per block, each block will mine 40 of them to add a backlog
            tx.vin[0].prevout.n = 10000 * blocknum + 100 * j; // make transaction unique
            uint256 hash = tx.GetHash();
            mpool.addUnchecked(hash, entry.Fee(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) + feebumper)
                                         .Time(GetTime())
                                         .Priority(basepri) // a junk priority
                                         .Height(blocknum)
                                         .FromTx(tx, &mpool));
            assert((txHashesSize - 1) >= curfee);
            txHashes[curfee].push_back(hash);
        }
        // include 40 transactions into a block
        while (vtx.size() < 40)
        {
            if (curfee >= 0) // cant access negative index in array
            {
                assert((txHashesSize - 1) >= curfee);
                size_t txAvail = txHashes[curfee].size();
                if (txAvail == 0)
                {
                    curfee--;
                    continue;
                }
                assert((txHashesSize - 1) >= curfee);
                uint256 txhash = txHashes[curfee].back();
                CTransactionRef ptx = mpool.get(txhash);
                if (ptx)
                {
                    vtx.push_back(ptx);
                }
                assert((txHashesSize - 1) >= curfee);
                txHashes[curfee].pop_back();
            }
            else
            {
                break;
            }
        }
        mpool.removeForBlock(vtx, ++blocknum, dummyConflicted);
        vtx.clear();
        if (blocknum % 5 == 0)
        {
            // we use *6 because our tx size is 188 bytes, if we add 1 sat to our fee it will make our fee rate go up by
            // 1000/188
            BOOST_CHECK(mpool.estimateFee(1) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(2) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(3) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(4) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(5) <= CFeeRate(1000 + (feebumper * 6)));
        }
    }
}

/*
 * BlockPolicyEstimates_short_partial_fee_inc test is supposed to
 * simulate what happens when more txs are added to the mempool
 * than a block can clear in a competative market.
 * The difference between this test and the previous is half way through
 * a "user" puts a group of txs into the mempool with a very high fee but
 * not enough to fill an entire block. (partial spike)
 *
 * The results should be the same as the previous test due to there not being
 * enough txs to fill a whole block
 */

BOOST_AUTO_TEST_CASE(BlockPolicyEstimates_short_partial_fee_inc)
{
    CTxMemPool mpool(CFeeRate(1000));
    TestMemPoolEntryHelper entry;
    double basepri = 10;

    // Store the hashes of transactions that have been
    // added to the mempool by their associate fee
    // index is fee - basefee, for example [0] is basefee
    // and [1] is basefee + 1
    // in short, the index is the feebumper
    std::vector<uint256> txHashes[txHashesSize];

    // Create a transaction template
    CScript garbage;
    for (unsigned int i = 0; i < 128; i++)
        garbage.push_back('X');
    CMutableTransaction tx;
    std::list<CTransactionRef> dummyConflicted;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = garbage;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0LL;
    // Create a fake block
    std::vector<CTransactionRef> vtx;
    int blocknum = 0;
    int32_t curfee = 0;
    // Loop through some blocks to test a sudden rise in fees at block 100
    int feebumper = 0;
    std::vector<uint256> highfeeholder;
    bool highFeeActive = false;
    int numtxgen = 100;
    while (blocknum < 200)
    {
        if (blocknum > 0 && (blocknum % 5) == 0)
        {
            feebumper = feebumper + 1;
            curfee = curfee + 1;
            // at block 100, massively increase fees to simulate some sort of attack
        }
        if (blocknum == 100)
        {
            feebumper = feebumper + 900;
            highFeeActive = true;
            numtxgen = 30;
        }
        // reset fee bumper to its regular level
        if (blocknum == 105)
        {
            feebumper = feebumper - 900;
            highFeeActive = false;
            numtxgen = 100;
        }
        // start with the current feebumper since it should be the highest fee
        for (int j = 0; j < numtxgen; j++)
        {
            // add 100 tx per block, each block will mine 40 of them to add a backlog
            tx.vin[0].prevout.n = 10000 * blocknum + 100 * j; // make transaction unique
            uint256 hash = tx.GetHash();
            mpool.addUnchecked(hash, entry.Fee(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) + feebumper)
                                         .Time(GetTime())
                                         .Priority(basepri) // a junk priority
                                         .Height(blocknum)
                                         .FromTx(tx, &mpool));
            if (highFeeActive)
            {
                highfeeholder.push_back(hash);
            }
            else
            {
                assert((txHashesSize - 1) >= curfee);
                txHashes[curfee].push_back(hash);
            }
        }
        // include 40 transactions into a block
        while (vtx.size() < 40)
        {
            if (!highfeeholder.empty())
            {
                uint256 txhash = highfeeholder.back();
                CTransactionRef ptx = mpool.get(txhash);
                if (ptx)
                {
                    vtx.push_back(ptx);
                }
                highfeeholder.pop_back();
            }
            else if (curfee >= 0) // cant access negative index in array
            {
                assert((txHashesSize - 1) >= curfee);
                size_t txAvail = txHashes[curfee].size();
                if (txAvail == 0)
                {
                    curfee--;
                    continue;
                }
                assert((txHashesSize - 1) >= curfee);
                uint256 txhash = txHashes[curfee].back();
                CTransactionRef ptx = mpool.get(txhash);
                if (ptx)
                {
                    vtx.push_back(ptx);
                }
                assert((txHashesSize - 1) >= curfee);
                txHashes[curfee].pop_back();
            }
            else
            {
                break;
            }
        }
        mpool.removeForBlock(vtx, ++blocknum, dummyConflicted);
        vtx.clear();
        if (blocknum % 5 == 0)
        {
            // we use *6 because our tx size is 188 bytes, if we add 1 sat to our fee it will make our fee rate go up by
            // 1000/188

            // even though we have high fees added in, because they are all cleared their bucket shouldnt affect the one
            // we are checking
            // for our fee estimate
            BOOST_CHECK(mpool.estimateFee(1) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(2) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(3) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(4) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(5) <= CFeeRate(1000 + (feebumper * 6)));
        }
    }
}

/*
 * BlockPolicyEstimates_short_full_fee_inc test is supposed to
 * simulate what happens when more txs are added to the mempool
 * than a block can clear in a competative market.
 * The difference between this test and the previous is half way through
 * a "user" puts a group of txs into the mempool with a very high fee,
 * enough to fill an entire block. (full spike)
 *
 * The results should be the same as the previous test except for a fee spike
 * that will taper off once the input of high fee txs is put into the mempool
 *
 */

BOOST_AUTO_TEST_CASE(BlockPolicyEstimates_short_full_fee_inc)
{
    CTxMemPool mpool(CFeeRate(1000));
    TestMemPoolEntryHelper entry;
    double basepri = 10;

    // Store the hashes of transactions that have been
    // added to the mempool by their associate fee
    // index is fee - basefee, for example [0] is basefee
    // and [1] is basefee + 1
    // in short, the index is the feebumper
    std::vector<uint256> txHashes[txHashesSize];

    // Create a transaction template
    CScript garbage;
    for (unsigned int i = 0; i < 128; i++)
        garbage.push_back('X');
    CMutableTransaction tx;
    std::list<CTransactionRef> dummyConflicted;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = garbage;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0LL;
    // Create a fake block
    std::vector<CTransactionRef> vtx;
    int blocknum = 0;
    int32_t curfee = 0;
    // Loop through some blocks to test a sudden rise in fees at block 1000
    // the difference between this test and the previous test is that this time we have
    // more high fees than we can clear in a single block
    int feebumper = 0;
    std::vector<uint256> highfeeholder;
    bool highFeeActive = false;
    while (blocknum < 200)
    {
        if (blocknum > 0 && (blocknum % 5) == 0)
        {
            feebumper = feebumper + 1;
            curfee = curfee + 1;
        }
        // at block 700, massively increase fees to simulate some sort of attack
        if (blocknum == 100)
        {
            feebumper = feebumper + 900;
            highFeeActive = true;
        }
        // reset fee bumper to its regular level
        if (blocknum == 105)
        {
            feebumper = feebumper - 900;
            highFeeActive = false;
        }
        // start with the current feebumper since it should be the highest fee
        for (int j = 0; j < 80; j++)
        {
            // add 80 tx per block, each block will mine 40 of them to add a backlog
            tx.vin[0].prevout.n = 10000 * blocknum + 100 * j; // make transaction unique
            uint256 hash = tx.GetHash();
            mpool.addUnchecked(hash, entry.Fee(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) + feebumper)
                                         .Time(GetTime())
                                         .Priority(basepri) // a junk priority
                                         .Height(blocknum)
                                         .FromTx(tx, &mpool));
            if (highFeeActive)
            {
                highfeeholder.push_back(hash);
            }
            else
            {
                assert((txHashesSize - 1) >= curfee);
                txHashes[curfee].push_back(hash);
            }
        }
        // include 40 transactions into a block
        while (vtx.size() < 40)
        {
            if (!highfeeholder.empty())
            {
                uint256 txhash = highfeeholder.back();
                CTransactionRef ptx = mpool.get(txhash);
                if (ptx)
                {
                    vtx.push_back(ptx);
                }
                highfeeholder.pop_back();
            }
            else if (curfee >= 0) // cant access negative index in array
            {
                assert((txHashesSize - 1) >= curfee);
                size_t txAvail = txHashes[curfee].size();
                if (txAvail == 0)
                {
                    curfee--;
                    continue;
                }
                assert((txHashesSize - 1) >= curfee);
                uint256 txhash = txHashes[curfee].back();
                CTransactionRef ptx = mpool.get(txhash);
                if (ptx)
                {
                    vtx.push_back(ptx);
                }
                assert((txHashesSize - 1) >= curfee);
                txHashes[curfee].pop_back();
            }
            else
            {
                break;
            }
        }
        mpool.removeForBlock(vtx, ++blocknum, dummyConflicted);
        vtx.clear();
        if (blocknum % 5 == 0)
        {
            // we use *6 because our tx size is 188 bytes, if we add 1 sat to our fee it will make our fee rate go up by
            // 1000/188

            // even though we have high fees added in, because they are all cleared their bucket shouldnt affect the one
            // we are checking
            // for our fee estimate
            BOOST_CHECK(mpool.estimateFee(1) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(2) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(3) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(4) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(5) <= CFeeRate(1000 + (feebumper * 6)));
        }
    }
}

/*
 * BlockPolicyEstimates_tx_bell_curve test is supposed to
 * simulate what happens when more txs are added to the mempool
 * than a block can clear in a competative market until halfway when the
 * amount of tx's generated is less than the amount required to fill a block.
 * (tx fee and density go up until block 100, then density goes down and space frees up)
 *
 * The results should be a fee increase until block space frees up so we get included in a block.
 * once space frees up the suggested fee should drop back down. graphing the suggested fee should
 * yield a bell curve
 */

BOOST_AUTO_TEST_CASE(BlockPolicyEstimates_tx_bell_curve)
{
    CTxMemPool mpool(CFeeRate(1000));
    TestMemPoolEntryHelper entry;
    double basepri = 10;

    // Store the hashes of transactions that have been
    // added to the mempool by their associate fee
    // index is fee - basefee, for example [0] is basefee
    // and [1] is basefee + 1
    // in short, the index is the feebumper
    std::vector<uint256> txHashes[txHashesSize];

    // Create a transaction template
    CScript garbage;
    for (unsigned int i = 0; i < 128; i++)
        garbage.push_back('X');
    CMutableTransaction tx;
    std::list<CTransactionRef> dummyConflicted;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = garbage;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0LL;
    // Create a fake block
    std::vector<CTransactionRef> vtx;
    int blocknum = 0;
    int32_t curfee = 0;
    int feebumper = 0;
    int numtxgen = 50;
    while (blocknum < 200)
    {
        if (blocknum > 0 && (blocknum % 5) == 0)
        {
            feebumper = feebumper + 1;
            curfee = curfee + 1;
            // at block 100, decrease num generated txs to simulate free blockspace
        }
        if (blocknum == 100)
        {
            numtxgen = 20;
        }
        for (int j = 0; j < numtxgen; j++)
        {
            tx.vin[0].prevout.n = 10000 * blocknum + 100 * j; // make transaction unique
            uint256 hash = tx.GetHash();
            mpool.addUnchecked(hash, entry.Fee(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) + feebumper)
                                         .Time(GetTime())
                                         .Priority(basepri) // a junk priority
                                         .Height(blocknum)
                                         .FromTx(tx, &mpool));
            assert((txHashesSize - 1) >= curfee);
            txHashes[curfee].push_back(hash);
        }
        // include 40 transactions into a block
        int index = curfee;
        while (vtx.size() < 40)
        {
            if (index >= 0) // cant access negative index in array
            {
                assert((txHashesSize - 1) >= curfee);
                size_t txAvail = txHashes[index].size();
                if (txAvail == 0)
                {
                    index--;
                    continue;
                }
                assert((txHashesSize - 1) >= curfee);
                uint256 txhash = txHashes[index].back();
                CTransactionRef ptx = mpool.get(txhash);
                if (ptx)
                {
                    vtx.push_back(ptx);
                }
                assert((txHashesSize - 1) >= curfee);
                txHashes[index].pop_back();
            }
            else
            {
                break;
            }
        }
        mpool.removeForBlock(vtx, ++blocknum, dummyConflicted);
        vtx.clear();
        if (blocknum % 5 == 0)
        {
            // we use *6 because our tx size is 188 bytes, if we add 1 sat to our fee it will make our fee rate go up by
            // 1000/188

            // even though we have high fees added in, because they are all cleared their bucket shouldnt affect the one
            // we are checking
            // for our fee estimate
            BOOST_CHECK(mpool.estimateFee(1) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(2) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(3) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(4) <= CFeeRate(1000 + (feebumper * 6)));
            BOOST_CHECK(mpool.estimateFee(5) <= CFeeRate(1000 + (feebumper * 6)));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
