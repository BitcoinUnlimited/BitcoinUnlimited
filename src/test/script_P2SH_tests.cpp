// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/tx_verify.h"
#include "key.h"
#include "keystore.h"
#include "policy/policy.h"
#include "script/ismine.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "test/test_bitcoin.h"
#include "validation/parallel.h"

#include <vector>

#include <boost/test/unit_test.hpp>

using namespace std;

// Helpers:
static std::vector<unsigned char> Serialize(const CScript &s)
{
    std::vector<unsigned char> sSerialized(s.begin(), s.end());
    return sSerialized;
}

static bool Verify(const CScript &scriptSig, const CScript &scriptPubKey, bool fStrict, ScriptError &err, bool fP2SH32)
{
    // Create dummy to/from transactions:
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;

    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vin[0].scriptSig = scriptSig;
    txTo.vout[0].nValue = 1;

    MutableTransactionSignatureChecker tsc(&txTo, 0, txFrom.vout[0].nValue);

    uint32_t flags = fStrict ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;
    if (fStrict && fP2SH32)
    {
        flags |= SCRIPT_ENABLE_P2SH_32;
    }
    ScriptImportedState sis(&tsc, MakeTransactionRef(txTo), std::vector<CTxOut>(), 0, txFrom.vout[0].nValue, flags);

    return VerifyScript(scriptSig, scriptPubKey, flags, MAX_OPS_PER_SCRIPT, sis, &err);
}


BOOST_FIXTURE_TEST_SUITE(script_P2SH_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(sign)
{
    for (const bool is_p2sh_32 : {false, true})
    {
        const uint32_t flags = is_p2sh_32 ? STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENABLE_P2SH_32 :
                                            STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_ENABLE_P2SH_32;

        LOCK(cs_main);
        // Pay-to-script-hash looks like this:
        // scriptSig:    <sig> <sig...> <serialized_script>
        // scriptPubKey: HASH160 <hash> EQUAL

        // Test SignSignature() (and therefore the version of Solver() that signs transactions)
        CBasicKeyStore keystore;
        CKey key[4];
        for (int i = 0; i < 4; i++)
        {
            key[i].MakeNewKey(true);
            keystore.AddKey(key[i]);
        }

        // 8 Scripts: checking all combinations of
        // different keys, straight/P2SH, pubkey/pubkeyhash
        CScript standardScripts[4];
        standardScripts[0] << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
        standardScripts[1] = GetScriptForDestination(key[1].GetPubKey().GetID());
        standardScripts[2] << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
        standardScripts[3] = GetScriptForDestination(key[2].GetPubKey().GetID());
        CScript evalScripts[4];
        for (int i = 0; i < 4; i++)
        {
            keystore.AddCScript(standardScripts[i], is_p2sh_32);
            evalScripts[i] = GetScriptForDestination(ScriptID(standardScripts[i], is_p2sh_32));
        }

        CMutableTransaction txFrom; // Funding transaction:
        string reason;
        txFrom.vout.resize(8);
        for (int i = 0; i < 4; i++)
        {
            txFrom.vout[i].scriptPubKey = evalScripts[i];
            txFrom.vout[i].nValue = COIN;
            txFrom.vout[i + 4].scriptPubKey = standardScripts[i];
            txFrom.vout[i + 4].nValue = COIN;
        }
        BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(txFrom)), reason, flags));

        CMutableTransaction txTo[8]; // Spending transactions
        for (int i = 0; i < 8; i++)
        {
            txTo[i].vin.resize(1);
            txTo[i].vout.resize(1);
            txTo[i].vin[0].prevout.n = i;
            txTo[i].vin[0].prevout.hash = txFrom.GetHash();
            txTo[i].vout[0].nValue = 1;
#ifdef ENABLE_WALLET
            BOOST_CHECK_MESSAGE(IsMine(keystore, txFrom.vout[i].scriptPubKey, 0), strprintf("IsMine %d", i));
#endif
        }
        for (int i = 0; i < 8; i++)
        {
            BOOST_CHECK_MESSAGE(SignSignature(flags, keystore, txFrom, txTo[i], 0),
                strprintf("SignSignature %d p2sh32: %d flags: %d", i, is_p2sh_32, flags));
        }
        // All of the above should be OK, and the txTos have valid signatures
        // Check to make sure signature verification fails if we use the wrong ScriptSig:
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 8; j++)
            {
                CScript sigSave = txTo[i].vin[0].scriptSig;
                txTo[i].vin[0].scriptSig = txTo[j].vin[0].scriptSig;

                const CTxOut &output = txFrom.vout[txTo[i].vin[0].prevout.n];
                const uint32_t flagsCheck = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC |
                                            SCRIPT_ENABLE_SIGHASH_FORKID | (is_p2sh_32 ? SCRIPT_ENABLE_P2SH_32 : 0);
                bool sigOK = CScriptCheck(nullptr, output.scriptPubKey, output.nValue, txTo[i], std::vector<CTxOut>(),
                    0, flagsCheck, MAX_OPS_PER_SCRIPT, false)();
                if (i == j)
                    BOOST_CHECK_MESSAGE(sigOK, strprintf("VerifySignature %d %d", i, j));
                else
                    BOOST_CHECK_MESSAGE(!sigOK, strprintf("VerifySignature %d %d", i, j));
                txTo[i].vin[0].scriptSig = sigSave;
            }
    }
}

