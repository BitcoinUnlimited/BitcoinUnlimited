// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/tx_verify.h"
#include "key.h"
#include "policy/policy.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/sighashtype.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"
#include "uint256.h"
#include "unlimited.h"

#include <vector>

#include <boost/test/unit_test.hpp>

using namespace std;

// Helpers:
static std::vector<unsigned char> Serialize(const CScript &s)
{
    std::vector<unsigned char> sSerialized(s.begin(), s.end());
    return sSerialized;
}

BOOST_FIXTURE_TEST_SUITE(sigopcount_tests, BasicTestingSetup)

void CheckScriptSigOps(const CScript &script, uint32_t accurate_sigops, uint32_t inaccurate_sigops, uint32_t datasigops)
{
    const uint32_t nodatasigflags = STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_ENABLE_CHECKDATASIG;
    const uint32_t datasigflags = STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENABLE_CHECKDATASIG;

    BOOST_CHECK_EQUAL(script.GetSigOpCount(nodatasigflags, false), inaccurate_sigops);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(datasigflags, false), inaccurate_sigops + datasigops);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(nodatasigflags, true), accurate_sigops);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(datasigflags, true), accurate_sigops + datasigops);

    const CScript p2sh = GetScriptForDestination(CScriptID(script));
    const CScript scriptSig = CScript() << OP_0 << Serialize(script);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(nodatasigflags, scriptSig), accurate_sigops);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(datasigflags, scriptSig), accurate_sigops + datasigops);

    // Check that GetSigOpCount do not report sigops in the P2SH script when the
    // P2SH flags isn't passed in.
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(SCRIPT_VERIFY_NONE, scriptSig), 0U);

    // Check that GetSigOpCount report the exact count when not passed a P2SH.
    BOOST_CHECK_EQUAL(script.GetSigOpCount(nodatasigflags, p2sh), accurate_sigops);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(datasigflags, p2sh), accurate_sigops + datasigops);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(SCRIPT_VERIFY_NONE, p2sh), accurate_sigops);
}


BOOST_AUTO_TEST_CASE(GetSigOpCount)
{
    // Test CScript::GetSigOpCount()
    CheckScriptSigOps(CScript(), 0, 0, 0);

    uint160 dummy;
    const CScript s1 = CScript() << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << OP_2 << OP_CHECKMULTISIG;
    CheckScriptSigOps(s1, 2, 20, 0);

    const CScript s2 = CScript(s1) << OP_IF << OP_CHECKSIG << OP_ENDIF;
    CheckScriptSigOps(s2, 3, 21, 0);

    std::vector<CPubKey> keys;
    for (int i = 0; i < 3; i++)
    {
        CKey k;
        k.MakeNewKey(true);
        keys.push_back(k.GetPubKey());
    }

    const CScript s3 = GetScriptForMultisig(1, keys);
    CheckScriptSigOps(s3, 3, 20, 0);

    const CScript p2sh = GetScriptForDestination(CScriptID(s3));
    CheckScriptSigOps(p2sh, 0, 0, 0);

    CScript scriptSig2;
    scriptSig2 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << Serialize(s3);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_ENABLE_CHECKDATASIG, scriptSig2), 3U);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENABLE_CHECKDATASIG, scriptSig2), 3U);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(SCRIPT_VERIFY_NONE, scriptSig2), 0U);

    const CScript s4 = CScript(s1) << OP_IF << OP_CHECKDATASIG << OP_ENDIF;
    CheckScriptSigOps(s4, 2, 20, 1);

    const CScript s5 = CScript(s4) << OP_CHECKDATASIGVERIFY;
    CheckScriptSigOps(s5, 2, 20, 2);
}

/**
 * Verifies script execution of the zeroth scriptPubKey of tx output and zeroth
 * scriptSig and witness of tx input.
 */
ScriptError VerifyWithFlag(const CTransaction &output, const CMutableTransaction &input, int flags)
{
    ScriptError error;
    CTransaction inputi(input);
    bool ret = VerifyScript(inputi.vin[0].scriptSig, output.vout[0].scriptPubKey, flags, MAX_OPS_PER_SCRIPT,
        TransactionSignatureChecker(&inputi, 0, output.vout[0].nValue), &error);
    BOOST_CHECK_EQUAL((ret == true), (error == SCRIPT_ERR_OK));

    return error;
}

/**
 * Builds a creationTx from scriptPubKey and a spendingTx from scriptSig and
 * witness such that spendingTx spends output zero of creationTx. Also inserts
 * creationTx's output into the coins view.
 */
