// Copyright (c) 2021 The Bitcoin developers
// Copyright (c) 2021 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"
#include "core_io.h"
#include "policy/policy.h"
#include "script/interpreter.h"
#include "script/script.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(native_introspection_tests, BasicTestingSetup)
using valtype = std::vector<uint8_t>;
using stacktype = std::vector<valtype>;

static std::vector<ScriptImportedState> createForAllInputs(CTransactionRef tx,
    const CCoinsViewCache &coinsCache,
    BaseSignatureChecker &bsc)
{
    std::vector<ScriptImportedState> ret;
    ret.reserve(tx->vin.size());
    for (size_t i = 0; i < tx->vin.size(); ++i)
    {
        CAmount amnt = 0;
        std::vector<CTxOut> coins;
        size_t k = 0;
        for (const auto &txin : tx->vin)
        {
            CoinAccessor coin(coinsCache, txin.prevout);
            coins.push_back(coin->out); // If already spent coin->out will be -1 value and an empty script
            if (i == k)
            {
                amnt = coin->out.nValue;
            }
            k = k + 1;
        }
        // private c'tor, must use push_back
        ret.push_back(ScriptImportedState(&bsc, tx, coins, i, amnt, STANDARD_SCRIPT_VERIFY_FLAGS));
    }
    return ret;
}


static void CheckErrorWithFlags(uint32_t flags,
    const stacktype &original_stack,
    const CScript &script,
    const ScriptImportedState &sis,
    ScriptError expected)
{
    ScriptError err = SCRIPT_ERR_OK;
    stacktype stack{original_stack};
    bool r = EvalScript(stack, script, flags, MAX_OPS_PER_SCRIPT, sis, &err);
    BOOST_CHECK(!r);
    BOOST_CHECK(err == expected);
}

static void CheckPassWithFlags(uint32_t flags,
    const stacktype &original_stack,
    const CScript &script,
    const ScriptImportedState &sis,
    const stacktype &expected)
{
    ScriptError err = SCRIPT_ERR_OK;
    stacktype stack{original_stack};
    bool r = EvalScript(stack, script, flags, MAX_OPS_PER_SCRIPT, sis, &err);
    BOOST_CHECK(r);
    BOOST_CHECK(err == SCRIPT_ERR_OK);
    BOOST_CHECK(stack == expected);
}

