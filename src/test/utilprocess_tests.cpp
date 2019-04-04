// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilprocess.h"
#include "test/test_bitcoin.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/predef/os.h>
#include <boost/test/unit_test.hpp>
#include <iostream>

BOOST_FIXTURE_TEST_SUITE(utilprocess_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(this_process_path_test)
{
#if BOOST_OS_LINUX
    std::string path = this_process_path();
    // TODO: replace boost with std::string::ends_with in C++20
    BOOST_CHECK(boost::algorithm::ends_with(path, "/test_bitcoin"));
#else
    BOOST_CHECK_THROW(this_process_path(), unsupported_platform_error);
#endif
}
BOOST_AUTO_TEST_SUITE_END()
