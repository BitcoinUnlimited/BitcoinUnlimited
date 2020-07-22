// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "electrum/electrs.h"
#include "test/test_bitcoin.h"
#include "util.h"
#include "xversionkeys.h"
#include "xversionmessage.h"

#include <sstream>
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

BOOST_AUTO_TEST_CASE(rawargs_verboseness)
{
    Logging::LogToggleCategory(ELECTRUM, true);
    BOOST_CHECK(electrs_args_has("-vvvv"));
    BOOST_CHECK(!electrs_args_has("-v"));

    mapMultiArgs["-electrum.rawarg"].push_back("-v");
    BOOST_CHECK(!electrs_args_has("-vvvv"));
    BOOST_CHECK(electrs_args_has("-v"));

    mapMultiArgs["-electrum.rawarg"].push_back("-vv");
    BOOST_CHECK(!electrs_args_has("-vvvv"));
    BOOST_CHECK(electrs_args_has("-vv"));
    mapMultiArgs.clear();
    Logging::LogToggleCategory(ELECTRUM, false);
}

static void call_setter(std::unique_ptr<CXVersionMessage> &ver)
{
    constexpr char network[] = "main";
    ver.reset(new CXVersionMessage);
    set_xversion_flags(*ver, network);
}

BOOST_AUTO_TEST_CASE(electrum_xversion)
{
    constexpr uint64_t PORT = 2020;
    constexpr uint64_t NOT_SET = 0;

    UnsetArg("-electrum");
    UnsetArg("-electrum.host");
    std::stringstream ss;
    ss << PORT;
    SetArg("-electrum.port", ss.str());

    std::unique_ptr<CXVersionMessage> ver;

    // Electrum server not enabled
    call_setter(ver);
    BOOST_CHECK_EQUAL(NOT_SET, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PORT_TCP));
    BOOST_CHECK_EQUAL(NOT_SET, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PROTOCOL_VERSION));

    // Electrum server enabled, but host is localhost
    SetArg("-electrum", "1");
    SetArg("-electrum.host", "127.0.0.1");
    call_setter(ver);
    BOOST_CHECK_EQUAL(NOT_SET, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PORT_TCP));
    BOOST_CHECK_EQUAL(NOT_SET, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PROTOCOL_VERSION));

    // Electrum server enabled, but host is private network
    SetArg("-electrum.host", "192.168.1.42");
    call_setter(ver);
    BOOST_CHECK_EQUAL(NOT_SET, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PORT_TCP));
    BOOST_CHECK_EQUAL(NOT_SET, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PROTOCOL_VERSION));

    // Electrum server enabled and on public network
    SetArg("-electrum.host", "8.8.8.8");
    call_setter(ver);
    BOOST_CHECK_EQUAL(PORT, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PORT_TCP));
    BOOST_CHECK_EQUAL(1400000, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PROTOCOL_VERSION));

    // Special case: Listen on all IP's is treated as public
    SetArg("-electrum.host", "0.0.0.0");
    call_setter(ver);
    BOOST_CHECK_EQUAL(PORT, ver->as_u64c(XVer::BU_ELECTRUM_SERVER_PORT_TCP));
}

// Test case for gitlab issue #2221, passing boolean parameters did not work.
BOOST_AUTO_TEST_CASE(issue_2221)
{
    mapMultiArgs["-electrum.rawarg"].push_back("--disable-full-compaction");
    mapMultiArgs["-electrum.rawarg"].push_back("--jsonrpc-import");
    BOOST_CHECK(electrs_args_has("--disable-full-compaction"));
    BOOST_CHECK(electrs_args_has("--jsonrpc-import"));
}

BOOST_AUTO_TEST_SUITE_END()