BOOST_AUTO_TEST_CASE(opcodes_basic)
{
    const uint32_t flags = MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_NATIVE_INTROSPECTION;
    const uint32_t flags_inactive = flags & ~SCRIPT_NATIVE_INTROSPECTION;

    CCoinsView dummy;
    CCoinsViewCache coins(&dummy);
    const COutPoint in1(uint256S("be89ae9569526343105994a950775869a910f450d337a6c29d43a37f093b662f"), 5);
    const COutPoint in2(uint256S("08d5fc002b094fced39381b7e9fa15fb8c944164e48262a2c0b8edef9866b348"), 7);
    const CAmount val1(2000);
    const CAmount val2(3000);
    const CScript coinScriptPubKey1 = CScript() << 2 << OP_ADD << 0 << OP_GREATERTHAN;
    const CScript coinScriptPubKey2 = CScript() << 3 << OP_ADD << 0 << OP_GREATERTHAN;

    coins.AddCoin(in1, Coin(CTxOut(val1, coinScriptPubKey1), 1, false), false);
    coins.AddCoin(in2, Coin(CTxOut(val2, coinScriptPubKey2), 1, false), false);

    CMutableTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout = in1;
    tx.vin[0].scriptSig = CScript() << OP_0;
    tx.vin[0].nSequence = 0x010203;
    tx.vin[1].prevout = in2;
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].nSequence = 0xbeeff00d;
    tx.vout.resize(3);
    tx.vout[0].nValue = 1000;
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    tx.vout[1].nValue = 1900;
    tx.vout[1].scriptPubKey = CScript() << OP_3;
    tx.vout[2].nValue = 2100;
    tx.vout[2].scriptPubKey = CScript() << OP_4;
    tx.nVersion = 101;
    tx.nLockTime = 10;
    FalseSignatureChecker bsc;
    CTransactionRef txref = MakeTransactionRef(tx);
    const auto context = createForAllInputs(txref, coins, bsc);
    BOOST_CHECK(context.size() == tx.vin.size());

    // OP_INPUTINDEX (nullary)
    {
        const valtype expected0(CScriptNum::fromIntUnchecked(0).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_INPUTINDEX, context[0], {expected0});

        const valtype expected1(CScriptNum::fromIntUnchecked(1).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_INPUTINDEX, context[1], {expected1});

        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_INPUTINDEX, {}, SCRIPT_ERR_DATA_REQUIRED);

        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, CScript() << OP_INPUTINDEX, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_ACTIVEBYTECODE (nullary)
    {
        const auto bytecode0 = CScript() << OP_ACTIVEBYTECODE << OP_9;
        const auto bytecode1 = CScript() << OP_ACTIVEBYTECODE << OP_10;
        auto const bytecode2 = CScript() << OP_10 << OP_11 << 7654321 << OP_CODESEPARATOR << 123123 << OP_DROP
                                         << OP_ACTIVEBYTECODE << OP_CODESEPARATOR << OP_1;
        auto const bytecode2b = CScript() << 123123 << OP_DROP << OP_ACTIVEBYTECODE << OP_CODESEPARATOR << OP_1;

        const valtype expected0(bytecode0.begin(), bytecode0.end());
        CheckPassWithFlags(flags, {}, bytecode0, context[0], {expected0, CScriptNum::fromIntUnchecked(9).getvch()});

        const valtype expected1(bytecode1.begin(), bytecode1.end());
        CheckPassWithFlags(flags, {}, bytecode1, context[0], {expected1, CScriptNum::fromIntUnchecked(10).getvch()});

        // check that OP_CODESEPARATOR is respected properly
        valtype const expected2(bytecode2b.begin(), bytecode2b.end());
        CheckPassWithFlags(flags, {}, bytecode2, context[0],
            {CScriptNum::fromIntUnchecked(10).getvch(), CScriptNum::fromIntUnchecked(11).getvch(),
                CScriptNum::fromIntUnchecked(7654321).getvch(), expected2, CScriptNum::fromIntUnchecked(1).getvch()});


        // failure (no context)
        CheckErrorWithFlags(flags, {}, bytecode1, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, bytecode1, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_TXVERSION (nullary)
    {
        const valtype expected(CScriptNum::fromIntUnchecked(tx.nVersion).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_TXVERSION, context[0], {expected});
        CheckPassWithFlags(flags, {}, CScript() << OP_TXVERSION, context[1], {expected});

        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_TXVERSION, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, CScript() << OP_TXVERSION, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_TXINPUTCOUNT (nullary)
    {
        const valtype expected(CScriptNum::fromIntUnchecked(tx.vin.size()).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_TXINPUTCOUNT, context[0], {expected});
        CheckPassWithFlags(flags, {}, CScript() << OP_TXINPUTCOUNT, context[1], {expected});

        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_TXINPUTCOUNT, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, CScript() << OP_TXINPUTCOUNT, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_TXOUTPUTCOUNT (nullary)
    {
        const valtype expected(CScriptNum::fromIntUnchecked(tx.vout.size()).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_TXOUTPUTCOUNT, context[0], {expected});
        CheckPassWithFlags(flags, {}, CScript() << OP_TXOUTPUTCOUNT, context[1], {expected});

        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_TXOUTPUTCOUNT, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, CScript() << OP_TXOUTPUTCOUNT, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_TXLOCKTIME (nullary)
    {
        const valtype expected(CScriptNum::fromIntUnchecked(tx.nLockTime).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_TXLOCKTIME, context[0], {expected});
        CheckPassWithFlags(flags, {}, CScript() << OP_TXLOCKTIME, context[1], {expected});

        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_TXLOCKTIME, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, CScript() << OP_TXLOCKTIME, context[0], SCRIPT_ERR_BAD_OPCODE);
    }


    // OP_UTXOVALUE (unary)
    {
        const valtype expected0(CScriptNum::fromIntUnchecked(val1).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_UTXOVALUE, context[0], {expected0});
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_UTXOVALUE, context[1], {expected0});

        const valtype expected1(CScriptNum::fromIntUnchecked(val2).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_UTXOVALUE, context[0], {expected1});
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_UTXOVALUE, context[1], {expected1});

        // failure (missing arg)
        CheckErrorWithFlags(flags, {}, CScript() << OP_UTXOVALUE, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_2 << OP_UTXOVALUE, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        CheckErrorWithFlags(flags, {}, CScript() << -1 << OP_UTXOVALUE, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_UTXOVALUE, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, CScript() << OP_0 << OP_UTXOVALUE, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_UTXOBYTECODE (unary)
    {
        const valtype expected0(coinScriptPubKey1.begin(), coinScriptPubKey1.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_UTXOBYTECODE, context[0], {expected0});
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_UTXOBYTECODE, context[1], {expected0});

        const valtype expected1(coinScriptPubKey2.begin(), coinScriptPubKey2.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_UTXOBYTECODE, context[0], {expected1});
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_UTXOBYTECODE, context[1], {expected1});

        // failure (missing arg)
        CheckErrorWithFlags(flags, {}, CScript() << OP_UTXOBYTECODE, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_2 << OP_UTXOBYTECODE, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        CheckErrorWithFlags(
            flags, {}, CScript() << -1 << OP_UTXOBYTECODE, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_UTXOBYTECODE, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(
            flags_inactive, {}, CScript() << OP_0 << OP_UTXOBYTECODE, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_OUTPOINTTXHASH (unary)
    {
        const valtype expected0(in1.hash.begin(), in1.hash.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPOINTTXHASH, context[0], {expected0});

        const valtype expected1(in2.hash.begin(), in2.hash.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_OUTPOINTTXHASH, context[1], {expected1});

        // failure (missing arg)
        CheckErrorWithFlags(flags, {}, CScript() << OP_OUTPOINTTXHASH, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_2 << OP_OUTPOINTTXHASH, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        CheckErrorWithFlags(
            flags, {}, CScript() << -1 << OP_OUTPOINTTXHASH, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPOINTTXHASH, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(
            flags_inactive, {}, CScript() << OP_0 << OP_OUTPOINTTXHASH, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_OUTPOINTINDEX (unary)
    {
        const valtype expected0(CScriptNum::fromIntUnchecked(in1.n).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPOINTINDEX, context[0], {expected0});
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPOINTINDEX, context[1], {expected0});

        const valtype expected1(CScriptNum::fromIntUnchecked(in2.n).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_OUTPOINTINDEX, context[0], {expected1});
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_OUTPOINTINDEX, context[1], {expected1});

        // failure (missing arg)
        CheckErrorWithFlags(flags, {}, CScript() << OP_OUTPOINTINDEX, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_2 << OP_OUTPOINTINDEX, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        CheckErrorWithFlags(
            flags, {}, CScript() << -1 << OP_OUTPOINTINDEX, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPOINTINDEX, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(
            flags_inactive, {}, CScript() << OP_0 << OP_OUTPOINTINDEX, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_INPUTBYTECODE (unary)
    {
        const valtype expected0(tx.vin[0].scriptSig.begin(), tx.vin[0].scriptSig.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_INPUTBYTECODE, context[0], {expected0});
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_INPUTBYTECODE, context[1], {expected0});

        const valtype expected1(tx.vin[1].scriptSig.begin(), tx.vin[1].scriptSig.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_INPUTBYTECODE, context[0], {expected1});
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_INPUTBYTECODE, context[1], {expected1});

        // failure (missing arg)
        CheckErrorWithFlags(flags, {}, CScript() << OP_INPUTBYTECODE, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_2 << OP_INPUTBYTECODE, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        CheckErrorWithFlags(
            flags, {}, CScript() << -1 << OP_INPUTBYTECODE, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_INPUTBYTECODE, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(
            flags_inactive, {}, CScript() << OP_0 << OP_INPUTBYTECODE, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_INPUTSEQUENCENUMBER (unary)
    {
        const valtype expected0(CScriptNum::fromIntUnchecked(tx.vin[0].nSequence).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_INPUTSEQUENCENUMBER, context[0], {expected0});
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_INPUTSEQUENCENUMBER, context[1], {expected0});

        const valtype expected1(CScriptNum::fromIntUnchecked(tx.vin[1].nSequence).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_INPUTSEQUENCENUMBER, context[0], {expected1});
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_INPUTSEQUENCENUMBER, context[1], {expected1});

        // failure (missing arg)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_INPUTSEQUENCENUMBER, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_2 << OP_INPUTSEQUENCENUMBER, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        CheckErrorWithFlags(
            flags, {}, CScript() << -1 << OP_INPUTSEQUENCENUMBER, context[1], SCRIPT_ERR_INVALID_TX_INPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_INPUTSEQUENCENUMBER, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(
            flags_inactive, {}, CScript() << OP_0 << OP_INPUTSEQUENCENUMBER, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_OUTPUTVALUE (unary)
    {
        const valtype expected0(CScriptNum::fromIntUnchecked(tx.vout[0].nValue).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPUTVALUE, context[0], {expected0});
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPUTVALUE, context[1], {expected0});

        const valtype expected1(CScriptNum::fromIntUnchecked(tx.vout[1].nValue).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_OUTPUTVALUE, context[0], {expected1});
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_OUTPUTVALUE, context[1], {expected1});

        const valtype expected2(CScriptNum::fromIntUnchecked(tx.vout[2].nValue).getvch());
        CheckPassWithFlags(flags, {}, CScript() << OP_2 << OP_OUTPUTVALUE, context[0], {expected2});
        CheckPassWithFlags(flags, {}, CScript() << OP_2 << OP_OUTPUTVALUE, context[1], {expected2});

        // failure (missing arg)
        CheckErrorWithFlags(flags, {}, CScript() << OP_OUTPUTVALUE, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_3 << OP_OUTPUTVALUE, context[1], SCRIPT_ERR_INVALID_TX_OUTPUT_INDEX);
        CheckErrorWithFlags(
            flags, {}, CScript() << -1 << OP_OUTPUTVALUE, context[1], SCRIPT_ERR_INVALID_TX_OUTPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPUTVALUE, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(flags_inactive, {}, CScript() << OP_0 << OP_OUTPUTVALUE, context[0], SCRIPT_ERR_BAD_OPCODE);
    }

    // OP_OUTPUTBYTECODE (unary)
    {
        const valtype expected0(tx.vout[0].scriptPubKey.begin(), tx.vout[0].scriptPubKey.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPUTBYTECODE, context[0], {expected0});
        CheckPassWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPUTBYTECODE, context[1], {expected0});

        const valtype expected1(tx.vout[1].scriptPubKey.begin(), tx.vout[1].scriptPubKey.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_OUTPUTBYTECODE, context[0], {expected1});
        CheckPassWithFlags(flags, {}, CScript() << OP_1 << OP_OUTPUTBYTECODE, context[1], {expected1});

        const valtype expected2(tx.vout[2].scriptPubKey.begin(), tx.vout[2].scriptPubKey.end());
        CheckPassWithFlags(flags, {}, CScript() << OP_2 << OP_OUTPUTBYTECODE, context[0], {expected2});
        CheckPassWithFlags(flags, {}, CScript() << OP_2 << OP_OUTPUTBYTECODE, context[1], {expected2});

        // failure (missing arg)
        CheckErrorWithFlags(flags, {}, CScript() << OP_OUTPUTBYTECODE, context[0], SCRIPT_ERR_INVALID_STACK_OPERATION);
        // failure (out of range)
        CheckErrorWithFlags(
            flags, {}, CScript() << OP_3 << OP_OUTPUTBYTECODE, context[1], SCRIPT_ERR_INVALID_TX_OUTPUT_INDEX);
        CheckErrorWithFlags(
            flags, {}, CScript() << -1 << OP_OUTPUTBYTECODE, context[1], SCRIPT_ERR_INVALID_TX_OUTPUT_INDEX);
        // failure (no context)
        CheckErrorWithFlags(flags, {}, CScript() << OP_0 << OP_OUTPUTBYTECODE, {}, SCRIPT_ERR_DATA_REQUIRED);
        // failure (not activated)
        CheckErrorWithFlags(
            flags_inactive, {}, CScript() << OP_0 << OP_OUTPUTBYTECODE, context[0], SCRIPT_ERR_BAD_OPCODE);
    }
}

BOOST_AUTO_TEST_SUITE_END()
