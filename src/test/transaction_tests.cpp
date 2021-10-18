// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "data/tx_invalid.json.h"
#include "data/tx_valid.json.h"
#include "test/test_bitcoin.h"

#include "clientversion.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "key.h"
#include "keystore.h"
#include "main.h" // For CheckTransaction
#include "policy/policy.h"
#include "script/script.h"
#include "script/script_error.h"
#include "test/scriptflags.h"
#include "utilstrencodings.h"

#include <map>
#include <string>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/test/unit_test.hpp>

#include <univalue.h>

using namespace std;

// In script_tests.cpp
extern UniValue read_json(const std::string &jsondata);

BOOST_FIXTURE_TEST_SUITE(transaction_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(tx_valid)
{
    // Read tests from test/data/tx_valid.json
    // Format is an array of arrays
    // Inner arrays are either [ "comment" ]
    // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
    // ... where all scripts are stringified scripts.
    //
    // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
    UniValue tests = read_json(std::string(json_tests::tx_valid, json_tests::tx_valid + sizeof(json_tests::tx_valid)));

    ScriptError err;
    for (unsigned int idx = 0; idx < tests.size(); idx++)
    {
        UniValue test = tests[idx];
        string strTest = test.write();
        if (test[0].isArray())
        {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            map<COutPoint, CScript> mapprevOutScriptPubKeys;
            std::map<COutPoint, int64_t> mapprevOutValues;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
            for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++)
            {
                const UniValue &input = inputs[inpIdx];
                if (!input.isArray())
                {
                    fValid = false;
                    break;
                }
                UniValue vinput = input.get_array();
                if (vinput.size() != 3)
                {
                    fValid = false;
                    break;
                }

                COutPoint outpoint(uint256S(vinput[0].get_str()), vinput[1].get_int());
                mapprevOutScriptPubKeys[outpoint] = ParseScript(vinput[2].get_str());
                if (vinput.size() >= 4)
                {
                    mapprevOutValues[outpoint] = vinput[3].get_int64();
                }
            }
            if (!fValid)
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            string transaction = test[1].get_str();
            CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
            CTransaction tx;
            stream >> tx;

            CValidationState state;
            BOOST_CHECK_MESSAGE(CheckTransaction(MakeTransactionRef(CTransaction(tx)), state), strTest);
            BOOST_CHECK(state.IsValid());

            for (unsigned int i = 0; i < tx.vin.size(); i++)
            {
                if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                {
                    BOOST_ERROR("Bad test: " << strTest);
                    break;
                }

                CAmount amount = 0;
                if (mapprevOutValues.count(tx.vin[i].prevout))
                {
                    amount = mapprevOutValues[tx.vin[i].prevout];
                }

                unsigned int verify_flags = ParseScriptFlags(test[2].get_str());
                BOOST_CHECK_MESSAGE(
                    VerifyScript(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout], verify_flags,
                        MAX_OPS_PER_SCRIPT, TransactionSignatureChecker(&tx, i, amount, verify_flags), &err),
                    strTest);
                BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(tx_invalid)
{
    // Read tests from test/data/tx_invalid.json
    // Format is an array of arrays
    // Inner arrays are either [ "comment" ]
    // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
    // ... where all scripts are stringified scripts.
    //
    // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
    UniValue tests =
        read_json(std::string(json_tests::tx_invalid, json_tests::tx_invalid + sizeof(json_tests::tx_invalid)));

    ScriptError err = SCRIPT_ERR_OK;
    for (unsigned int idx = 0; idx < tests.size(); idx++)
    {
        UniValue test = tests[idx];
        string strTest = test.write();
        if (test[0].isArray())
        {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            map<COutPoint, CScript> mapprevOutScriptPubKeys;
            std::map<COutPoint, int64_t> mapprevOutValues;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
            for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++)
            {
                const UniValue &input = inputs[inpIdx];
                if (!input.isArray())
                {
                    fValid = false;
                    break;
                }
                UniValue vinput = input.get_array();
                if (vinput.size() != 3)
                {
                    fValid = false;
                    break;
                }

                COutPoint outpoint(uint256S(vinput[0].get_str()), vinput[1].get_int());
                mapprevOutScriptPubKeys[outpoint] = ParseScript(vinput[2].get_str());
                if (vinput.size() >= 4)
                {
                    mapprevOutValues[outpoint] = vinput[3].get_int64();
                }
            }
            if (!fValid)
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            string transaction = test[1].get_str();
            CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
            CTransaction tx;
            stream >> tx;

            CValidationState state;
            fValid = CheckTransaction(MakeTransactionRef(CTransaction(tx)), state) && state.IsValid();

            for (unsigned int i = 0; i < tx.vin.size() && fValid; i++)
            {
                if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                {
                    BOOST_ERROR("Bad test: " << strTest);
                    break;
                }

                CAmount amount = 0;
                if (mapprevOutValues.count(tx.vin[i].prevout))
                {
                    amount = mapprevOutValues[tx.vin[i].prevout];
                }

                unsigned int verify_flags = ParseScriptFlags(test[2].get_str());
                fValid = VerifyScript(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout], verify_flags,
                    MAX_OPS_PER_SCRIPT, TransactionSignatureChecker(&tx, i, amount, verify_flags), &err);
            }
            BOOST_CHECK_MESSAGE(!fValid, strTest);
            BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, ScriptErrorString(err));
        }
    }
}

BOOST_AUTO_TEST_CASE(basic_transaction_tests)
{
    // Random real transaction (e2769b09e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436)
    unsigned char ch[] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x6b, 0xff, 0x7f, 0xcd, 0x4f, 0x85, 0x65, 0xef, 0x40, 0x6d,
        0xd5, 0xd6, 0x3d, 0x4f, 0xf9, 0x4f, 0x31, 0x8f, 0xe8, 0x20, 0x27, 0xfd, 0x4d, 0xc4, 0x51, 0xb0, 0x44, 0x74,
        0x01, 0x9f, 0x74, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x49, 0x30, 0x46, 0x02, 0x21, 0x00, 0xda, 0x0d, 0xc6,
        0xae, 0xce, 0xfe, 0x1e, 0x06, 0xef, 0xdf, 0x05, 0x77, 0x37, 0x57, 0xde, 0xb1, 0x68, 0x82, 0x09, 0x30, 0xe3,
        0xb0, 0xd0, 0x3f, 0x46, 0xf5, 0xfc, 0xf1, 0x50, 0xbf, 0x99, 0x0c, 0x02, 0x21, 0x00, 0xd2, 0x5b, 0x5c, 0x87,
        0x04, 0x00, 0x76, 0xe4, 0xf2, 0x53, 0xf8, 0x26, 0x2e, 0x76, 0x3e, 0x2d, 0xd5, 0x1e, 0x7f, 0xf0, 0xbe, 0x15,
        0x77, 0x27, 0xc4, 0xbc, 0x42, 0x80, 0x7f, 0x17, 0xbd, 0x39, 0x01, 0x41, 0x04, 0xe6, 0xc2, 0x6e, 0xf6, 0x7d,
        0xc6, 0x10, 0xd2, 0xcd, 0x19, 0x24, 0x84, 0x78, 0x9a, 0x6c, 0xf9, 0xae, 0xa9, 0x93, 0x0b, 0x94, 0x4b, 0x7e,
        0x2d, 0xb5, 0x34, 0x2b, 0x9d, 0x9e, 0x5b, 0x9f, 0xf7, 0x9a, 0xff, 0x9a, 0x2e, 0xe1, 0x97, 0x8d, 0xd7, 0xfd,
        0x01, 0xdf, 0xc5, 0x22, 0xee, 0x02, 0x28, 0x3d, 0x3b, 0x06, 0xa9, 0xd0, 0x3a, 0xcf, 0x80, 0x96, 0x96, 0x8d,
        0x7d, 0xbb, 0x0f, 0x91, 0x78, 0xff, 0xff, 0xff, 0xff, 0x02, 0x8b, 0xa7, 0x94, 0x0e, 0x00, 0x00, 0x00, 0x00,
        0x19, 0x76, 0xa9, 0x14, 0xba, 0xde, 0xec, 0xfd, 0xef, 0x05, 0x07, 0x24, 0x7f, 0xc8, 0xf7, 0x42, 0x41, 0xd7,
        0x3b, 0xc0, 0x39, 0x97, 0x2d, 0x7b, 0x88, 0xac, 0x40, 0x94, 0xa8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76,
        0xa9, 0x14, 0xc1, 0x09, 0x32, 0x48, 0x3f, 0xec, 0x93, 0xed, 0x51, 0xf5, 0xfe, 0x95, 0xe7, 0x25, 0x59, 0xf2,
        0xcc, 0x70, 0x43, 0xf9, 0x88, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00};
    vector<unsigned char> vch(ch, ch + sizeof(ch) - 1);
    CDataStream stream(vch, SER_DISK, CLIENT_VERSION);
    CMutableTransaction tx;
    stream >> tx;
    CValidationState state;
    BOOST_CHECK_MESSAGE(CheckTransaction(MakeTransactionRef(CTransaction(tx)), state) && state.IsValid(),
        "Simple deserialized transaction should be valid.");

    // Check that duplicate txins fail
    tx.vin.push_back(tx.vin[0]);
    BOOST_CHECK_MESSAGE(!CheckTransaction(MakeTransactionRef(CTransaction(tx)), state) || !state.IsValid(),
        "Transaction with duplicate txins should be invalid.");
}