void BuildTxs(CMutableTransaction &spendingTx,
    CCoinsViewCache &coins,
    CMutableTransaction &creationTx,
    const CScript &scriptPubKey,
    const CScript &scriptSig)
{
    creationTx.nVersion = 1;
    creationTx.vin.resize(1);
    creationTx.vin[0].prevout = COutPoint();
    creationTx.vin[0].scriptSig = CScript();
    creationTx.vout.resize(1);
    creationTx.vout[0].nValue = CAmount(1);
    creationTx.vout[0].scriptPubKey = scriptPubKey;

    spendingTx.nVersion = 1;
    spendingTx.vin.resize(1);
    spendingTx.vin[0].prevout = COutPoint(creationTx.GetHash(), 0);
    spendingTx.vin[0].scriptSig = scriptSig;
    spendingTx.vout.resize(1);
    spendingTx.vout[0].nValue = CAmount(1);
    spendingTx.vout[0].scriptPubKey = CScript();

    AddCoins(coins, CTransaction(creationTx), 0);
}

BOOST_AUTO_TEST_CASE(GetTxSigOpCost)
{
    // Transaction creates outputs
    CMutableTransaction creationTx;
    // Transaction that spends outputs and whose sig op cost is going to be
    // tested
    CMutableTransaction spendingTx;

    // Create utxo set
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    // Create key
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    // Default flags
    const uint32_t flags = SCRIPT_VERIFY_P2SH;

    // Multisig script (legacy counting)
    {
        CScript scriptPubKey = CScript() << 1 << ToByteVector(pubkey) << ToByteVector(pubkey) << 2
                                         << OP_CHECKMULTISIGVERIFY;
        // Do not use a valid signature to avoid using wallet operations.
        CScript scriptSig = CScript() << OP_0 << OP_0;

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig);

        // Legacy counting only includes signature operations in scriptSigs and
        // scriptPubKeys of a transaction and does not take the actual executed
        // sig operations into account. spendingTx in itself does not contain a
        // signature operation.
        BOOST_CHECK_EQUAL(GetTransactionSigOpCount(MakeTransactionRef(CTransaction(spendingTx)), coins, flags), 0);
        // creationTx contains two signature operations in its scriptPubKey, but
        // legacy counting is not accurate.
        BOOST_CHECK_EQUAL(GetTransactionSigOpCount(MakeTransactionRef(CTransaction(creationTx)), coins, flags),
            MAX_PUBKEYS_PER_MULTISIG);
        // Sanity check: script verification fails because of an invalid
        // signature.
        BOOST_CHECK_EQUAL(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags), SCRIPT_ERR_CHECKMULTISIGVERIFY);

        // Make sure non P2SH sigops are counted even if the flag for P2SH is
        // not passed in.
        BOOST_CHECK_EQUAL(
            GetTransactionSigOpCount(MakeTransactionRef(CTransaction(spendingTx)), coins, SCRIPT_VERIFY_NONE), 0);
        BOOST_CHECK_EQUAL(
            GetTransactionSigOpCount(MakeTransactionRef(CTransaction(creationTx)), coins, SCRIPT_VERIFY_NONE),
            MAX_PUBKEYS_PER_MULTISIG);
    }

    // Multisig nested in P2SH
    {
        CScript redeemScript = CScript() << 1 << ToByteVector(pubkey) << ToByteVector(pubkey) << 2
                                         << OP_CHECKMULTISIGVERIFY;
        CScript scriptPubKey = GetScriptForDestination(CScriptID(redeemScript));
        CScript scriptSig = CScript() << OP_0 << OP_0 << ToByteVector(redeemScript);

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCount(MakeTransactionRef(CTransaction(spendingTx)), coins, flags), 2);
        BOOST_CHECK_EQUAL(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags), SCRIPT_ERR_CHECKMULTISIGVERIFY);

        // Make sure P2SH sigops are not counted if the flag for P2SH is not
        // passed in.
        BOOST_CHECK_EQUAL(
            GetTransactionSigOpCount(MakeTransactionRef(CTransaction(spendingTx)), coins, SCRIPT_VERIFY_NONE), 0);
    }
}

