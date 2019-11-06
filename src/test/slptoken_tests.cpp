// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "slptokens/token.h"
#include "stat.h"
#include "test/test_bitcoin.h"
#include "univalue.h"

#include "data/slp_script_tests.json.h"
#include "data/slp_tx_input_tests.json.h"

#include <boost/test/unit_test.hpp>

extern UniValue read_json(const std::string &jsondata);

BOOST_FIXTURE_TEST_SUITE(slptoken_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(slp_script_data)
{
    UniValue tests = read_json(
        std::string(json_tests::slp_script_tests, json_tests::slp_script_tests + sizeof(json_tests::slp_script_tests)));

    for (unsigned int idx = 0; idx < tests.size(); idx++)
    // for (unsigned int idx = 0; idx < 5; idx++)
    {
        UniValue test = tests[idx];
        std::string strTest = test[0].getValStr();
        if (test.size() != 3 || !test[0].isStr() || !test[1].isStr() || !test[2].isNum())
        {
            BOOST_ERROR("Bad test format: " << test[0].getValStr().c_str());
            continue;
        }
        std::vector<unsigned char> testdata = ParseHex(test[1].getValStr().c_str());
        CScript scripttest(testdata.begin(), testdata.end());
        CSLPToken newToken;
        uint8_t result = newToken.ParseBytes(scripttest);
        uint8_t expected = test[2].get_uint8(); // 0 is the only number that means this test should pass
        if (result != expected && (result == 0 || expected == 0))
        {
            BOOST_ERROR("Test failed:" << test[0].getValStr().c_str());
            BOOST_ERROR("InputScript:" << test[1].getValStr().c_str());
            BOOST_ERROR("Failure Result:" << std::to_string(result) << " != " << std::to_string(expected));
            BOOST_ERROR("\n\n");
        }
        // BOOST_CHECK_EQUAL(result, expected);
    }
}
/*
BOOST_AUTO_TEST_CASE(slp_tx_inputs)
{
    UniValue tests = read_json(std::string(
        json_tests::slp_script_tests, json_tests::slp_script_tests + sizeof(json_tests::slp_tx_input_tests)));

    for (unsigned int idx = 0; idx < tests.size(); idx++)
    // for (unsigned int idx = 0; idx < 5; idx++)
    {
        UniValue test = tests[idx];
        std::string strTest = test[0].getValStr();
        if (test.size() != 3 || !test[0].isStr() || !test[1].isStr() || !test[2].isNum())
        {
            BOOST_ERROR("Bad test format: " << test[0].getValStr().c_str());
            continue;
        }
        std::vector<unsigned char> testdata = ParseHex(test[1].getValStr().c_str());
        CScript scripttest(testdata.begin(), testdata.end());
        CSLPToken newToken(0);
        uint8_t result = newToken.ParseBytes(scripttest);
        uint8_t expected = test[2].get_uint8(); // 0 is the only number that means this test should pass
        if (result != expected && (result == 0 || expected == 0))
        {
            BOOST_ERROR("Test failed:" << test[0].getValStr().c_str());
            BOOST_ERROR("InputScript:" << test[1].getValStr().c_str());
            BOOST_ERROR("Failure Result:" << std::to_string(result) << " != " << std::to_string(expected));
            BOOST_ERROR("\n\n");
        }
        // BOOST_CHECK_EQUAL(result, expected);
    }
}
*/
BOOST_AUTO_TEST_SUITE_END()