//
// Helper: create two dummy transactions, each with
// two outputs.  The first has 11 and 50 CENT outputs
// paid to a TX_PUBKEY, the second 21 and 22 CENT outputs
// paid to a TX_PUBKEYHASH.
//
static std::vector<CMutableTransaction> SetupDummyInputs(CBasicKeyStore &keystoreRet, CCoinsViewCache &coinsRet)
{
    std::vector<CMutableTransaction> dummyTransactions;
    dummyTransactions.resize(2);

    // Add some keys to the keystore:
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(i % 2);
        keystoreRet.AddKey(key[i]);
    }

    // Create some dummy input transactions
    dummyTransactions[0].vout.resize(2);
    dummyTransactions[0].vout[0].nValue = 11 * CENT;
    dummyTransactions[0].vout[0].scriptPubKey << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    dummyTransactions[0].vout[1].nValue = 50 * CENT;
    dummyTransactions[0].vout[1].scriptPubKey << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
    AddCoins(coinsRet, dummyTransactions[0], 0);

    dummyTransactions[1].vout.resize(2);
    dummyTransactions[1].vout[0].nValue = 21 * CENT;
    dummyTransactions[1].vout[0].scriptPubKey = GetScriptForDestination(key[2].GetPubKey().GetID());
    dummyTransactions[1].vout[1].nValue = 22 * CENT;
    dummyTransactions[1].vout[1].scriptPubKey = GetScriptForDestination(key[3].GetPubKey().GetID());
    AddCoins(coinsRet, dummyTransactions[1], 0);

    return dummyTransactions;
}

BOOST_AUTO_TEST_CASE(test_Get)
{
    CBasicKeyStore keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    std::vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, coins);

    CMutableTransaction t1;
    t1.vin.resize(3);
    t1.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t1.vin[0].prevout.n = 1;
    t1.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
    t1.vin[1].prevout.hash = dummyTransactions[1].GetHash();
    t1.vin[1].prevout.n = 0;
    t1.vin[1].scriptSig << std::vector<unsigned char>(65, 0) << std::vector<unsigned char>(33, 4);
    t1.vin[2].prevout.hash = dummyTransactions[1].GetHash();
    t1.vin[2].prevout.n = 1;
    t1.vin[2].scriptSig << std::vector<unsigned char>(65, 0) << std::vector<unsigned char>(33, 4);
    t1.vout.resize(2);
    t1.vout[0].nValue = 90 * CENT;
    t1.vout[0].scriptPubKey << OP_1;

    BOOST_CHECK(AreInputsStandard(MakeTransactionRef(CTransaction(t1)), coins, false));
    BOOST_CHECK(AreInputsStandard(MakeTransactionRef(CTransaction(t1)), coins, true));
    BOOST_CHECK_EQUAL(coins.GetValueIn(t1), (50 + 21 + 22) * CENT);
}


BOOST_AUTO_TEST_CASE(test_IsStandard)
{
    LOCK(cs_main);
    CBasicKeyStore keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    std::vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, coins);

    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t.vin[0].prevout.n = 1;
    t.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = 90 * CENT;
    CKey key;
    key.MakeNewKey(true);
    t.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    string reason;
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // Check dust with default threshold:
    nDustThreshold.Set(DEFAULT_DUST_THRESHOLD);
    // dust:
    t.vout[0].nValue = nDustThreshold.Value() - 1;
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    // not dust:
    t.vout[0].nValue = nDustThreshold.Value();
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // Check dust with odd threshold
    nDustThreshold.Set(1234);
    // dust:
    t.vout[0].nValue = 1234 - 1;
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    // not dust:
    t.vout[0].nValue = 1234;
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    nDustThreshold.Set(DEFAULT_DUST_THRESHOLD);

    t.vout[0].scriptPubKey = CScript() << OP_1;
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    BOOST_CHECK(CTransaction(t).HasData() == false);

    // Check max LabelPublic: MAX_OP_RETURN_RELAY-2 byte TX_NULL_DATA
    nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;
    uint64_t someNumber = 17; // serializes to 2 bytes which is important to make the total script the desired len
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << CScriptNum::fromIntUnchecked(someNumber)
                                       << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef7105"
                                                   "2312");
    BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY, t.vout[0].scriptPubKey.size());
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // MAX_OP_RETURN_RELAY-byte TX_NULL_DATA in multiple outputs (standard after May 2021 Network Upgrade)
    t.vout.resize(3);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("646578784062697477617463682e636f2092c558ed52c56d");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN << ParseHex("8dd14ca76226bc936a84820d898443873eb03d8854b21fa3");
    t.vout[2].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("952b99a2981873e74509281730d78a21786d34a38bd1ebab"
                                                   "822fad42278f7f4420db6ab1fd2b6826148d4f73bb41ec2d"
                                                   "40a6d5793d66e17074a0c56a8a7df21062308f483dd6e38d"
                                                   "53609d350038df0a1b2a9ac8332016e0b904f66880dd0108"
                                                   "81c4e8074cce8e4ad6c77cb3460e01bf0e7e811b5f945f83"
                                                   "732ba6677520a893d75d9a966cb8f85dc301656b1635c631"
                                                   "f5d00d4adf73f2dd112ca75cf19754651909becfbe65aed1");
    BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY,
        t.vout[0].scriptPubKey.size() + t.vout[1].scriptPubKey.size() + t.vout[2].scriptPubKey.size());
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // MAX_OP_RETURN_RELAY+1-byte TX_NULL_DATA in multiple outputs (non-standard)
    t.vout[2].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("952b99a2981873e74509281730d78a21786d34a38bd1ebab"
                                                   "822fad42278f7f4420db6ab1fd2b6826148d4f73bb41ec2d"
                                                   "40a6d5793d66e17074a0c56a8a7df21062308f483dd6e38d"
                                                   "53609d350038df0a1b2a9ac8332016e0b904f66880dd0108"
                                                   "81c4e8074cce8e4ad6c77cb3460e01bf0e7e811b5f945f83"
                                                   "732ba6677520a893d75d9a966cb8f85dc301656b1635c631"
                                                   "f5d00d4adf73f2dd112ca75cf19754651909becfbe65aed1"
                                                   "3a");
    BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY + 1,
        t.vout[0].scriptPubKey.size() + t.vout[1].scriptPubKey.size() + t.vout[2].scriptPubKey.size());
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // TODO: The following check may not be applicible post May 2021 upgrade
    // Check that 2 public labels are not allowed
    t.vout.resize(2);
    t.vout[1].scriptPubKey = CScript() << OP_RETURN << CScriptNum::fromIntUnchecked(someNumber)
                                       << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef7105"
                                                   "2312");
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // Check that 1 pub label and 1 normal data is not allowed
    t.vout[1].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef7105"
                                                   "2312");
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    t.vout.resize(1);


    // Check max LabelPublic: MAX_OP_RETURN_RELAY-byte TX_NULL_DATA
    // MAX_OP_RETURN_RELAY+1-2 -byte TX_NULL_DATA (non-standard)
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << CScriptNum::fromIntUnchecked(someNumber)
                                       << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef7105"
                                                   "2312ac");
    BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY + 1, t.vout[0].scriptPubKey.size());
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // Check when a custom value is used for -datacarriersize .
    nMaxDatacarrierBytes = 90;

    // Max user provided payload size in multiple outputs is standard
    // after the May 2021 Network Upgrade.
    t.vout.resize(2);
    t.vout[1].nValue = 0;
    t.vout[0].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                                                   "a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("271967f1a67130b7105cd6a828e03909a67962e0ea1f61de"
                                                   "b649f6bc3f4cef3877696e646578");
    BOOST_CHECK_EQUAL(t.vout[0].scriptPubKey.size() + t.vout[1].scriptPubKey.size(), 90);
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // Max user provided payload size + 1 in multiple outputs is non-standard
    // even after the May 2021 Network Upgrade.
    t.vout[1].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("271967f1a67130b7105cd6a828e03909a67962e0ea1f61de"
                                                   "b649f6bc3f4cef3877696e64657878");
    BOOST_CHECK_EQUAL(t.vout[0].scriptPubKey.size() + t.vout[1].scriptPubKey.size(), 91);
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // Reset datacarriersize back to default [standard] size
    nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;
    t.vout.resize(1);

    // MAX_OP_RETURN_RELAY-byte TX_NULL_DATA (standard)
    nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;
    t.vout[0].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef7105"
                                                   "2312acbd");
    BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY, t.vout[0].scriptPubKey.size());
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // MAX_OP_RETURN_RELAY+1-byte TX_NULL_DATA (non-standard)
    t.vout[0].scriptPubKey = CScript() << OP_RETURN
                                       << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a671"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38ce"
                                                   "30b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef7105"
                                                   "2312acbdab");
    BOOST_CHECK_EQUAL(MAX_OP_RETURN_RELAY + 1, t.vout[0].scriptPubKey.size());
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    BOOST_CHECK(CTransaction(t).HasData(2969406055) == false); // dataID (first data after op_return) too long
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("678afdb0");
    BOOST_CHECK(CTransaction(t).HasData() == true);
    BOOST_CHECK(CTransaction(t).HasData(2969406055) == true);
    BOOST_CHECK(CTransaction(t).HasData(12345678) == false); // wrong dataID

    // Data payload can be encoded in any way...
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("");
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("00") << ParseHex("01");
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    // OP_RESERVED *is* considered to be a PUSHDATA type opcode by IsPushOnly()!
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << OP_RESERVED << -1 << 0 << ParseHex("01") << 2 << 3 << 4 << 5 << 6
                                       << 7 << 8 << 9 << 10 << 11 << 12 << 13 << 14 << 15 << 16;
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    BOOST_CHECK(CTransaction(t).HasData() == true);
    BOOST_CHECK(CTransaction(t).HasData(1) == false);

    t.vout[0].scriptPubKey =
        CScript() << OP_RETURN << 0 << ParseHex("01") << 2
                  << ParseHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // ...so long as it only contains PUSHDATA's
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << OP_RETURN;
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // TX_NULL_DATA w/o PUSHDATA
    t.vout.resize(1);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    // Only one TX_NULL_DATA permitted in all cases, until the May 2021 network upgrade
    t.vout.resize(2);
    t.vout[0].scriptPubKey =
        CScript() << OP_RETURN
                  << ParseHex("04578afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    t.vout[1].scriptPubKey =
        CScript() << OP_RETURN
                  << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    BOOST_CHECK(CTransaction(t).HasData() == true);

    t.vout[0].scriptPubKey =
        CScript() << OP_RETURN
                  << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
    BOOST_CHECK(CTransaction(t).HasData() == true);
    BOOST_CHECK(CTransaction(t).HasData(1) == false);

    // Check two op_returns have data...
    // this is nonstandard until the May 2021 network upgrade, but we check it anyway
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("04578afd");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afd");

    BOOST_CHECK(CTransaction(t).HasData() == true);
    BOOST_CHECK(CTransaction(t).HasData(4253701892) == true); // make sure both vouts are checked
    BOOST_CHECK(CTransaction(t).HasData(4253705988) == true);
    BOOST_CHECK(CTransaction(t).HasData(4253705989) == false);

    // Every OP_RETURN output script without data pushes is one byte long,
    // so the maximum number of outputs will be nMaxDatacarrierBytes.
    t.vout.resize(nMaxDatacarrierBytes + 1);
    for (auto &out : t.vout)
    {
        out.nValue = 0;
        out.scriptPubKey = CScript() << OP_RETURN;
    }
    BOOST_CHECK(!IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));

    t.vout.pop_back();
    BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(t)), reason));
}

