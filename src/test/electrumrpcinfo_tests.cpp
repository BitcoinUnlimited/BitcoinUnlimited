// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "electrum/electrumrpcinfo.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <map>
#include <string>

using namespace electrum;

class ElectrumRPCInfoMOC : public ElectrumRPCInfo
{
public:
    bool ibd = false;
    bool isrunning = false;
    int height = 0;
    std::map<std::string, int64_t> info;

    int ActiveTipHeight() const override { return height; }
    bool IsInitialBlockDownload() const override { return ibd; }
    bool IsRunning() const override { return isrunning; }
    std::map<std::string, int64_t> FetchElectrsInfo() const override { return info; }
};

BOOST_FIXTURE_TEST_SUITE(electrumrpcinfo_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(help_throws) { BOOST_CHECK_THROW(ElectrumRPCInfo::ThrowHelp(), std::invalid_argument); }
BOOST_AUTO_TEST_CASE(info_status)
{
    ElectrumRPCInfoMOC rpc;
    UniValue status;

    rpc.isrunning = false;
    status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL("stopped", status["status"].get_str());

    rpc.isrunning = true;
    rpc.ibd = true;
    status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL("waiting for initial block download", status["status"].get_str());

    rpc.isrunning = true;
    rpc.ibd = false;
    rpc.info = {};
    status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL("initializing", status["status"].get_str());

    rpc.height = 100;
    rpc.info = {{INDEX_HEIGHT_KEY, 99}};
    status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL("indexing", status["status"].get_str());

    rpc.height = 100;
    rpc.info = {{INDEX_HEIGHT_KEY, 100}};
    status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL("ok", status["status"].get_str());
}

BOOST_AUTO_TEST_CASE(info_progress)
{
    ElectrumRPCInfoMOC rpc;
    rpc.height = 100;
    rpc.info = {{INDEX_HEIGHT_KEY, 99}};
    UniValue status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL(99., status["index_progress"].get_real());

    rpc.info = {{}};
    status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL(0., status["index_progress"].get_real());
}

BOOST_AUTO_TEST_CASE(info_indexheight)
{
    ElectrumRPCInfoMOC rpc;

    rpc.info = {{}};
    UniValue status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL(-1, status["index_height"].get_int());

    rpc.info = {{INDEX_HEIGHT_KEY, 100}};
    status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL(100, status["index_height"].get_int());
}

BOOST_AUTO_TEST_CASE(info_can_handle_longint)
{
    ElectrumRPCInfoMOC rpc;

    rpc.info = {{INDEX_HEIGHT_KEY, std::numeric_limits<int64_t>::max()}};
    UniValue status = rpc.GetElectrumInfo();
    BOOST_CHECK_EQUAL(std::numeric_limits<int64_t>::max(), status["index_height"].get_int64());
}

BOOST_AUTO_TEST_SUITE_END()