BOOST_AUTO_TEST_CASE(norecurse)
{
    // This tests p2sh_20 and p2sh_32 as well.
    for (const bool is_p2sh_32 : {false, true})
    {
        ScriptError err;
        // Make sure only the outer pay-to-script-hash does the
        // extra-validation thing:
        CScript invalidAsScript;
        invalidAsScript << OP_INVALIDOPCODE << OP_INVALIDOPCODE;

        CScript p2sh = GetScriptForDestination(ScriptID(invalidAsScript, is_p2sh_32));

        CScript scriptSig;
        scriptSig << Serialize(invalidAsScript);

        // Should not verify, because it will try to execute OP_INVALIDOPCODE
        BOOST_CHECK(!Verify(scriptSig, p2sh, true, err, is_p2sh_32));
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_BAD_OPCODE, ScriptErrorString(err));

        // Try to recur, and verification should succeed because
        // the inner HASH160 <> EQUAL should only check the hash:
        CScript p2sh2 = GetScriptForDestination(ScriptID(p2sh, is_p2sh_32));
        CScript scriptSig2;
        scriptSig2 << Serialize(invalidAsScript) << Serialize(p2sh);

        BOOST_CHECK(Verify(scriptSig2, p2sh2, true, err, is_p2sh_32));
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    }
}

BOOST_AUTO_TEST_CASE(set)
{
    // This tests p2sh_20 and p2sh_32 as well.
    for (const bool is_p2sh_32 : {false, true})
    {
        const uint32_t flags = is_p2sh_32 ? STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENABLE_P2SH_32 :
                                            STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_ENABLE_P2SH_32;
        LOCK(cs_main);
        // Test the CScript::Set* methods
        CBasicKeyStore keystore;
        CKey key[4];
        std::vector<CPubKey> keys;
        for (int i = 0; i < 4; i++)
        {
            key[i].MakeNewKey(true);
            keystore.AddKey(key[i]);
            keys.push_back(key[i].GetPubKey());
        }

        CScript inner[4];
        inner[0] = GetScriptForDestination(key[0].GetPubKey().GetID());
        inner[1] = GetScriptForMultisig(2, std::vector<CPubKey>(keys.begin(), keys.begin() + 2));
        inner[2] = GetScriptForMultisig(1, std::vector<CPubKey>(keys.begin(), keys.begin() + 2));
        inner[3] = GetScriptForMultisig(2, std::vector<CPubKey>(keys.begin(), keys.begin() + 3));

        CScript outer[4];
        for (int i = 0; i < 4; i++)
        {
            outer[i] = GetScriptForDestination(ScriptID(inner[i], is_p2sh_32));
            keystore.AddCScript(inner[i], is_p2sh_32);
        }

        CMutableTransaction txFrom; // Funding transaction:
        string reason;
        txFrom.vout.resize(4);
        for (int i = 0; i < 4; i++)
        {
            txFrom.vout[i].scriptPubKey = outer[i];
            txFrom.vout[i].nValue = CENT;
        }
        BOOST_CHECK(IsStandardTx(MakeTransactionRef(CTransaction(txFrom)), reason, flags));

        CMutableTransaction txTo[4]; // Spending transactions
        for (int i = 0; i < 4; i++)
        {
            txTo[i].vin.resize(1);
            txTo[i].vout.resize(1);
            txTo[i].vin[0].prevout.n = i;
            txTo[i].vin[0].prevout.hash = txFrom.GetHash();
            txTo[i].vout[0].nValue = 1 * CENT;
            txTo[i].vout[0].scriptPubKey = inner[i];
#ifdef ENABLE_WALLET
            BOOST_CHECK_MESSAGE(IsMine(keystore, txFrom.vout[i].scriptPubKey, 0), strprintf("IsMine %d", i));
#endif
        }
        for (int i = 0; i < 4; i++)
        {
            BOOST_CHECK_MESSAGE(SignSignature(flags, keystore, txFrom, txTo[i], 0), strprintf("SignSignature %d", i));
            BOOST_CHECK_MESSAGE(IsStandardTx(MakeTransactionRef(CTransaction(txTo[i])), reason, flags),
                strprintf("txTo[%d].IsStandard", i));
            BOOST_CHECK_MESSAGE(IsStandardTx(MakeTransactionRef(CTransaction(txTo[i])), reason, flags),
                strprintf("txTo[%d].IsStandard", i));
        }
    }
}