BOOST_AUTO_TEST_CASE(large_transaction_tests)
{
    // Random valid large transaction with 125 inputs
    std::string raw_tx =
        "010000007d2202f7d624559a2dd159dfa93ad7915032064f718c5d838c9d01ee0caea6032c000000004847304402205fa7c46d0df544f5"
        "85f566bdc75ec9b475f2b1b0e9a92d314ef7aa1141d6c4fb0220571beb5b81608af864ed78fd1c37bdb162192e01f3bea735a6cf89ace8"
        "77176741feffffff5e6b96f0e4b81d821c5dd84c96f22c0b7cedc848456c4079175d0049c60e3a510000000049483045022100deef4f7b"
        "85be8d2887239e3ec36f08b7dc68ed9632f9034652c10efd1af78c51022072970cb99c39f63d673a11d7f9d641149528e329211f937992"
        "58d382934654d741feffffff4c1a26d4c370acae30275ddbd2209139dc8027590a090cac07d36b27f750181f0000000049483045022100"
        "ebe5f01eeb3ea60adb4641a116af76741bc2bad69921c9cea76b5751481fe24c02204236a8c5a611fd86a4f16e0a96a4d793e22bd7f14b"
        "030f8145453d92edca94d241feffffffd1f0ae347775ab54e9a4bddd1b8015103720101d27b7ac2472eb21eb85324c1400000000494830"
        "45022100d2dfb4756c69e3660959fb7345e31f60045ef0bd4a322a1a1e07f338766dad0802207f8f2f5814f84b6b74eb78da8ee250f983"
        "dcf076b4cddb1a25f642f6c0166af941feffffff1183d4d41238f2c03e88d164dd0ef2a951304e285fbad75798a7eb80642c33a6000000"
        "0049483045022100daf4c0aff824a1fc4953d25761f1f6dd2743af8928aa89d8c6d2309a438edb2e02205ffc726673deabff5f0245a438"
        "4312ee53a6519cd9582fbcdd8abe39a34ef0cc41feffffff9b3d7cd1912edc2f182f530c726a742524fd5fb06c7888e726f33730be7aa8"
        "1b000000004847304402205f4f5991130f86608d145dc131961fa6bb6265872e7320142d703bfcc283e33f02204857e4cfd8450e17e684"
        "71944f91d88d7253c98a6fe1fd84ea1d16fb925ed30641feffffff91e6afd3dba4da2f53ab521b27598b9255844010eeaca83274bfd77a"
        "2fbcef0000000000484730440220492c68f6304e687d480fa1ab7ca82441c420372792e851e9e1370c98959ff5e902207932af8c860c05"
        "b962ac9aed89d0abdebc41f969dd26f360ab51911d133ba77c41feffffff128fef3dd9da9672c93f93cfa17db1b2a311ec3d422aa57d60"
        "627b6d16f0685c000000004847304402203ff6783c08ce38fe5a38c557528a294e96070faf20852722664515ff3453f944022011917978"
        "3edb20ae04df784c057cd80336076b5074fdecd6c89039b8a7e32fca41feffffff99cf04e02f1efd5387a8492b69e1e9d3913fd5160cb8"
        "92ec4d7af3e62c8bde2600000000484730440220749abaf4ed0c085b64740d1eae94d7e8d0a107bd908ccb040e9167456ea94f33022043"
        "40184d5513480ab19b0251c43b1d4e32b61933efe5bf1419f65879e271900241fefffffffafbe8063b98eab4e7f636e4d9e8e4fc8d5580"
        "cf11db0a84f4286ea2b022196800000000494830450221008f7b574ba026cc3a3aa2a09acecb705b7e3e18867fec9f78aa925bcac6ebf1"
        "f402206cbab83e1def9b66ff6ed3815124765000a38f8c47b96d081b500c54609cd9e341feffffff829f33241d94623f235459a0406394"
        "c651f5d41e3e4cf5074f559a270713e7ca0000000048473044022060c634c2dccdc93e0970d313467c27c2b2681458a7809e600d0f4ee7"
        "a80f9500022060220a42ee04ff0ba5bda4011299320fa1fe25fa8f58ba64d8b5edbb9cdebffc41feffffff300b9bcb9044feba2199e80e"
        "6d09f2e0352c30a15b0bebedbeda5a783c9128230000000048473044022044b5fe427edbdde92e02fe38a831c6e311b444623054750854"
        "52e3ac7d9563bf0220690e1d27cbd68c73fb4fa09db0be020ed2430b84dcba243fb0c4d18249e8708441feffffff419086b592da0a20e5"
        "61c337a8f04cb0a943bef224acdd73e3180a699fa79ee0000000004847304402203509194dbea4482f49d865789ff89b235aa601adca1a"
        "54bf48b3ec6d167ea0770220266f6afd72bf75aaf07521cf70c60cf4815df22156436e9390d57127e7d6e18641feffffffd593d77c214b"
        "a2e5bebd645133691f5444506134a2cfe43e50ac0d32ac9e49c5000000004948304502210082e094ed586de3fc97fe71ebf9943931a83f"
        "65e1fbc899d4330091f4b696d4d902206d2136062fa7e11582af75a2793db2dc657f786e5f3dba4f0be3c86212bd516741feffffff4539"
        "ed69ee5bc328b515db24f97fbff05ae942a042602d26e7eac0211fde1eba0000000048473044022058c48bc0564375f8c627c62cbb9c7a"
        "14dd987cdd3a7325b649740b222cd50def0220546fb156c57bb7703224296e9b8c910221d151832bebc1d8d046a5234afd346041feffff"
        "ff1f784c749fb12cdc27a0b668b9555c44bda1ab6140d1cca9d84d351d8ebbe18c0000000048473044022063a36ffb79bd370709da5066"
        "a10785dfbe11f48f37a1d5a2e2fb43547beabb6c02200c32b0a42a279513ec11b14725a73a38f616d33c0379ef17ca22e8963e25ae0941"
        "feffffffd27f9cd2ae8c8fc81beab57435cee33ebbde49a1ee895d2ef1d9d778cc6121c00000000049483045022100f6d4c5028043854d"
        "7091b2c4370c6ee1d323d4c35f59da311bc25a2becb4eff002206adb15180986076fc0d102500fa60a30aecb2969b352fdc6fc1b43c38b"
        "471a6f41feffffff53b7da392ce9c3e4edb801ad9daa00c0a67e5ebb1e44dc0e4e2b68e8729d83a4000000004847304402204bff2f6dbb"
        "9d4d8e36f808caa67f0bf36ddc417ed7440a1f97428f762a70d04902201a7ebf61483a81339df14ae8d6404ad8d2d6af3af989c76467c9"
        "0fddb8f46a8a41feffffff6372983daf146c2d3271f17a644fd0598d266bba0cab4cb21f5ed286b05e03d2000000004847304402207f7d"
        "a273b6068e0e096737e293e7dece23803e229f24819e070b7008230ac64c022040cb5bb70b83cd5014eb7b403d97db0fd3ae8053cdcf6c"
        "d5aee6b54882a9e5b841fefffffff2f775c7e6cb970e644edea2477177a4d4238877826a53e162e85352b90ea52b000000004948304502"
        "21009a3c24abaa17885aee8b9aa615646315c1a805386b97c7a20d661ee7411779860220270602b5eeb904718dba48a2f38002ade04a69"
        "979daeb47a183376b64db6cae441feffffffe1e030b24a29f431d67ed9824af7c7e203b8655a2f8f7573c16ac92fbaf9d9460000000049"
        "483045022100be0016a84e189b78b34b2c259767eae34232f693c128ac53e93d4d2ac99ab5fe02207a5d99e1a800f2aa9180069aff96a0"
        "d3b60567f2329ee25e202161e2fa19a87d41feffffff0b946519f115f29dc08ee57124e7fd15a5c689228d52c6817b52281edaa7355100"
        "0000004847304402200a869e6ac62cbdffd0417b34f800568cc4747906f002eece0ecdf82f5272444202207f19b1c8604125362af0783b"
        "bc2dbd160322ae752d49dfe15f6da687f0d13da541feffffffa6c7126e51ea3a29d390f0df3842a4978b02e72bc8a92b3ad14c4a9ff314"
        "b7af0000000049483045022100faf7652da2d4b9b95e57fc64e6ae329e3b52e14557d1963bdf5154df80ec607702207999aa23feb632e5"
        "8654ea9eb7f62fa34f8fc0cc1ccd76176f9e2ea0399a9b0d41feffffff11004585e345afe777cddaeaff9ef3e4d0568d82c784b24fbd3c"
        "a0a465deaa270000000048473044022013fb83d9ae7a45ed90c6a4d757f12f849e57993df441f0abc837943644d8291c022048915ecc0e"
        "e6d8ff977984084cd8f71ad914c884b39edc835dab29c572567bfe41feffffff8b789d0e172ab04aa0e3d194ca653118cc17544bef9e3a"
        "5697336deefe4c5c220000000048473044022030b082b21d280131e402f2b49b20858dba306bb4a2a53b5ee0215efbcb39388002205d47"
        "8035b60f29c76f5c15f57c0a6d2ef3d9bbff40db3a816c9b34f05887f58941feffffff8c3043d53e083252401dab508368a87074bb94db"
        "2ca914472343a146360af4220000000048473044022045f09ce6eb53bdb7b572d4519adc255934d3b78a367834860686dec1cfe64a7d02"
        "204ee0a8f9fbf74687aa851de000c0b5e05348f052d4c23bb855902a7258498a3a41feffffff5d3f5572c653d702a4139c07f6b39119e1"
        "f158979422dec1eb04103995e9aa73000000004847304402203d7ce41e877799a0a4041a8f05ce3bcabb00d7128b3fbfed17fe36db12f4"
        "959b0220525baf63785b139634d6faba42fec3a3dbcb29ba1d869fa824603c4b56a19b0841feffffff514b0cbb1902abfadbdd42537b71"
        "f19f258f71e0784ce26c104f2db95a332a4e000000004847304402203979d60439cef35ab3091c351cb1de2369c00069237b4d4f0775a7"
        "e527d8bc6602204e94605361bdf8758a2ff695a174b870807fa1607aefd59e192e21acc41f7ccd41feffffffd5a7dfbe2c59a544e07d5a"
        "2c0cc1322ae7ffcb69317a30330848b9c71f4d0de300000000494830450221008264ce02a62d70eb54580ac1ce649b5ae4e96842037ca7"
        "8be4981ef763dc14830220367774a6290634bc36f7d11ae0d55e95074ea06aae3c2d0607e636242634087641feffffff93fa81bcccfdc7"
        "92930fa9a1d4ebc6eb9c5f33f290fdc11e82815fb0a4fe46350000000048473044022015b8d4b77b16ac2cf3ed1c0ab52cc1d7758fa5be"
        "4bdb8f557ab2ed93138b363702201018469c61a6b3f3abc9d272b69494cd145adb5ea9dad74e73d41af26d782fb641feffffffffea1142"
        "a0a0621d12d2a72b95fe3d0cd0f4f8e6e9674238360966f48df8c210000000004948304502210094d0c5205ea3269aa9ba0d273a351288"
        "b1a115dc439492c00aac08cb447c89ab02202c87062505ed6bafa10ff5892ab39d70650d97c76624abe864771283e2add5cf41feffffff"
        "ad6592f5812f7638b58f053ec2f7ebfaabbe6f4365ae661b816d0885e7d2692c00000000484730440220293b85ac062eeb4577354e54a6"
        "53d4133b71ca2d7ab7745b3eb8592378049de00220068ae26fdc28ddb9bbf40246cb12998fa483757ed8a7ae89fad3f9a564484d5241fe"
        "ffffffa659be4cc8571aed2f6161b7efa375ebdbf70e8cbe792dd69699a4cf62b1171a0000000048473044022004920453f6d8c5d9042e"
        "d9cab460f9ec79ee945ef6b3c357c1a92f72106d65dc0220110954bc9b7077e7a30f0194967a17e1803c2c1cf2c10f1297d2c4e6be0c54"
        "6741feffffffa7781d7ecfc3be661f4ce668f28c6e1eb1fbca8a08291f738a0c9f93778fd53d000000004847304402201a42d0ee135b50"
        "9cb0038366bd5de86b9eb906a32d954d1b30321e1712121149022034202082508a36690d26bca5850d998985073ea210f67fa54ffb7239"
        "30d463da41feffffffa18c7861ad49f78c23d14795a689c1dd9e369a2f21180864ff46ea090643559400000000484730440220588553e9"
        "5145a3282c081c4b257765676dc903c1c59724af00ec6e25423bcd5f02206ad90cdd581f2abdb32d121522625a1ffadb29e64a055ae963"
        "526f19c307592e41feffffff58f6a268262ad285a86c58c9a3013d0d5c836a6bcaf81c841ce9922ccc8361970000000048473044022010"
        "d7f203be744eced1452dd796e10fb6d903db751578a58982b1f1f37736c5cf02204e883528f60fca5bb3cc56f595dd8cc751e294adc46c"
        "86d7903b2a5eeebcfeeb41feffffff15f30a25a2d785ec6b8ae736c2d250ee7bd6031ad5c2689484e989e7d79656af0000000049483045"
        "022100fb89cb6b44197d447ecf772e2b5b1983631d3f73502e3126da6aa7b02c5d0c07022046695378eecd33f90ac155938d1744dddf7e"
        "33196fbe24ae9194bed1ba7966f941feffffff102384c3d9cc2418bd7429592b4b92ce7162a9a28cc434c58663620b03820f3200000000"
        "494830450221008f264508df64c4ec67664ad96f7b8f30ef27c03c2063256983155d3bcdd5f3a902207638f6ad64eec30bdf93dc1f69b3"
        "13711ee35d3591d7d358da4b0441d657fbdc41feffffff0120383d97df391bf6adc5772153bef6ea8b3115c54999d3e19fd89d1c0b2383"
        "00000000484730440220097cd4b0fe8cf3c080b6f41e8a7d38db15ae647999f44d5e023adf06a15d2ce80220315c878ce166f5db62a483"
        "35e3190e024ae0fd0e67e2c136f533b314476ee30641feffffffba487ec2bf098a32324073e1bb2c205f1a0c142afa816b9bdb54fc960a"
        "f63a7b0000000049483045022100926c2a6498a6cb15afbf7a7ecc2e82881e7be6f75ccd10aa811965b37a8ff6340220266f60ecd3ede7"
        "f8f484d6d2bc413834c7b463177d375e8ce964f52c37e5d04841feffffff96b7a4f54a420c74f2f08b361725c112f397b10eb688c60426"
        "7acb9ec8d28fd40000000049483045022100da8fa80e9459b1963ed2b75d4654603f9df3e17ebbe97b6fc3fdff086205ebbe02201dfb97"
        "f6c2f0695905be9c18d0aea1a9d22b80c3559c2e385cb899eeae28b04f41feffffffa46c196b97b0c748d9d83376b3598a3a64ca3a4f7b"
        "56877f863a20ae1edcdc520000000049483045022100dc805988d60c0b212229b78ff26f3564a53e59b0b56eeabf726898027f2d2b9702"
        "200efa0d5c634355d896fc6aaa1dc808963c32b85f884b0201e9cd1613b13ca26141feffffffefe26e7075666e01e316bd03709532f97e"
        "ddf35ede566eb6e8ed1dc32e98b06a000000004847304402206fce7bce335d8081ae9626b5a6e51567467a68c74b01871b8b50324036d5"
        "a66a022011c532a0fa261ed205051630dc235eb56cd741bcf6cea8c437b3300d418009ef41feffffff6d54d8c078ba245f36a254e09e22"
        "2b5f9ed4bc763207a85d43b3c207192dbc1d00000000494830450221009136e6a2ac95d7671edbf6aa5b267b4688ab33b1d3a54943a28c"
        "c22adf46564d02201347fee1f052b6b6158a1de0222a828df7f67739e5913d74f04cd8ac096e979641feffffff140ee0090e1a361c7be8"
        "69aca7a5a9d4bcef630c906e0c6c04b8d6d30e7a392e0000000048473044022007ce53af8b800a5c57e476924578abb6b03fb69ff7262b"
        "e59fc156714bdef3f0022035fe659bdac538b60a28cf2fb12198fcaa2a1374526578355af1fcfc1331c8b141feffffff24e0a8498994c5"
        "0f6e121c643c81875132944913de4d3173575a06c993dbdb790000000049483045022100bd785475fae9ca9ad4702a452bf3a71b5a3015"
        "9b932cb7517a43d5bb8bb7c825022047c75895ed991f3740223658fc5a228b5d10c75254a9e2da66f0a55edd858f5341feffffff6f498a"
        "b7f3c1e7734717870b166e44c656a6a3139de21a80956d2f146fb8bef40000000048473044022005a7a4913cea7cbf50b1c1af6629088f"
        "ef823e2a9c0fc13527c29ad21b7af19002205414139115b1a23612214e2dd1a1ac5d7952dfe21a5b5487dd8c1fb0ffd4b74041feffffff"
        "4e4424653ba785d22a422aaecfb18e391a2c44e963fb977056329797094cae3e0000000049483045022100ed6ae4e750c745545e17aeb6"
        "8a67ce782b7c44cf04d510f60c07f153862cf081022079058a30551bd9f759d68b4cae2327664773d5f81281289375dd0318997edcac41"
        "feffffff0e84e2549ff3028e77fff796034f6bf72152423d027ff65cef28c726c6f191770000000049483045022100df5cf53cced5488a"
        "db33db21cec02e8554bfff4009cbd4a7790cdff0879f6cea02207b11f865b6e50be8a1990304a86750eb51ad5168f95a1c2eef84062844"
        "bd85a641feffffffc58219941efeed9a46feca9c421e4fe8aa57afc40cb72e880e1b6a66ce2ae1e40000000049483045022100cec27c41"
        "e5e29f6fedf6485e7988cdb8a8f031a8021ec6e8b57ea835e121ee5002207e12dc9fc0deac2d4a74ddef6f20d3d29f6e3de20a180247e4"
        "9c75472770a7d441fefffffff1819a6bb0beef300d9a705825be3bc12d433bf7b95c5b8d608f2bebd6972565000000004847304402202b"
        "423c56b5ca4587461d624c624b49506337362d2e08e03fd26118a1dd57a7d602206675b2a26760bd3a26b73e912412c126af754419ae5d"
        "1357bdee26faf0fba0e041feffffffd2ffdfa0b115188761f406725ec4f1a03c784ed84ae0b20bb6c6d8c792a179cf0000000048473044"
        "022014d9af1d9ce1d3c82f56ab31ac1f7d22756eb0347e206728db039b13f4bc8faa02204a3b6484cbf5583ede77be18f2a1e884a44121"
        "986c5bde71b5bf22bfa1e62e2541feffffff254a38f58bc2d60041a6f75d57b21bc2182edd9f0fd4440f8d7127ec996190310000000049"
        "483045022100b79128f27b72376607dc5249c573b7e18240beaeaed884761a45c7f32599835c022013847e7efce7b2e34540c03ffa2d90"
        "3d1d6452f16b8c97743e89f91148e2387741feffffff77add447258b46854341676e3389828c14443bc03e7de41aa6aed657bc450b3d00"
        "00000049483045022100c76bac4b5ca08656bc775c1f8e9bc6c53445c11f59d2e90d75830b07a0651f2202200b1f17249505c9ba30a181"
        "42713d4ef707a42295ce0fe3876e748eb582903bad41feffffffb0f9593fba438b7d6472a786c3618d025604ad7c14fe8f68e7718258e4"
        "b8b5050000000048473044022025ef82d34108b20536f6ec5d6b5f537dbddcc9fbcacc107fbd61a990272150eb022021c844827fa4757d"
        "5f012ac09d40c1ee76ce163f1d357d8b96d1d8d699ef59ee41feffffff45f47d7457c065092a56dc0ecc609609632fd6b339a4e044b320"
        "062c8e36cbec0000000048473044022026080ac77c1b8ceb2fb2b9cb57d0cb96b8f4ed8023776685a04783929ed94cc00220066ce8679c"
        "525e5d5e185bf326f77a6144ee152c5b7bd54cd523da7f4998444b41feffffffee2d0ea138202cefd2076f1a478820944d017f02404dcb"
        "5537fcd76d0c1720ba000000004847304402207fea1d000dd9caf64b82af27954fcf8860bdf491afeab4c90c030b54c67c4d3b02204aea"
        "b0697717b1fe30623be79b0a24c0b2d1956811224fa234613ecf0ade870641feffffff7bdd68cbdb0e68ac6c8f5c3809f8adeb37446272"
        "ac0a61ef5db620226cae042b0000000049483045022100e92fb79f6fe16d8954cf29a0a7099b662ad813f823ddac020295afac8444645f"
        "0220724732653989cb032d8112696b1c0439fb825d94b749d0f35b624821327f2c0a41fefffffff9d8d1f94e5cb70526f36d651c873d00"
        "59eb78bf79ce229aa540dc618d86512d000000004847304402201838cd056e15f879e37a52929c44bfd80e930592e43d85d822a0f0c4a7"
        "71f75402207fad4a758ad78d90bf08bbfcd1d5e0d012135b3094322dadbc9aa4cf6327ce8341fefffffff2138bf8ffc344de06249c6a31"
        "ecc3e613e50feec601fe927e615db2251472cb00000000494830450221009a0227734329abfe71453524c8c212102c803373dcde85c52c"
        "d0ba3570e4360502205fb12f68adec608f0932afb1e4982e06dffe6ace9583498d626738bf5399fdc341fefffffff7ab93e096a2051852"
        "18ddf4d8b7fdc6af7fcf9778b0289273cfcb030b94958000000000484730440220053928d100314b5c640e77643843aa520fdc47095daf"
        "c31830ebfcb14a33c99a022045c63e86fee1ec18713bf9fcd726de84c58b300bb20eba809f2affda99c8cb7241feffffffcdfc4c348d1b"
        "c30e2b97341a24f6ab385595cfecffc838abbd8007c7d71a47d3000000004847304402201e151e4d622100bd526fd42e08dfc09cf17dcd"
        "ee69f50e9e03f5ec7608f39737022053d77f9a51a1982086922d0a609cb7f620b77d8f6e06e41cf1b71ddea89fc11741fefffffff5e19b"
        "b2d1b8579baef0c081563cd9147006602ee00e01fdc211b3f805281f280000000048473044022062c67f2544a6a7f363241e39ac3951d2"
        "2c8bab8d377b69b7cf35d5b53cd3426d022053e9668b1125e9215319be78ac0b794291fcff63028ec89f2946ad65a55d7f6941feffffff"
        "ba0fe736bea3afafe30671b41cb5e114a3e9919b983ceacf83ea126522597621000000004847304402207b7c1fc185f82eebb57a9cbf6d"
        "9954e15eab6cd5b526e75099ff7bd72c82613e02202e879dd4abcd4a7c55231989865541cb1a9770423e5ee4a4c114a06ef0cc16b241fe"
        "ffffff865e2021269dc598d1ba0ecfd2664621f9d7e7b884f1f23b4f1b6d58d47e47820000000049483045022100d65d11ca23185b6cf5"
        "77f214f498439c2d4790265484da3c3f4e4789e5bb81e702202a6a4615a4e0654c60ddd4d9f95fb13b791f8f7fdfba94afbc5ed40301bc"
        "a75841feffffff12a672257beb2eb4c8ba94be27f4273aedd2cc1747de2959b8b65432e9010f950000000048473044022027caaf84fe75"
        "cedba1b4b6f50f2a2d8fba1cf1ac8e28a317a7b85d91400c4fa902206312e37ece747f1786eacd18ef4b1fd565e0a437bf4d9360ac86a2"
        "1862f024d841feffffff23484ea83c0b627ef19a14a91404ea5349b704d5853e1c7768d592055a8d70ba000000004847304402205640f3"
        "6d14858c3083ffcd8e517a748c24c64b65bc0e1444b050378783d244ee0220344bc111cbfb08c9e80a60c6157a92971b91a0224a75c155"
        "adf216dfac0e496041feffffff4d40020f37eed8bcc9e2c00d3b2907f99cb5d6ea8e397a02c397284cf2b513a500000000494830450221"
        "00e7ad7af39c7d1814f31d326ad34c7f98aa36079917878b08be54dac3e33c39d8022055a1c8083711c23e6d69d0b23b8977c7626515bc"
        "6369f7a16f3bfd536d06931241feffffffb1e88d399df1145afb94dd8c72594f2391be508635f053a6becd0d43d7bffdd9000000004948"
        "3045022100c570eeeba9815bea50d99d8c2c449cfec008db6d7381ae2a12cc4cad722d66f7022035024b4fa56315c1809dce10e53bf01c"
        "ec9f88a9d3c5991a1e20dcb8b45b395a41feffffff9c996c10d66f2e367f5ceb5dd6068f9ad2248552701b315b2f7b4f35b0f8b0360000"
        "00004948304502210083709c8c25c3be1d3621ceaa496db93ae482c8b1ad5cd1f53b197049fa2e4f26022065943f075c1063eab4cfb899"
        "7dd3b360a297214a7ca47d6d40ac73272a36fd7141feffffff9e110196758aef8af915746f83a310de3c309c6a3efb28ed8be8ef092982"
        "34c500000000494830450221008117b9971a36901ed5c51a6168d8399f795dcf67ca74ff3706c74b39acd3bd2d022058dddaa75b20529c"
        "5c0f87dca42c74ecf3cbec4536df34dbce0862182d0ea5f441feffffff7b77d70109cdee4b4aaf01e6d3a5cb782590ed0a7fa7295ab7bb"
        "e61d0ce865100000000048473044022067409249d8c8cbf8b40a3b36f620903ab0ef7cbb87e88db87d3d5aeb53e7dd81022051d7a5c604"
        "4780cae31ebcc5f545c05a775f29234be2f4c914ab3ab9273e573541feffffff033c75e0e54ee43b195c71800692f3c6b57433766fc2cc"
        "e6e6a5af156cad0c8600000000494830450221009813ec14fe7083f216159b0698c1127813e99ff0a1d8eb392d2e948f74490f8702201d"
        "6ae2419865292fde7a730877ecfc6d00237807fa74eaf37be4ee7b93d35ca241feffffff7adba7b41551e0a3aa0e07d556bc34ea1e8afc"
        "ef8d9ec3bc467560214396c6a40000000048473044022017dbd52182c4b3ae9d33e40438149cc504d584692cd880b310e3ee2d37ed479f"
        "022061ad60be79ec9db4e105c7ed10dce28ca6feb298d2c7111ceb1212d31aabf32a41feffffffaf526a0a35fbbd1b2fb56cb65be70938"
        "f36fe13305aed9666196413412feff33000000004847304402205c2332fa68727112ad7a00f4619d32cd35ff8d3bebbfbcf13c4546dba7"
        "d21d9902207a9635a1ecc07bf8c5d57d5f00984f7c7ce1acf63eb0104d9a5926082a0832aa41feffffff7a04af1b1d32733236f1b1693f"
        "1194a14a32d48cf355ac4d0b6aadbe93e88c4f0000000048473044022051a1f6e579245a264061b2f6f38b7d61ea42f47a1d9620a480ea"
        "b754311599f6022019c5383d6763a58d65b5050f7a12789875217334b83b6856a3aefb3b32eb07b541feffffff782a097ac518ee2516b8"
        "e291721d44bd44e03cc748ecf6b01b916128844951c90000000049483045022100b41a589224e50a2c549c7b1d23a6eb88ddcf32799f69"
        "545a38c8d9ee4169b7ef022007903995675a2c311ee91cf2f1482c060b5b6deeef487650fec91c6bfcb96ea041feffffffd27dcac10bf0"
        "4507d3ef794323a1cb89f05ae04eedb72eacfb9717c73e80f680000000004847304402206d598b7d1e2ca87c503bb660d8f5637274ef1e"
        "bf673b540c550dcab2c66606c4022037d3001eeb4330817c4bec2ab983264f3450e39dbee439f5876c2f2fea94026541fefffffffa0b72"
        "e1cc6440fa1f0334e4689a76fc56475ebfc85b196a5a2ca263648415df0000000048473044022047d218fa521de215e73fa558a728b80e"
        "37e8e064cb178d6d23f96afe0a81d581022073ecfc317862ae1329faa0f4e5dc48e1bff32a631aa923b91c4e5a495aeff39241feffffff"
        "132cd7eba2ebd5436b657936130bd2c82e1a41783618548d0423545a48d3b037000000004847304402207ea90bef8bc0267b70f73e3347"
        "098c2fbfab1c7d36379dedee85eb4f21b58e3402200893d7485c899a280a41adcac98612033673c32875b1aaeb6f7f65f75475b2e741fe"
        "ffffff3c46b7797ec4d5ecffb16d0e4a98b0c9e1a73976d892290b9a4de3d432e8ecef000000004847304402200c4e23723b1604bfc12c"
        "a1fb859c36ebdae07080ae128d58873e8b1c4e109bcb02207cc26021971cf1868eed55b927d5ce56c134dad70a902cda3b70732d108b5e"
        "5a41feffffff3159a5d0f50ae9894a48fad801406e4b8f801b4a14a1d6cf7f9bfbadc3858dcf0000000049483045022100e33e8ee671e1"
        "0e2ca9732aef5e218a2cdf9c84167605237d6a112896c479d4cd02204640ffa120e28971126b02ff2fde8167001c332b442cf363095419"
        "344640a1ec41feffffff7a00b2822f21b60cf95899ac298409ab75f2926def6a1558998c9605ba4594c10000000048473044022011d1f6"
        "86c75c96eaec12590594ca7d80530689f964fdbe31681df02f05b710e402205fd29ea98b137406daa0dadc5eb89406044ef3f06349d12c"
        "34c126ac721b50f941feffffff9a388e105836b0aee33f68020fa2837e182592b4c589ad97aca79e43aa6996f600000000484730440220"
        "17143482c68c81650c8d01e872fa228c9d11bff73a30ab0ece1d5f3a4c6cbf1402203d0455897d1ce7e41d818f2efd99f6d9b53b995d96"
        "1c4bd76321f2ecd679628141feffffffa0c7d55a7ea1c4ea5bf71cf55e14b87976fe1c51d9b8875b79923dc06e618cbf00000000494830"
        "450221009114cf9267c9e6f4d0db6f18446ba8a61f9908e75b2585b8399aa7ab8270dbab022040dd269c0ce2de476a7671bff5d85d9c55"
        "46243262abd63878494cbce1714d4b41feffffff705e0295217dc872d5cf7a0d9dd5e0a359c755256d08b8de6661a00b8b793ff4000000"
        "00484730440220711feea64c6cc66ff1e22f09e9ee632a09470e95a8f9ac620df4da4e8b11d3c60220317f77eb1c6b5fa05213b0386d6e"
        "1653981ed2326037e33e1665d9e7588173e141feffffffd5ea4a003379caffdd2b30f0422c57cfa084a7840f6ed697f93f921bbdaf595e"
        "000000004847304402204c5e91f176f4b0572ac6ecfa489f7a743147dda34e918f25f1615fef6aad92ca0220456cf018f4e292b8e433b6"
        "054b506015895883b3021019923a198146554c461541feffffff05ea9eb4e4c0d931f991bdc2d023d352240e4a1d5036d3abc4bf0d3c22"
        "12284400000000494830450221008d1d460ebe0d4b524b8de9daff2bde3f42b76f95930c7f88799acd9c483f27260220612d207473f3c4"
        "249471cfe20818f00861cd7c8f98a7305f5b1de453a0936e6e41feffffff1312763e97f131a173a4e26b06690a26d24e15c74fd3d0c264"
        "32c423bfae4c9400000000484730440220442cd85ab901994a7e799099ca3586e64f191a599dca30941533de42c5f931bc02206dee4152"
        "0d1933d31ff807727fd33fdcc67de82b32e8b27d9899a3302223364241feffffffd8e2eb93248089e5d98e39a5d2afb526bd1d86771edb"
        "73576a51c7a851ddc0760000000049483045022100ae91085e4e3e0604acde80f782415575519e5c79b51122ae364eaf50b6324ffa0220"
        "76b88d8001bcd5f8985660fa3c1fbd1505162993e4538411d2d62ed6d3c0e16141feffffff20c2703fc3d83dceab8f9539ca35efa5008f"
        "4c9bc6299186023767df9085840100000000494830450221008a9e536ab70e2e71e5f6d32578b80fb0d6a2df59a469729590586101c792"
        "3612022042e672c249a79a05c741d8eaef2fe81ef19e8286aac8b7465fb013b6dfe76b8541feffffff55d7e68d0955d14db571455103ab"
        "39dbb816250517e9c1773c5bb61be9d9cf090000000049483045022100af2f0cae55cdb4128f45208529d4cc817aba7cff818ddf93852f"
        "547c4cfc1dc50220130d2c9ac8709a452396cc13882b0758f19cdfc908c1834e85feef80b33be27f41feffffff18af6ce2a55c7cb2f20b"
        "44e160b7f618c56ffbadffe3029ef7e0c9c97c7ed0790000000049483045022100f97ce785aa808861985dfa84c84668ffd46204d000b8"
        "cd5e08e7808faa7ef056022023667ac72588d7915bcc8e6fad29a27f74fc43d495014439bceed68c5abdefbd41feffffff647af7546553"
        "c5a2775b0a4639f39e6a95d29728cd23925f560a834d61987fad000000004847304402200b294be768663b677125fe87b094f72ec8ffd8"
        "e12c5a796b7d83f682c7a844540220580305793eb8cd4450f7c6153936a48478376b80fd9037d6b954e812b8b8be2441feffffffd3a406"
        "cdf9e6f16694a9fc7e3cd32b3e49bd99a016e0198c471fcded882aba660000000049483045022100bd3da653fe9ee96c54f9aaf83a569a"
        "50659ad422d6337a101c79250621668eaa022010a7641c6c169d89ec8a72a472ef1efe3f124acd5d3cbdbc957b7247f3f8d7c941feffff"
        "ff2098a88eb13da1e047225b64a42098d548ac748889ffa8033bb3d47c76b754690000000049483045022100b969d018645abb33d9de4e"
        "c6241ddd5089de0090135759e7f2fb0a14c4c01a5b02201af0a570f01a9a23da48fdb49e07e442f82c29c0251c44eb7d3ba5e2a97ed9a7"
        "41feffffff815b98efc2e96a9453f559a24ff2564c4e2dca8df273d00db1dbe7beefcccbde00000000484730440220627f682fceda2226"
        "51b9fc61249d0a8a919ae6abd713efc822e771a15e4e458b0220321105d2ae3131a860c3b319846e833a91ed5cdc4b6f82bf26c6f3c15c"
        "b2e07641feffffffd7dc67ea148cf25b5ce305ba3a8384545f1b9a38841ebb13a0b8148e92eb1ff400000000484730440220454c161c0c"
        "b844d7f2f4b29b17fd639417e500935920ae32e294abdfaf659aab0220400fed95e089fb64c05306f641ac66f4dc86f7f7bedda7dab0ee"
        "44f040f5c8c941feffffff98b81a01a6f3c58f92c72c29ae6a0ca09901dfdf85603891ce18ba6ae436529f000000004847304402207e29"
        "4aad56aa54086efe9c0f2f1da14531ee7ff622315a40284e19a609dc16d402203bf4f4ed99b1e8668e5e4ef4e14a66b9df7082ad1220de"
        "9cdc665221c8e1e32741feffffff4a77fd33ef9b85289bc42b4da0decbc66183d1e6caddff24aa68a14127c4470e000000004948304502"
        "2100e04dbc1e56758fc1917f01f5a8f0a4993aaa533d473fdd6c2c511ff3e60501c9022074082494cf77c4bca7515978c4f263ffed69ea"
        "5a39808c38f03773aa9e8e9a4c41feffffff7c57ad94c0888e5428d1a9950e8ad2e5c72991e80c30d09b4de18b67c66617340000000048"
        "4730440220079e01c11097ad2b5900f3895acfd5acf2d70e6687a3a145110e72da5fde741302201f54aa495c3f3e0698c202f299fa5486"
        "ce631db6c77106df5107074368c6a52541feffffffd178e43af60853f06ca1cf7f133d466c2dd82baa1770d238a92df8a2974745e50000"
        "00004948304502210094cd729af19f9a33568882aba7ad8d1b274faeafeef4a062b73a156eabaeec74022075b58d7c52f186630e2f031d"
        "60c50736114fbcb348cce6498d4fdcb7fce3058841feffffff6df556519fef5104111ca2a34922b9d494b02cfaeeac209c7bc18d59f7c6"
        "a53c0000000049483045022100c00e9a4ac9f44c06adfef4031136329b859db4201b9c584d70e10068691a470e022032719b5e9bd4ed44"
        "37ef8979f3d2acf497c95167506e45896a6757a9c3d6646a41feffffff63c8af4f826860a5a2b93fe0fc06eb809ed3c70e2764c9124216"
        "41c2f0df5c9f0000000049483045022100ee82addaa9b64f91f491b420253ff627150c35531fe1064f758edb22bea7681702202f6af52f"
        "c87e9103b77f0abb310d2f065a9dce650e775df4880177666eb5e23e41feffffffae16b3431c098e194b9c8f72a7b1b36193cf517da0b4"
        "a62842a3a33792707d890000000049483045022100d9d1042aef8061c82c32d393308e359eab2eae0c0fdb1f641f7d215451f294620220"
        "06bcfe40b270e2f3dc64b81246584116416df9f3790f370bc6c189e53d006b8b41feffffffc97a18cd78a633828dfeb6ee264006db42e7"
        "aeb785b6c267758465c4dba5c7dd000000004847304402205eff8b7cb9c09ecbf0fa11e1d211d00f45133feb8daea87496662a154fc87b"
        "bf02204a6218b77595eccf820a283a6d4d7a6e2bb0ba0670688d018194237b043d82ea41feffffff5823081df9efb05784336e11758139"
        "6fd2b53ec619473af445cd0ac13ae855710000000048473044022031a8324b3141b34a16d016a5d19a320b3acc1204d07b5d087710e813"
        "22d4280702204972d697ac6af5bd14eed3efb6dbc5534b45ce8047ad4423cf82cd05546b0bc741feffffffdb83f1079499e28873921db8"
        "5a631968f2e2c23d222abfd05ec863e8a713fa580000000049483045022100890af3835ba75ed120587e895d01c91563f62ab97862290d"
        "50028445cf1178550220745c5e8f5bb49f1211c0346f3497f4d6c37e8bcd1bddd10ee8e412d03670941e41feffffff9d10a980857de8fd"
        "6399ab56ba1e393e4998e35947c3a90104c3d59c8c8abab30000000049483045022100974005109f8e743d3bfb75abf11612a4c1f85e8a"
        "dd28e30b9191b6a628cdab4902207da1bcfa6f6093c09510aedb933c8ec67dd02ad7708b9a65cca8ae799e3c9ad641feffffffaecd5fb1"
        "3c827fafe391e130e7165b280b8397dcaa300a5f0029a4fd662aa2ad00000000484730440220543c323e6b3b6bf0e448e014333103cf24"
        "c9a14371f921ed73b7de3a620d326002205d74b73cc40eee92d028a1c01c2bdfe925b4ecd72e450031a0247f7769ce3eb541feffffffc3"
        "ac9458d032c9cc90b3d18921e9c155838a7edd5b13fd8b10d0fd215a3c570c0000000048473044022043153e9c564bc70acae8ca7be5d2"
        "6c37069aa833ca5e09dd9ee3a466b49c983f022047352e89a855a065eb1b78644360877a405907ddae097433adf1e9d621f33acd41feff"
        "ffffd31f7426aafb19021d95c4e2534c3f94eec6010a3cd57c00ff509cb3fa9760e50000000049483045022100cd9c027b830a949fcf40"
        "0aa8edf74ab8ae2ee0c695d0b2a944e1d2061210142402201ec9b168090cf25fb09f5a2e748643989ab5c4dff6f1e42fb1dc6e4a463aff"
        "ca41feffffff8c4890ea01a4f25bfd30a5c5a5961f1bfba855c1e9ad844005fee5e180a5057b0000000049483045022100d7652bd07d10"
        "6f596cd9e7911a7d828bdc2bac4d944edcf79dfbbd2d4f1a249e022076dfe799220a1f18acf1d5330cd37fbd89b44a91ef5972ca90824c"
        "3c002f997041feffffff8f777d729d07f1a4c597f73637c189db78366060a3560f806f8583919065b4620000000049483045022100cc15"
        "417a0a72f8a1d29e5a494c571686adaa5ed1930a894534ff5a256bb2625d022031117c0ac269c20be4566e4799c597799422092e4633f4"
        "298a4d322fbce20ae341feffffffb5bb82b9927c25680bab5bd458a1412df582893526733a48c1e6dc7d4a7bc068000000004847304402"
        "2010bd41eaca00d6118be311a5e80edd837d918d49d9f2e20cd4c5e1a9f5f4b9e9022058bf054561175e5d9dfe8848114c9c6430d733e1"
        "be0bc00269255b6c9799524f41fefffffff04a5ea34b811937a199af432fcad89c9dd4d39e364b21fb133a1fc6e72d7395000000004847"
        "304402202a318f593632dcf7f74dd55f65fd4070608ef8a55d3c41f4dae9125a982e3661022050d5c727695c5fa3af45aa641bfa2ff004"
        "2820ec4217b222a9bf5ebf8e9704de41feffffff4c82ade499352d04f6df523adc67142b8a67380710f1ac6895fd7eea84e1f9df000000"
        "004948304502210098af9ac054dcd97730e8b470c8b5d0b4d58e4d1cd927625b8e9f37c01888f8fb02201b297f7b2b4b76bce2f6d9a604"
        "50ff6367950503d4b6d07259f46a82ea9b249f41feffffffdadb6fb6f058e15373c8f7a044a4014104eaec370053efd94a40b99b7f1498"
        "730000000049483045022100bbcf75f2877f232082db24ca56f2872eb17648895517a66a79c495c3a4f2cd6e0220132cf75aa84eb654a6"
        "f720905dac3a5eddd32a0364a42fc97fcadf8802dbc06541feffffff1e4fc70faee37f2242361c9f0063fd52d2b10f1a7b87989f3f7319"
        "f1bfa02729000000004847304402207eaa731d48a5caf584817c6f90106075a8be73b9ac45bdf243418343e0afb269022034fb05f91ed5"
        "650aa86caceec0be9ed22f91b17ea68e3a961b4e755c3834d88f41feffffffdabd8dd93c2ba0107db8f290d965762fbee44348edafc2cb"
        "4f141bb714eeb0af0000000049483045022100cff6f35dc6f23e127b0dece0b77adbc80281741b47c20cca5bace5409f69f2a902200c35"
        "df1e9a3605a9e3db6eac60da16b9bec13e417392c484c210a58e260f6cde41feffffff645fe11fcbb57bbca3efe8969c77bc8dae71eb11"
        "141c8ffd9eccf5d270530d1f00000000494830450221008fe586e72a4d583ef95b0db6e14b19cbb1a6086a16670f2e3a6841621ff0de34"
        "02201e0183775100a8ce84c60d330c126a097de8d27e93cfe371f6dcb8405eabf38d41feffffff9ef1c7fe9a94af588b5666d706da049b"
        "ff25d320571fa6685f04d1442a5b91f600000000484730440220730d988ed3519334ef695bb5de4c8e14127a4f85f624d34fef9cdb1a22"
        "35f284022003105c63575bd3ff64c96448d3f51ba52908979dfbe17aa38ad169d2b654fc7a41feffffff16e1dd7d0a705346e9db9a1be6"
        "ba67f413da78a82f542b80a4fe78fa344da01b0000000049483045022100a49a9c8f3c1364b1c09106065ebc3b4fa80a32406d00e9ae8f"
        "adc49e1b24db15022058a2b887f1c849a69279421fef8e73fd729fa298ff151ad86eb87501fb60226341feffffff1f2835d7f4d71e3478"
        "a3d0e5b3c18c146395ebab0185e96bdb531d71448229170000000049483045022100aa50c57572240252504c7e9c0d8bb716ae07e3d32a"
        "c55e7fc4a9dee6a86ed5ae0220699d5ed5982993d4c190690eee67b692ee8853bd269de1f2029a9e4d6c1a8edb41feffffffe8a30317f7"
        "e52b18e49a77af123f577e47b21235681e3d3ececc5463fe7ae785000000004948304502210085307c266314b2f0913309e30b0ddf3e8c"
        "cbed069e23c9f5fbc382a55466011a022068b7a997d5f6cdce4c7087a004761e899a348faea92a453c5f013848d204646f41feffffff02"
        "00c51967910000001976a914f70405ef45adf839cef1ebe4530b217d94a882ac88ac492dcd1d000000001976a914dedd082c7fad871394"
        "88a8b1ba784cb71c495a3d88ace1000000";

    CTransaction tx;
    BOOST_CHECK(DecodeHexTx(tx, raw_tx) == true);

    CValidationState state;
    BOOST_CHECK_MESSAGE(CheckTransaction(MakeTransactionRef(tx), state) && state.IsValid(),
        "Simple deserialized transaction should be valid.");

    // Check that duplicate txins fail
    CMutableTransaction mtx(tx);
    mtx.vin.push_back(tx.vin[0]);
    BOOST_CHECK_MESSAGE(!CheckTransaction(MakeTransactionRef(CTransaction(mtx)), state) || !state.IsValid(),
        "Transaction with duplicate txins should be invalid.");
}

BOOST_AUTO_TEST_SUITE_END()