BOOST_AUTO_TEST_CASE(test_consensus_sigops_limit)
{
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(1), MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(123456), MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(1000000), MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(1000001), 2 * MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(1348592), 2 * MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(2000000), 2 * MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(2000001), 3 * MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(2654321), 3 * MAX_BLOCK_SIGOPS_PER_MB);
    BOOST_CHECK_EQUAL(GetMaxBlockSigOpsCount(std::numeric_limits<uint32_t>::max()), 4295 * MAX_BLOCK_SIGOPS_PER_MB);
}

BOOST_AUTO_TEST_CASE(test_max_sigops_per_tx)
{
    CMutableTransaction tx;
    tx.nVersion = 1;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    tx.vin[0].scriptSig = CScript();
    tx.vout.resize(1);
    tx.vout[0].nValue = CAmount(1);
    tx.vout[0].scriptPubKey = CScript();

    {
        CValidationState state;
        BOOST_CHECK(CheckTransaction(MakeTransactionRef(CTransaction(tx)), state));
    }

    // Get just before the limit.
    for (size_t i = 0; i < MAX_TX_SIGOPS_COUNT; i++)
    {
        tx.vout[0].scriptPubKey << OP_CHECKSIG;
    }

    {
        CValidationState state;
        BOOST_CHECK(CheckTransaction(MakeTransactionRef(CTransaction(tx)), state));
    }

    // And go over.
    tx.vout[0].scriptPubKey << OP_CHECKSIG;

    {
        CValidationState state;
        BOOST_CHECK(!ContextualCheckTransaction(MakeTransactionRef(CTransaction(tx)), state, nullptr, Params()));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-too-many-sigops");
    }
}


class AlwaysGoodSignatureChecker : public BaseSignatureChecker
{
protected:
    unsigned int nFlags = SCRIPT_ENABLE_SIGHASH_FORKID;

public:
    //! Verifies a signature given the pubkey, signature and sighash
    virtual bool VerifySignature(const std::vector<uint8_t> &vchSig,
        const CPubKey &vchPubKey,
        const uint256 &sighash) const
    {
        if (vchSig.size() > 0)
            return true;
        return false;
    }

    //! Verifies a signature given the pubkey, signature, script, and transaction (member var)
    virtual bool CheckSig(const std::vector<unsigned char> &scriptSig,
        const std::vector<unsigned char> &vchPubKey,
        const CScript &scriptCode) const
    {
        if (scriptSig.size() > 0)
            return true;
        return false;
    }

    virtual bool CheckLockTime(const CScriptNum &nLockTime) const { return true; }
    virtual bool CheckSequence(const CScriptNum &nSequence) const { return true; }
    virtual ~AlwaysGoodSignatureChecker() {}
};

unsigned int evalForSigChecks(const CScript &scriptSig,
    const CScript &scriptPubKey,
    unsigned int flags,
    BaseSignatureChecker *checker = nullptr)
{
    AlwaysGoodSignatureChecker sigChecker;
    ScriptError serror;
    ScriptMachineResourceTracker tracker;
    if (checker == nullptr)
        checker = &sigChecker;

    bool worked = VerifyScript(scriptSig, scriptPubKey, flags, 0xffffffff, *checker, &serror, &tracker);
    if (!worked)
    {
        printf("unexpected verify failure: %d: %s\n", (int)serror, ScriptErrorString(serror));
    }
    BOOST_CHECK(worked == true); // All the sigops counting checks should be passed valid scripts

    return tracker.consensusSigCheckCount;
}

CMutableTransaction BuildCreditingTransaction(const CScript &scriptPubKey, CAmount nValue)
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
    txCredit.vout[0].nValue = nValue;

    return txCredit;
}

CMutableTransaction BuildSpendingTransaction(const CScript &scriptSig, const CMutableTransaction &txCredit)
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

CScript sign_multisig(const CScript &scriptPubKey, std::vector<CKey> keys, const CTransaction &transaction, CAmount amt)
{
    unsigned char sighashType = SIGHASH_ALL | SIGHASH_FORKID;

    uint256 hash = SignatureHash(scriptPubKey, transaction, 0, sighashType, amt, nullptr);
    assert(hash != SIGNATURE_HASH_ERROR);

    CScript result;
    //
    // NOTE: CHECKMULTISIG has an unfortunate bug; it requires
    // one extra item on the stack, before the signatures.
    // Putting OP_0 on the stack is the workaround;
    // fixing the bug would mean splitting the block chain (old
    // clients would not accept new CHECKMULTISIG transactions,
    // and vice-versa)
    //
    result << OP_0;
    for (const CKey &key : keys)
    {
        vector<unsigned char> vchSig;
        BOOST_CHECK(key.SignECDSA(hash, vchSig));
        vchSig.push_back(sighashType);
        result << vchSig;
    }
    return result;
}
CScript sign_multisig(const CScript &scriptPubKey, const CKey &key, const CTransaction &transaction, CAmount amt)
{
    std::vector<CKey> keys;
    keys.push_back(key);
    return sign_multisig(scriptPubKey, keys, transaction, amt);
}