BOOST_AUTO_TEST_CASE(is)
{
    // This tests p2sh_20 and p2sh_32 as well.
    for (const bool is_p2sh_32 : {false, true})
    {
        const uint32_t flags = is_p2sh_32 ? STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENABLE_P2SH_32 :
                                            STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_ENABLE_P2SH_32;
        // Test CScript::IsPayToScriptHash()
        uint160 dummy;
        CScript p2sh;
        p2sh << OP_HASH160 << ToByteVector(dummy) << OP_EQUAL;
        BOOST_CHECK(p2sh.IsPayToScriptHash(flags));

        // Not considered pay-to-script-hash if using one of the OP_PUSHDATA opcodes:
        static const unsigned char direct[] = {
            OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL};
        BOOST_CHECK(CScript(direct, direct + sizeof(direct)).IsPayToScriptHash(flags));
        static const unsigned char pushdata1[] = {
            OP_HASH160, OP_PUSHDATA1, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL};
        static const uint8_t pushdata1_32[] = {OP_HASH160, OP_PUSHDATA1, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL};
        BOOST_CHECK(!CScript(pushdata1, pushdata1 + sizeof(pushdata1)).IsPayToScriptHash(flags));
        BOOST_CHECK(!CScript(pushdata1_32, pushdata1_32 + sizeof(pushdata1_32)).IsPayToScriptHash(flags));
        static const unsigned char pushdata2[] = {
            OP_HASH160, OP_PUSHDATA2, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL};
        static const uint8_t pushdata2_32[] = {OP_HASH160, OP_PUSHDATA2, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL};
        BOOST_CHECK(!CScript(pushdata2, pushdata2 + sizeof(pushdata2)).IsPayToScriptHash(flags));
        BOOST_CHECK(!CScript(pushdata2_32, pushdata2_32 + sizeof(pushdata2_32)).IsPayToScriptHash(flags));
        static const unsigned char pushdata4[] = {OP_HASH160, OP_PUSHDATA4, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL};
        static const uint8_t pushdata4_32[] = {OP_HASH160, OP_PUSHDATA4, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUAL};
        BOOST_CHECK(!CScript(pushdata4, pushdata4 + sizeof(pushdata4)).IsPayToScriptHash(flags));
        BOOST_CHECK(!CScript(pushdata4_32, pushdata4_32 + sizeof(pushdata4_32)).IsPayToScriptHash(flags));

        CScript not_p2sh;
        BOOST_CHECK(!not_p2sh.IsPayToScriptHash(flags));

        not_p2sh.clear();
        not_p2sh << OP_HASH160 << ToByteVector(dummy) << ToByteVector(dummy) << OP_EQUAL;
        BOOST_CHECK(!not_p2sh.IsPayToScriptHash(flags));
        not_p2sh.clear();
        not_p2sh << OP_HASH256 << ToByteVector(dummy) << ToByteVector(dummy) << OP_EQUAL;
        BOOST_CHECK(!not_p2sh.IsPayToScriptHash(flags));

        not_p2sh.clear();
        not_p2sh << OP_NOP << ToByteVector(dummy) << OP_EQUAL;
        BOOST_CHECK(!not_p2sh.IsPayToScriptHash(flags));

        not_p2sh.clear();
        not_p2sh << OP_HASH160 << ToByteVector(dummy) << OP_CHECKSIG;
        BOOST_CHECK(!not_p2sh.IsPayToScriptHash(flags));

        not_p2sh.clear();
        not_p2sh << OP_HASH256 << ToByteVector(dummy) << OP_CHECKSIG;
        BOOST_CHECK(!not_p2sh.IsPayToScriptHash(flags));
    }
}

