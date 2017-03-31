// Copyright (c) 2013-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>


#define TEST_COMPLETED false  // remove once test completed

// we probably don't need BasicTestingSetup, but a fixture with 
// some file which can be read on test platforms.
BOOST_FIXTURE_TEST_SUITE(cbufferedfile_tests, BasicTestingSetup)


BOOST_AUTO_TEST_CASE(TestCBufferedFile)
{
    assert(TEST_COMPLETED);
}

BOOST_AUTO_TEST_SUITE_END()
