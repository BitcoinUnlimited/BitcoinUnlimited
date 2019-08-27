// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "electrum/electrs.h"
#include "test/test_bitcoin.h"
#include "util.h"

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace electrum;

BOOST_FIXTURE_TEST_SUITE(electrs_tests, BasicTestingSetup)

static bool electrs_args_has(const std::string &arg, const std::string &network = "main")
{
    const std::vector<std::string> args = electrs_args(42, network);
    return std::find(begin(args), end(args), arg) != end(args);
}

// Test case for github issue #1700
BOOST_AUTO_TEST_CASE(issue_1700)
{
    UnsetArg("-electrum.port");
    SetArg("-electrum.host", "foo");
    BOOST_CHECK(electrs_args_has("--electrum-rpc-addr=foo:50001"));

    UnsetArg("-electrum.host");
    SetArg("-electrum.port", "24");
    BOOST_CHECK(electrs_args_has("--electrum-rpc-addr=127.0.0.1:24"));

    SetArg("-electrum.port", "24");
    SetArg("-electrum.host", "foo");
    BOOST_CHECK(electrs_args_has("--electrum-rpc-addr=foo:24"));

    UnsetArg("-electrum.host");
    UnsetArg("-electrum.port");
    BOOST_CHECK(electrs_args_has("--electrum-rpc-addr=127.0.0.1:50001"));
    BOOST_CHECK(electrs_args_has("--electrum-rpc-addr=127.0.0.1:60001", "test"));
}

BOOST_AUTO_TEST_CASE(rawargs)
{
    BOOST_CHECK(electrs_args_has("--txid-limit=500"));
    BOOST_CHECK(!electrs_args_has("--txid-limit=42"));

    // Test that we override txid-limit and append server-banner
    mapMultiArgs["-electrum.rawarg"].push_back("--txid-limit=42");
    mapMultiArgs["-electrum.rawarg"].push_back("--server-banner=\"Hello World!\"");

    BOOST_CHECK(!electrs_args_has("--txid-limit=500"));
    BOOST_CHECK(electrs_args_has("--txid-limit=42"));
    BOOST_CHECK(electrs_args_has("--server-banner=\"Hello World!\""));

    mapMultiArgs.clear();
}

BOOST_AUTO_TEST_SUITE_END()