BOOST_AUTO_TEST_CASE(switchover)
{
    // This tests p2sh_20 and p2sh_32 as well.
    for (const bool is_p2sh_32 : {false, true})
    {
        // Test switch over code
        CScript notValid;
        ScriptError err;
        notValid << OP_11 << OP_12 << OP_EQUALVERIFY;
        CScript scriptSig;
        scriptSig << Serialize(notValid);

        CScript fund = GetScriptForDestination(ScriptID(notValid, is_p2sh_32));


        // Validation should succeed under old rules (hash is correct):
        BOOST_CHECK(Verify(scriptSig, fund, false, err, is_p2sh_32));
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
        // Fail under new:
        BOOST_CHECK(!Verify(scriptSig, fund, true, err, is_p2sh_32));
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EQUALVERIFY, ScriptErrorString(err));
    }
}

BOOST_AUTO_TEST_CASE(AreInputsStandard)
{
    // This tests p2sh_20 and p2sh_32 as well.
    for (const bool is_p2sh_32 : {false, true})
    {
        const uint32_t flags = is_p2sh_32 ? STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENABLE_P2SH_32 :
                                            STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_ENABLE_P2SH_32;
        LOCK(cs_main);
        CCoinsView coinsDummy;
        CCoinsViewCache coins(&coinsDummy);
        CBasicKeyStore keystore;
        CKey key[6];
        vector<CPubKey> keys;
        for (int i = 0; i < 6; i++)
        {
            key[i].MakeNewKey(true);
            keystore.AddKey(key[i]);
        }
        for (int i = 0; i < 3; i++)
            keys.push_back(key[i].GetPubKey());

        CMutableTransaction txFrom;
        txFrom.vout.resize(7);

        // First three are standard:
        CScript pay1 = GetScriptForDestination(key[0].GetPubKey().GetID());
        keystore.AddCScript(pay1, is_p2sh_32);
        CScript pay1of3 = GetScriptForMultisig(1, keys);

        txFrom.vout[0].scriptPubKey = GetScriptForDestination(ScriptID(pay1, is_p2sh_32)); // P2SH (OP_CHECKSIG)
        txFrom.vout[0].nValue = 1000;
        txFrom.vout[1].scriptPubKey = pay1; // ordinary OP_CHECKSIG
        txFrom.vout[1].nValue = 2000;
        txFrom.vout[2].scriptPubKey = pay1of3; // ordinary OP_CHECKMULTISIG
        txFrom.vout[2].nValue = 3000;

        // vout[3] is complicated 1-of-3 AND 2-of-3
        // ... that is OK if wrapped in P2SH:
        CScript oneAndTwo;
        oneAndTwo << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey())
                  << ToByteVector(key[2].GetPubKey());
        oneAndTwo << OP_3 << OP_CHECKMULTISIGVERIFY;
        oneAndTwo << OP_2 << ToByteVector(key[3].GetPubKey()) << ToByteVector(key[4].GetPubKey())
                  << ToByteVector(key[5].GetPubKey());
        oneAndTwo << OP_3 << OP_CHECKMULTISIG;
        keystore.AddCScript(oneAndTwo, is_p2sh_32);
        txFrom.vout[3].scriptPubKey = GetScriptForDestination(ScriptID(oneAndTwo, is_p2sh_32));
        txFrom.vout[3].nValue = 4000;

        // vout[4] is max sigops:
        CScript fifteenSigops;
        fifteenSigops << OP_1;
        for (unsigned i = 0; i < MAX_P2SH_SIGOPS; i++)
            fifteenSigops << ToByteVector(key[i % 3].GetPubKey());
        fifteenSigops << OP_15 << OP_CHECKMULTISIG;
        keystore.AddCScript(fifteenSigops, is_p2sh_32);
        txFrom.vout[4].scriptPubKey = GetScriptForDestination(ScriptID(fifteenSigops, is_p2sh_32));
        txFrom.vout[4].nValue = 5000;

        // vout[5/6] are non-standard because they exceed MAX_P2SH_SIGOPS
        CScript sixteenSigops;
        sixteenSigops << OP_16 << OP_CHECKMULTISIG;
        keystore.AddCScript(sixteenSigops, is_p2sh_32);
        txFrom.vout[5].scriptPubKey = GetScriptForDestination(ScriptID(fifteenSigops, is_p2sh_32));
        txFrom.vout[5].nValue = 5000;
        CScript twentySigops;
        twentySigops << OP_CHECKMULTISIG;
        keystore.AddCScript(twentySigops, is_p2sh_32);
        txFrom.vout[6].scriptPubKey = GetScriptForDestination(ScriptID(twentySigops, is_p2sh_32));
        txFrom.vout[6].nValue = 6000;

        AddCoins(coins, txFrom, 0);

        CMutableTransaction txTo;
        txTo.vout.resize(1);
        txTo.vout[0].scriptPubKey = GetScriptForDestination(key[1].GetPubKey().GetID());

        txTo.vin.resize(5);
        for (int i = 0; i < 5; i++)
        {
            txTo.vin[i].prevout.n = i;
            txTo.vin[i].prevout.hash = txFrom.GetHash();
        }
        BOOST_CHECK(SignSignature(flags, keystore, txFrom, txTo, 0));
        BOOST_CHECK(SignSignature(flags, keystore, txFrom, txTo, 1));
        BOOST_CHECK(SignSignature(flags, keystore, txFrom, txTo, 2));
        // SignSignature doesn't know how to sign these. We're
        // not testing validating signatures, so just create
        // dummy signatures that DO include the correct P2SH scripts:
        txTo.vin[3].scriptSig << OP_11 << OP_11 << vector<unsigned char>(oneAndTwo.begin(), oneAndTwo.end());
        txTo.vin[4].scriptSig << vector<unsigned char>(fifteenSigops.begin(), fifteenSigops.end());

        BOOST_CHECK(::AreInputsStandard(MakeTransactionRef(CTransaction(txTo)), coins, false, flags));
        BOOST_CHECK(::AreInputsStandard(MakeTransactionRef(CTransaction(txTo)), coins, true, flags));
        // 22 P2SH sigops for all inputs (1 for vin[0], 6 for vin[3], 15 for vin[4]
        BOOST_CHECK_EQUAL(GetP2SHSigOpCount(MakeTransactionRef(CTransaction(txTo)), coins, flags), 22U);
        // Check that no sigops show up when P2SH is not activated.
        BOOST_CHECK_EQUAL(GetP2SHSigOpCount(MakeTransactionRef(CTransaction(txTo)), coins, SCRIPT_VERIFY_NONE), 0U);

        CMutableTransaction txToNonStd1;
        txToNonStd1.vout.resize(1);
        txToNonStd1.vout[0].scriptPubKey = GetScriptForDestination(key[1].GetPubKey().GetID());
        txToNonStd1.vout[0].nValue = 1000;
        txToNonStd1.vin.resize(1);
        txToNonStd1.vin[0].prevout.n = 5;
        txToNonStd1.vin[0].prevout.hash = txFrom.GetHash();
        txToNonStd1.vin[0].scriptSig << vector<unsigned char>(sixteenSigops.begin(), sixteenSigops.end());

        BOOST_CHECK(!::AreInputsStandard(MakeTransactionRef(CTransaction(txToNonStd1)), coins, false, flags));
        BOOST_CHECK(::AreInputsStandard(MakeTransactionRef(CTransaction(txToNonStd1)), coins, true, flags));
        BOOST_CHECK_EQUAL(GetP2SHSigOpCount(MakeTransactionRef(CTransaction(txToNonStd1)), coins, flags), 16U);
        // Check that no sigops show up when P2SH is not activated.
        BOOST_CHECK_EQUAL(
            GetP2SHSigOpCount(MakeTransactionRef(CTransaction(txToNonStd1)), coins, SCRIPT_VERIFY_NONE), 0U);

        CMutableTransaction txToNonStd2;
        txToNonStd2.vout.resize(1);
        txToNonStd2.vout[0].scriptPubKey = GetScriptForDestination(key[1].GetPubKey().GetID());
        txToNonStd2.vout[0].nValue = 1000;
        txToNonStd2.vin.resize(1);
        txToNonStd2.vin[0].prevout.n = 6;
        txToNonStd2.vin[0].prevout.hash = txFrom.GetHash();
        txToNonStd2.vin[0].scriptSig << vector<unsigned char>(twentySigops.begin(), twentySigops.end());

        BOOST_CHECK(!::AreInputsStandard(MakeTransactionRef(CTransaction(txToNonStd2)), coins, false, flags));
        BOOST_CHECK(::AreInputsStandard(MakeTransactionRef(CTransaction(txToNonStd2)), coins, true, flags));
        BOOST_CHECK_EQUAL(GetP2SHSigOpCount(MakeTransactionRef(CTransaction(txToNonStd2)), coins, flags), 20U);
        // Check that no sigops show up when P2SH is not activated.
        BOOST_CHECK_EQUAL(
            GetP2SHSigOpCount(MakeTransactionRef(CTransaction(txToNonStd2)), coins, SCRIPT_VERIFY_NONE), 0U);
    }
}

BOOST_AUTO_TEST_SUITE_END()
