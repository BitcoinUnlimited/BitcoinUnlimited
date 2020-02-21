// Copyright (c) 2016-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "unlimited.h"

#include "../consensus/consensus.h"
#include "test/test_bitcoin.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <sstream>

using namespace std;

// Defined in rpc_tests.cpp not bitcoin-cli.cpp
extern UniValue CallRPC(string strMethod);

BOOST_FIXTURE_TEST_SUITE(excessiveblock_test, TestingSetup)

BOOST_AUTO_TEST_CASE(rpc_excessive)
{
    BOOST_CHECK_NO_THROW(CallRPC("getexcessiveblock"));

    BOOST_CHECK_NO_THROW(CallRPC("getminingmaxblock"));

    // Testing the parsing of input parameters of setexcessive block,
    // this RPC set the value for EB and AD and expect exactly 2 unsigned
    // integer parameter.

    // 1) RPC accept 2 parameters EB and AD and both has to be positive integer
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock not_uint"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 36000000 not_uint"), boost::bad_lexical_cast);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 36000000 -1"), boost::bad_lexical_cast);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock -1 0"), runtime_error);

    // 2) passing 3 params should raise an exception
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 1000 0 0"), runtime_error);

    // Testing the semantics of input parameters of setexcessive

    // 1) EB must be bigger than 32MB and bigger than MG
    BOOST_CHECK_NO_THROW(CallRPC("setminingmaxblock 33000000"));
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 32000000 1"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("setminingmaxblock 32000000"));
    BOOST_CHECK_NO_THROW(CallRPC("setexcessiveblock 32000000 1"));

    // Testing the parsing of inputs parameters of setminingmaxblock,
    // this RPC call set the value in byte for the max size of produced
    // block. It accepts exactly one parameter (positive integer) bigger
    // than 100 bytes

    // Passing 0 params should fail
    BOOST_CHECK_THROW(CallRPC("setminingmaxblock"), runtime_error);
    // Passing 2 parameters should throw an error
    BOOST_CHECK_THROW(CallRPC("setminingmaxblock 0 0"), runtime_error);

    // Test the semantics of the parameters of setminingmaxblock

    // MG can't be greater than EB
    BOOST_CHECK_THROW(CallRPC("setminingmaxblock 33000000"), runtime_error);
    // MG has to be an integer not a string
    BOOST_CHECK_THROW(CallRPC("setminingmaxblock not_uint"), boost::bad_lexical_cast);
    // MG has to be a positive integer
    BOOST_CHECK_THROW(CallRPC("setminingmaxblock -1"), boost::bad_lexical_cast);
    // MG has to be a positive integer greater than 100
    BOOST_CHECK_THROW(CallRPC("setminingmaxblock 0"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("setminingmaxblock 1000"));
    BOOST_CHECK_NO_THROW(CallRPC("setminingmaxblock 101"));

    // Set it back to the expected values for other tests
    BOOST_CHECK_NO_THROW(CallRPC("setexcessiveblock 32000000 12"));
    BOOST_CHECK_NO_THROW(CallRPC("setminingmaxblock 1000000"));
}

BOOST_AUTO_TEST_CASE(buip005)
{
    string exceptedEB;
    string exceptedAD;
    excessiveBlockSize = 1000000;
    excessiveAcceptDepth = 9999999;
    exceptedEB = "EB1";
    exceptedAD = "AD9999999";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize << " but was "
                                 << BUComments.front());
    BOOST_CHECK_MESSAGE(BUComments.back() == exceptedAD,
        "AD ought to have been " << exceptedAD << " when excessiveBlockSize = " << excessiveAcceptDepth);
    excessiveBlockSize = 100000;
    excessiveAcceptDepth = 9999999 + 1;
    exceptedEB = "EB0.1";
    exceptedAD = "AD9999999";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize << " but was "
                                 << BUComments.front());
    BOOST_CHECK_MESSAGE(BUComments.back() == exceptedAD,
        "AD ought to have been " << exceptedAD << " when excessiveBlockSize = " << excessiveAcceptDepth);
    excessiveBlockSize = 10000;
    exceptedEB = "EB0";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize << " but was "
                                 << BUComments.front());
    excessiveBlockSize = 1670000;
    exceptedEB = "EB1.6";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been rounded to " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize
                                            << " but was " << BUComments.front());
    excessiveBlockSize = 150000;
    exceptedEB = "EB0.1";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been rounded to " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize
                                            << " but was " << BUComments.front());
    excessiveBlockSize = 0;
    exceptedEB = "EB0";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been rounded to " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize
                                            << " but was " << BUComments.front());
    excessiveBlockSize = 3800000000;
    exceptedEB = "EB3800";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been rounded to " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize
                                            << " but was " << BUComments.front());
    excessiveBlockSize = 49200000000;
    exceptedEB = "EB49200";
    settingsToUserAgentString();
    BOOST_CHECK_MESSAGE(BUComments.front() == exceptedEB,
        "EB ought to have been rounded to " << exceptedEB << " when excessiveBlockSize = " << excessiveBlockSize
                                            << " but was " << BUComments.front());
    // set back to defaults
    excessiveBlockSize = 1000000;
    excessiveAcceptDepth = 4;
}


