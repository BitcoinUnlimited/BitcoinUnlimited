// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include "fastfilter.h"
#include "hash.h"

BOOST_FIXTURE_TEST_SUITE(fastfilter_tests, BasicTestingSetup)

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
        for (int i = 1; i < 20000; i++)
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
        // printf("collisions: %d\n", collisions);
        BOOST_CHECK(collisions < 400); // sanity check, actual result may vary
        // check them all again
        num = origNum;
        int numFalsePositives = 0;
        for (int i = 1; i < 20000; i++)
        {
            num += 1;
            uint256 t1 = ArithToUint256(num);
            uint256 tmp = Hash(t1.begin(), t1.end());
            BOOST_CHECK(filt.contains(tmp));
        }
        for (int i = 1; i < 20000; i++) // check a bunch of numbers we didn't add
        {
            num += 1;
            uint256 t1 = ArithToUint256(num);
            uint256 tmp = Hash(t1.begin(), t1.end());
            if (filt.contains(tmp))
                numFalsePositives++;
        }
        // printf("numFalsePositives: %d\n", numFalsePositives);
        BOOST_CHECK(numFalsePositives < 2000); // sanity check, actual result may vary
    }


    // Test the 4 MB filter since that's what we use
    {
        CFastFilter<4 * 1024 * 1024> filt;

        arith_uint256 num(0);
        int collisions = 0;
        for (int i = 0; i < 100000; i++)
        {
            num += 1;
            uint256 t1 = ArithToUint256(num);
            uint256 tmp = Hash(t1.begin(), t1.end());
            if (!filt.checkAndSet(tmp))
                collisions += 1;
            BOOST_CHECK(filt.contains(tmp));
        }
        // printf("collisions: %d\n", collisions);
        BOOST_CHECK(collisions < 2000); // sanity check, actual result may vary
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
    // printf("collisions: %d  rolling collisions: %d\n", collisions, rcollisions);
    BOOST_CHECK(rcollisions < collisions);
}


BOOST_AUTO_TEST_SUITE_END()
