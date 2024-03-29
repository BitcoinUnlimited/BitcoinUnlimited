// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2016-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"
#include "key.h"
#if defined(HAVE_CONSENSUS_LIB)
#include "script/bitcoinconsensus.h"
#endif
#include "script/script.h"
#include "script/sign.h"
#include "streams.h"

#include <array>

// FIXME: Dedup with BuildCreditingTransaction in test/script_tests.cpp.
static CMutableTransaction BuildCreditingTransaction(const CScript &scriptPubKey)
{
    CMutableTransaction txCredit;
    txCredit.nVersion = 1;
    txCredit.nLockTime = 0;
    txCredit.vin.resize(1);
    txCredit.vout.resize(1);
    txCredit.vin[0].prevout.SetNull();
    txCredit.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    txCredit.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txCredit.vout[0].scriptPubKey = scriptPubKey;
    txCredit.vout[0].nValue = 1;

    return txCredit;
}

// FIXME: Dedup with BuildSpendingTransaction in test/script_tests.cpp.
static CMutableTransaction BuildSpendingTransaction(const CScript &scriptSig, const CMutableTransaction &txCredit)
{
    CMutableTransaction txSpend;
    txSpend.nVersion = 1;
    txSpend.nLockTime = 0;
    txSpend.vin.resize(1);
    txSpend.vout.resize(1);
    txSpend.vin[0].prevout.hash = txCredit.GetHash();
    txSpend.vin[0].prevout.n = 0;
    txSpend.vin[0].scriptSig = scriptSig;
    txSpend.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txSpend.vout[0].scriptPubKey = CScript();
    txSpend.vout[0].nValue = txCredit.vout[0].nValue;

    return txSpend;
}

// Microbenchmark for verification of a basic P2WPKH script. Can be easily
// modified to measure performance of other types of scripts.
static void VerifyScriptBench(benchmark::State &state)
{
    const ECCVerifyHandle verify_handle;
    ECC_Start();

    const int flags = SCRIPT_VERIFY_P2SH;

    // Keypair.
    CKey key;
    static const std::array<unsigned char, 32> vchKey = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
    key.Set(vchKey.begin(), vchKey.end(), false);
    CPubKey pubkey = key.GetPubKey();
    uint160 pubkeyHash;
    CHash160().Write(pubkey.begin(), pubkey.size()).Finalize(pubkeyHash.begin());

    // Script.
    CScript scriptPubKey = CScript() << ToByteVector(pubkeyHash);
    CScript scriptSig;
    CScript witScriptPubkey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash) << OP_EQUALVERIFY
                                        << OP_CHECKSIG;
    const CMutableTransaction &txCredit = BuildCreditingTransaction(scriptPubKey);
    CMutableTransaction txSpend = BuildSpendingTransaction(scriptSig, txCredit);
    CScript &ssig = txSpend.vin[0].scriptSig;
    uint256 sighash = SignatureHash(witScriptPubkey, txSpend, 0, SIGHASH_ALL, txCredit.vout[0].nValue);
    assert(sighash != SIGNATURE_HASH_ERROR);
    std::vector<unsigned char> sig1;
    key.SignECDSA(sighash, sig1);
    sig1.push_back(static_cast<unsigned char>(SIGHASH_ALL));
    auto pubkeyvec = ToByteVector(pubkey);
    sig1.insert(sig1.end(), pubkeyvec.begin(), pubkeyvec.end());
    ssig = CScript() << sig1;

    // Benchmark.
    while (state.KeepRunning())
    {
        ScriptError err;
        bool success = VerifyScript(txSpend.vin[0].scriptSig, txCredit.vout[0].scriptPubKey, flags, MAX_OPS_PER_SCRIPT,
            MutableTransactionSignatureChecker(&txSpend, 0, txCredit.vout[0].nValue), &err);
        assert(err == SCRIPT_ERR_OK);
        assert(success);
    }
    ECC_Stop();
}

static void VerifyNestedIfScript(benchmark::State &state)
{
    std::vector<std::vector<unsigned char> > stack;
    CScript script;
    for (int i = 0; i < 100; ++i)
    {
        script << OP_1 << OP_IF;
    }
    for (int i = 0; i < 1000; ++i)
    {
        script << OP_1;
    }
    for (int i = 0; i < 100; ++i)
    {
        script << OP_ENDIF;
    }
    while (state.KeepRunning())
    {
        auto stack_copy = stack;
        ScriptError error;
        BaseSignatureChecker sigchecker;
        bool ret = EvalScript(stack_copy, script, 0, MAX_OPS_PER_SCRIPT, sigchecker, &error);
        assert(ret);
    }
}
BENCHMARK(VerifyScriptBench, 6300);

BENCHMARK(VerifyNestedIfScript, 100);