BOOST_AUTO_TEST_CASE(consensusSigCheck)
{
    unsigned int sigchecks = 0;
    unsigned int flags = MANDATORY_SCRIPT_VERIFY_FLAGS;
    SigHashType sigHashType = SigHashType().withForkId();
    // Any non-0-size sig will be interpreted as a good signature by the sigchecker used in this code.
    // use 65 so this looks like a good schnorr signature.
    std::vector<unsigned char> fakeSchnorrSig(65);
    fakeSchnorrSig[64] = static_cast<uint8_t>(sigHashType.getRawSigHashType());

    std::vector<unsigned char> fakeSchnorrDataSig(64);

    std::vector<unsigned char> someData(10);

    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);

    {
        CScript scriptPubKey12;
        scriptPubKey12 << OP_1 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << OP_2
                       << OP_CHECKMULTISIG;

        CMutableTransaction txFrom12 = BuildCreditingTransaction(scriptPubKey12, 1);
        CMutableTransaction txTo12 = BuildSpendingTransaction(CScript(), txFrom12);

        CScript goodsig1 = sign_multisig(scriptPubKey12, key1, CTransaction(txTo12), txFrom12.vout[0].nValue);

        sigchecks = evalForSigChecks(goodsig1, scriptPubKey12, flags);
        BOOST_CHECK(sigchecks == 2); // ECDSA multisig sigchecks is N in a M-of-N sig
    }

    {
        CScript constraint = CScript() << OP_2 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey())
                                       << ToByteVector(key3.GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
        CScript satisfier = CScript() << OP_3 << fakeSchnorrSig << fakeSchnorrSig;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 2); // Schnorr multisig sigchecks is M in a M-of-N sig
    }

    {
        CScript constraint = CScript() << OP_2 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey())
                                       << ToByteVector(key3.GetPubKey()) << OP_3 << OP_CHECKMULTISIG << OP_DROP << OP_1;
        CScript satisfier = CScript() << OP_0 << OP_0 << OP_0;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 0);
    }

    { // CHECKSIG is 1
        CScript constraint = CScript() << ToByteVector(key2.GetPubKey()) << OP_CHECKSIG;
        CScript satisfier = CScript() << fakeSchnorrSig;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 1);
    }

    { // CDS is 1
        CScript constraint = CScript() << someData << ToByteVector(key2.GetPubKey()) << OP_CHECKDATASIG;
        CScript satisfier = CScript() << fakeSchnorrDataSig;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 1);
    }

    { // CHECKSIG is 1
        CScript constraint = CScript() << ToByteVector(key2.GetPubKey()) << OP_CHECKSIGVERIFY << OP_1;
        CScript satisfier = CScript() << fakeSchnorrSig;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 1);
    }

    { // CDS is 1
        CScript constraint = CScript() << someData << ToByteVector(key2.GetPubKey()) << OP_CHECKDATASIGVERIFY << OP_1;
        CScript satisfier = CScript() << fakeSchnorrDataSig;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 1);
    }

    // NULL sig is 0 sigchecks
    {
        CScript constraint = CScript() << ToByteVector(key2.GetPubKey()) << OP_CHECKSIG << OP_DROP << OP_1;
        CScript satisfier = CScript() << OP_0;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 0);
    }

    // NULL sig is 0 sigchecks
    {
        CScript constraint = CScript() << someData << ToByteVector(key2.GetPubKey()) << OP_CHECKDATASIG << OP_DROP
                                       << OP_1;
        CScript satisfier = CScript() << OP_0;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 0);
    }

    { // additive?
        CScript constraint = CScript() << ToByteVector(key2.GetPubKey()) << OP_CHECKSIGVERIFY << someData
                                       << ToByteVector(key2.GetPubKey()) << OP_CHECKDATASIG;
        CScript satisfier = CScript() << fakeSchnorrDataSig << fakeSchnorrSig;
        sigchecks = evalForSigChecks(satisfier, constraint, flags);
        BOOST_CHECK(sigchecks == 2);
    }
}


BOOST_AUTO_TEST_SUITE_END()
