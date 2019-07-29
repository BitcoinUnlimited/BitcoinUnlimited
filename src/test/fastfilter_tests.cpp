// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include "fastfilter.h"
#include "hashwrapper.h"

void TestVariableFastFilter(CVariableFastFilter filt, int buffer, int n, double fpr)
{
    //  pick a random start point for a randomized test
    // arith_uint256 num(insecure_rand.rand32());
    arith_uint256 num(1);
    arith_uint256 origNum = num;
    int collisions = 0;
    for (int i = 1; i < 50000; i++)
    {
        num += 1;
        uint256 t1 = ArithToUint256(num);
        // for the fastfilter to work without lots of collisions the data must be pseudo-random
        uint256 tmp = Hash(t1.begin(), t1.end());
        if (filt.contains(tmp))
        {
            collisions++;
        }
        filt.insert(tmp);
        BOOST_CHECK(filt.contains(tmp));
        BOOST_CHECK(!filt.checkAndSet(tmp));
    }
    BOOST_CHECK(collisions < 10); // sanity check, actual result may vary
    // check them all again
    num = origNum;
    int numFalsePositives = 0;
    for (int i = 1; i < 50000; i++)
    {
        num += 1;
        uint256 t1 = ArithToUint256(num);
        uint256 tmp = Hash(t1.begin(), t1.end());
        BOOST_CHECK(filt.contains(tmp));
    }
    for (int i = 1; i < 50000; i++) // check a bunch of numbers we didn't add
    {
        num += 1;
        uint256 t1 = ArithToUint256(num);
        uint256 tmp = Hash(t1.begin(), t1.end());
        if (filt.contains(tmp))
            numFalsePositives++;
    }
    BOOST_CHECK(numFalsePositives < buffer * n * fpr); // sanity check, actual result may vary
}

BOOST_FIXTURE_TEST_SUITE(fastfilter_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(variablefastfilter_dummy_constructor)
{
    CVariableFastFilter filt;

    uint256 t = ArithToUint256(1);
    uint256 tmp = Hash(t.begin(), t.end());
    filt.insert(tmp);
    BOOST_CHECK(filt.contains(tmp));
}

BOOST_AUTO_TEST_CASE(variablefastfilter_many_hash_funcs)
{
    int n = 4 * 1024 * 1024;
    int k = 10;
    double fpr = 0.1 * std::exp(-k * LN2); // Guaranteed to require > 10 hash funcs
    int buffer = 2;
    CVariableFastFilter filt(n, fpr);
    TestVariableFastFilter(filt, buffer, n, fpr);
}

BOOST_AUTO_TEST_CASE(variablefastfilter_tests)
{
    int n = 4 * 1024 * 1024;
    double fpr = 0.1;
    int buffer = 2;
    CVariableFastFilter filt(n, fpr);
    TestVariableFastFilter(filt, buffer, n, fpr);
}

BOOST_AUTO_TEST_CASE(fastfilter_tests)
{
    // Like a bloom filter the fast filter can have false positives but not false negatives
    FastRandomContext insecure_rand;
    {
        CFastFilter<1024 * 1024> filt;

        //  pick a random start point for a randomized test
        // arith_uint256 num(insecure_rand.rand32());
        arith_uint256 num(1);
        arith_uint256 origNum = num;
        int collisions = 0;
        for (int i = 1; i < 50000; i++)
        {
            num += 1;
            uint256 t1 = ArithToUint256(num);
            // for the fastfilter to work without lots of collisions the data must be pseudo-random
            uint256 tmp = Hash(t1.begin(), t1.end());
            if (filt.contains(tmp))
            {
                collisions++;
            }
            filt.insert(tmp);
            BOOST_CHECK(filt.contains(tmp));
            BOOST_CHECK(!filt.checkAndSet(tmp));
        }
        BOOST_CHECK(collisions < 10); // sanity check, actual result may vary
        // check them all again
        num = origNum;
        int numFalsePositives = 0;
        for (int i = 1; i < 50000; i++)
        {
            num += 1;
            uint256 t1 = ArithToUint256(num);
            uint256 tmp = Hash(t1.begin(), t1.end());
            BOOST_CHECK(filt.contains(tmp));
        }
        for (int i = 1; i < 50000; i++) // check a bunch of numbers we didn't add
        {
            num += 1;
            uint256 t1 = ArithToUint256(num);
            uint256 tmp = Hash(t1.begin(), t1.end());
            if (filt.contains(tmp))
                numFalsePositives++;
        }
        BOOST_CHECK(numFalsePositives < 10); // sanity check, actual result may vary
    }


    // Test the 4 MB filter since that's what we use
    {
        CFastFilter<4 * 1024 * 1024, 2> filt;
        CFastFilter<4 * 1024 * 1024, 8> filt2;

        arith_uint256 num(0);
        int collisions = 0;
        int collisions2 = 0;
        for (int i = 0; i < 100000; i++)
        {
            num += 1;
            uint256 t1 = ArithToUint256(num);
            uint256 tmp = Hash(t1.begin(), t1.end());
            if (!filt.checkAndSet(tmp))
                collisions += 1;
            if (!filt2.checkAndSet(tmp))
                collisions2 += 1;
            BOOST_CHECK(filt.contains(tmp));
            BOOST_CHECK(filt2.contains(tmp));
        }
        BOOST_CHECK(collisions < 100); // sanity check, actual result may vary
        BOOST_CHECK(collisions2 < 10); // sanity check, actual result may vary
    }
}

BOOST_AUTO_TEST_CASE(rollingfastfilter_tests)
{
    // Like a bloom filter the fast filter can have false positives but not false negatives
    FastRandomContext insecure_rand;
    CRollingFastFilter<1024 * 1024> rfilt;
    CFastFilter<1024 * 1024> filt;

    //  pick a random start point for a randomized test
    // arith_uint256 num(insecure_rand.rand32());
    arith_uint256 num(1);
    int rcollisions = 0;
    int collisions = 0;
    for (int i = 1; i < 2000000; i++)
    {
        num += 1;
        uint256 t1 = ArithToUint256(num);
        uint256 tmp = Hash(t1.begin(), t1.end());
        if (filt.contains(tmp))
        {
            collisions++;
        }
        if (rfilt.contains(tmp))
        {
            rcollisions++;
        }
        filt.insert(tmp);
        rfilt.insert(tmp);
        BOOST_CHECK(filt.contains(tmp));
        BOOST_CHECK(!filt.checkAndSet(tmp));
        BOOST_CHECK(rfilt.contains(tmp));
        BOOST_CHECK(!rfilt.checkAndSet(tmp));
    }
    BOOST_CHECK(rcollisions < collisions);
    // This next check is probabilistic, see comment in CRollingFastFilter insert()
    BOOST_CHECK(((double)rcollisions) / 2000000.0 < .02);
}


BOOST_AUTO_TEST_SUITE_END()
