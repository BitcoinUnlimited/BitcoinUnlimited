// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "electrum/electrumserver.h"
#include "test/test_bitcoin.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>

BOOST_FIXTURE_TEST_SUITE(electrumserver_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(isrunning)
{
#if (BOOST_OS_LINUX && (BOOST_VERSION >= 106500))
    if (!boost::filesystem::exists("/bin/sleep"))
    {
        std::cout << "Skipping " << __func__ << std::endl;
        return;
    }
    auto &server = electrum::ElectrumServer::Instance();
    BOOST_CHECK(server.Start("/bin/sleep", {"30"}));
    BOOST_CHECK(server.IsRunning());
    server.Stop();
    BOOST_CHECK(!server.IsRunning());
#endif
}

BOOST_AUTO_TEST_SUITE_END()