BOOST_AUTO_TEST_CASE(excessiveChecks)
{
    CBlock block;

    excessiveBlockSize = 16000000; // Ignore excessive block size when checking sigops and block effort

    // Check tx size values
    maxTxSize.Set(DEFAULT_LARGEST_TRANSACTION);

    // Within a 1 MB block, a 1MB transaction is not excessive
    BOOST_CHECK_MESSAGE(
        false == CheckExcessive(block, BLOCKSTREAM_CORE_MAX_BLOCK_SIZE, 1, BLOCKSTREAM_CORE_MAX_BLOCK_SIZE),
        "improper max tx");

    // With a > 1 MB block, use the maxTxSize to determine
    BOOST_CHECK_MESSAGE(
        false == CheckExcessive(block, BLOCKSTREAM_CORE_MAX_BLOCK_SIZE + 1, 1, maxTxSize.Value()), "improper max tx");
    BOOST_CHECK_MESSAGE(true == CheckExcessive(block, BLOCKSTREAM_CORE_MAX_BLOCK_SIZE + 1, 1, maxTxSize.Value() + 1),
        "improper max tx");
}

BOOST_AUTO_TEST_CASE(check_validator_rule)
{
    BOOST_CHECK(MiningAndExcessiveBlockValidatorRule(1000000, 1000000));
    BOOST_CHECK(MiningAndExcessiveBlockValidatorRule(16000000, 1000000));
    BOOST_CHECK(MiningAndExcessiveBlockValidatorRule(1000001, 1000000));

    BOOST_CHECK(!MiningAndExcessiveBlockValidatorRule(1000000, 1000001));
    BOOST_CHECK(!MiningAndExcessiveBlockValidatorRule(1000000, 16000000));

    BOOST_CHECK(MiningAndExcessiveBlockValidatorRule(1357, 1357));
    BOOST_CHECK(MiningAndExcessiveBlockValidatorRule(161616, 2222));
    BOOST_CHECK(MiningAndExcessiveBlockValidatorRule(88889, 88888));

    BOOST_CHECK(!MiningAndExcessiveBlockValidatorRule(929292, 929293));
    BOOST_CHECK(!MiningAndExcessiveBlockValidatorRule(4, 234245));
}

BOOST_AUTO_TEST_CASE(check_excessive_validator)
{
    // Saving EB / MG default value
    uint64_t c_mgb = maxGeneratedBlock;
    uint64_t c_ebs = excessiveBlockSize;

    // Tweaks validator is potentially executed twice for every set operation.
    // The first execution check the validity of the value we want to set the param to.
    // The second execution happens only if the assignment happened successfully and
    // could be used as notification update mechanism.
    // The first time with the 3rd par (validate) = true and the 2nd with validate = false.
    // If validate is true, the function is given a candidate and decides whether to allow
    // the assignment to happen, if validate is false, an assignment has happened
    // and you have the opportunity to add side effects (update the GUI or something)
    // if validate is true, the 2nd parameter represent the current value of the tweak
    // whereas the 1st one is the value we want it to be changed to. If validate is false
    // the assignment already happened hence the 2nd par is the new value (just set) and
    // the 1st par the previous one.


    // TEST 1): EB has to be always greater or equal to MG
    // TEST 2): EB has to be always greater or equal of MIN_EXCESSIVE_BLOCK_SIZE
    // Test 2 will be performed in validateblocktemplate.py

    // TEST 1)
    // new EB = 31MB, old EB = 32MB, perform validation

    // fudge global variables....
    std::string str;
    std::ostringstream expected_ret;
    maxGeneratedBlock = 32500000;
    excessiveBlockSize = 33000000;

    uint64_t tmpExcessive = 32000000;
    expected_ret << "Sorry, your maximum mined block (" << maxGeneratedBlock
                 << ") is larger than your proposed excessive size (" << tmpExcessive
                 << ").  This would cause you to orphan your own blocks.";

    str = ExcessiveBlockValidator(tmpExcessive, nullptr, true);
    BOOST_CHECK(str == expected_ret.str());

    // Restore default value for EB and MG
    maxGeneratedBlock = c_mgb;
    excessiveBlockSize = c_ebs;
}

BOOST_AUTO_TEST_CASE(check_generated_block_validator)
{
    uint64_t c_mgb = maxGeneratedBlock;
    uint64_t c_ebs = excessiveBlockSize;

    // fudge global variables....
    maxGeneratedBlock = 888;
    excessiveBlockSize = 1000000;

    uint64_t tmpMGB = 1000000;
    std::string str;

    str = MiningBlockSizeValidator(tmpMGB, nullptr, true);
    BOOST_CHECK(str.empty());

    maxGeneratedBlock = 8888881;
    str = MiningBlockSizeValidator(tmpMGB, nullptr, false);
    BOOST_CHECK(str.empty());

    str = MiningBlockSizeValidator(tmpMGB, (uint64_t *)42, true);
    BOOST_CHECK(str.empty());

    tmpMGB = excessiveBlockSize - 1;

    str = MiningBlockSizeValidator(tmpMGB, nullptr, true);
    BOOST_CHECK(str.empty());

    maxGeneratedBlock = 8888881;
    str = MiningBlockSizeValidator(tmpMGB, nullptr, false);
    BOOST_CHECK(str.empty());

    str = MiningBlockSizeValidator(tmpMGB, (uint64_t *)42, true);
    BOOST_CHECK(str.empty());

    tmpMGB = excessiveBlockSize + 1;

    str = MiningBlockSizeValidator(tmpMGB, nullptr, true);
    BOOST_CHECK(!str.empty());

    str = MiningBlockSizeValidator(tmpMGB, nullptr, false);
    BOOST_CHECK(str.empty());

    str = MiningBlockSizeValidator(tmpMGB, (uint64_t *)42, true);
    BOOST_CHECK(!str.empty());

    maxGeneratedBlock = c_mgb;
    excessiveBlockSize = c_ebs;
}


BOOST_AUTO_TEST_SUITE_END()
