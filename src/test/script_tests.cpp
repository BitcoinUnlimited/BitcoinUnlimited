// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "data/script_tests.json.h"

#include "core_io.h"
#include "key.h"
#include "keystore.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sighashtype.h"
#include "script/sign.h"
#include "test/scriptflags.h"
#include "test/test_bitcoin.h"
#include "unlimited.h"
#include "util.h"
#include "utilstrencodings.h"

#if defined(HAVE_CONSENSUS_LIB)
#include "script/bitcoinconsensus.h"
#endif

#include <fstream>
#include <stdint.h>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

using namespace std;

// Uncomment if you want to output updated JSON tests.
#define UPDATE_JSON_TESTS

static const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC | SCRIPT_ENABLE_SIGHASH_FORKID;


UniValue read_json(const std::string &jsondata)
{
    UniValue v;

    if (!v.read(jsondata) || !v.isArray())
    {
        BOOST_ERROR("Parse error.");
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}

struct ScriptErrorDesc
{
    ScriptError_t err;
    const char *name;
};

// clang-format off
static ScriptErrorDesc script_errors[] = {
    {SCRIPT_ERR_OK, "OK"},
    {SCRIPT_ERR_UNKNOWN_ERROR, "UNKNOWN_ERROR"},
    {SCRIPT_ERR_EVAL_FALSE, "EVAL_FALSE"},
    {SCRIPT_ERR_OP_RETURN, "OP_RETURN"},
    {SCRIPT_ERR_SCRIPT_SIZE, "SCRIPT_SIZE"},
    {SCRIPT_ERR_PUSH_SIZE, "PUSH_SIZE"},
    {SCRIPT_ERR_OP_COUNT, "OP_COUNT"},
    {SCRIPT_ERR_STACK_SIZE, "STACK_SIZE"},
    {SCRIPT_ERR_SIG_COUNT, "SIG_COUNT"},
    {SCRIPT_ERR_PUBKEY_COUNT, "PUBKEY_COUNT"},
    {SCRIPT_ERR_INVALID_OPERAND_SIZE, "OPERAND_SIZE"},
    {SCRIPT_ERR_INVALID_NUMBER_RANGE, "INVALID_NUMBER_RANGE"},
    {SCRIPT_ERR_INVALID_SPLIT_RANGE, "SPLIT_RANGE"},
    {SCRIPT_ERR_INVALID_BIT_COUNT, "INVALID_BIT_COUNT"},
    {SCRIPT_ERR_VERIFY, "VERIFY"},
    {SCRIPT_ERR_EQUALVERIFY, "EQUALVERIFY"},
    {SCRIPT_ERR_CHECKMULTISIGVERIFY, "CHECKMULTISIGVERIFY"},
    {SCRIPT_ERR_CHECKSIGVERIFY, "CHECKSIGVERIFY"},
    {SCRIPT_ERR_CHECKDATASIGVERIFY, "CHECKDATASIGVERIFY"},
    {SCRIPT_ERR_NUMEQUALVERIFY, "NUMEQUALVERIFY"},
    {SCRIPT_ERR_BAD_OPCODE, "BAD_OPCODE"},
    {SCRIPT_ERR_DISABLED_OPCODE, "DISABLED_OPCODE"},
    {SCRIPT_ERR_INVALID_STACK_OPERATION, "INVALID_STACK_OPERATION"},
    {SCRIPT_ERR_INVALID_ALTSTACK_OPERATION, "INVALID_ALTSTACK_OPERATION"},
    {SCRIPT_ERR_UNBALANCED_CONDITIONAL, "UNBALANCED_CONDITIONAL"},
    {SCRIPT_ERR_NEGATIVE_LOCKTIME, "NEGATIVE_LOCKTIME"},
    {SCRIPT_ERR_UNSATISFIED_LOCKTIME, "UNSATISFIED_LOCKTIME"},
    {SCRIPT_ERR_SIG_HASHTYPE, "SIG_HASHTYPE"},
    {SCRIPT_ERR_SIG_DER, "SIG_DER"},
    {SCRIPT_ERR_MINIMALDATA, "MINIMALDATA"},
    {SCRIPT_ERR_SIG_PUSHONLY, "SIG_PUSHONLY"},
    {SCRIPT_ERR_SIG_HIGH_S, "SIG_HIGH_S"},
    {SCRIPT_ERR_PUBKEYTYPE, "PUBKEYTYPE"},
    {SCRIPT_ERR_CLEANSTACK, "CLEANSTACK"},
    {SCRIPT_ERR_SIG_NULLFAIL, "NULLFAIL"},
    {SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS, "DISCOURAGE_UPGRADABLE_NOPS"},
    {SCRIPT_ERR_DIV_BY_ZERO, "DIV_BY_ZERO"},
    {SCRIPT_ERR_MOD_BY_ZERO, "MOD_BY_ZERO"},
    {SCRIPT_ERR_SIG_BADLENGTH, "SIG_BADLENGTH"},
    {SCRIPT_ERR_SIG_NONSCHNORR, "SIG_NONSCHNORR" },
    {SCRIPT_ERR_MUST_USE_FORKID, "MUST_USE_FORKID"},
    {SCRIPT_ERR_NONCOMPRESSED_PUBKEY, "NONCOMPRESSED_PUBKEY"},
    {SCRIPT_ERR_NUMBER_OVERFLOW, "NUMBER_OVERFLOW"},
    {SCRIPT_ERR_NUMBER_BAD_ENCODING, "NUMBER_BAD_ENCODING"},
    {SCRIPT_ERR_INVALID_BITFIELD_SIZE, "BITFIELD_SIZE"},
    {SCRIPT_ERR_INVALID_BIT_RANGE, "BIT_RANGE"},
};
// clang-format on

const char *FormatScriptError(ScriptError_t err)
{
    for (unsigned int i = 0; i < ARRAYLEN(script_errors); ++i)
        if (script_errors[i].err == err)
            return script_errors[i].name;
    BOOST_ERROR("Unknown scripterror enumeration value, update script_errors in script_tests.cpp.");
    return "";
}

ScriptError_t ParseScriptError(const std::string &name)
{
    for (unsigned int i = 0; i < ARRAYLEN(script_errors); ++i)
        if (script_errors[i].name == name)
            return script_errors[i].err;
    BOOST_ERROR("Unknown scripterror \"" << name << "\" in test description");
    return SCRIPT_ERR_UNKNOWN_ERROR;
}

BOOST_FIXTURE_TEST_SUITE(script_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(minimalpush)
{
    // Ensure that CheckMinimalPush always return true for non "pushing" opcodes
    std::vector<uint8_t> dummy{};
    for (const auto opcode : {OP_1NEGATE, OP_1, OP_2, OP_3, OP_4, OP_5, OP_6, OP_7, OP_8, OP_9, OP_10, OP_11, OP_12,
             OP_13, OP_14, OP_15, OP_16})
    {
        BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, opcode), true);
    }

    // Ensure that CheckMinimalPush return false in case we are try use a push opcodes operator whereas
    // we should have use OP_0 instead (i.e. data array is empty array)
    for (const auto opcode_b : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4})
    {
        BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, opcode_b), false);
    }

    // If data.size() is equal to 1 we should have used OP_1 .. OP_16.
    dummy = {0};
    BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, OP_PUSHDATA4), false);

    // Initialize the vector s to that its size is between 2 and 75
    for (int i = 0; i <= 10; i++)
    {
        dummy.push_back(1);
    }
    // In this case we should a direct push (opcode indicating number of bytes  pushed + those bytes)
    BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, OP_PUSHDATA4), false);

    // extend it to have the length between 76 and 255
    for (int i = 11; i < 240; i++)
    {
        dummy.push_back(1);
    }
    // in this case we must have used OP_PUSHDATA1
    BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, OP_PUSHDATA4), false);
    BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, OP_PUSHDATA1), true);

    // extend it to have the length between 256 and 65535
    for (int i = 241; i < 300; i++)
    {
        dummy.push_back(1);
    }
    // in this case we must have used OP_PUSHDATA2
    BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, OP_PUSHDATA4), false);
    BOOST_CHECK_EQUAL(CheckMinimalPush(dummy, OP_PUSHDATA2), true);
}

BOOST_AUTO_TEST_CASE(minimaldata_creation)
{
    std::vector<unsigned char> vec(1);

    // Check every encoding of a single byte vector since they are irksome
    for (CAmount qty = 0; qty < 256; qty++)
    {
        vec[0] = qty;
        CScript script = CScript() << vec << OP_DROP << OP_1;

        // Verify that the script passes standard checks, especially the data coding
        std::vector<std::vector<uint8_t> > stack;
        BaseSignatureChecker sigchecker;
        ScriptError err = SCRIPT_ERR_OK;
        bool r = EvalScript(stack, script, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_MINIMALDATA,
            MAX_OPS_PER_SCRIPT, sigchecker, &err);
        BOOST_CHECK(r);
        BOOST_CHECK(err != SCRIPT_ERR_MINIMALDATA);
    }

    // Check weird vector sizes
    for (int size = 0x0; size < 0xffff + 2; size++)
    {
        // Skip regions that are not weird
        if (size == 1)
            size = 0xff;
        if (size == 0x101)
            size = 0xffff;

        vec.resize(size);
        CScript script = CScript() << vec << OP_DROP << OP_1;
        std::vector<std::vector<uint8_t> > stack;
        BaseSignatureChecker sigchecker;
        ScriptError err = SCRIPT_ERR_OK;
        bool r = EvalScript(stack, script, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_MINIMALDATA,
            MAX_OPS_PER_SCRIPT, sigchecker, &err);

        // We know large scripts will fail the eval -- this is not interesting WRT this test
        if (size <= MAX_SCRIPT_SIZE)
            BOOST_CHECK(r);
        BOOST_CHECK(err != SCRIPT_ERR_MINIMALDATA);
    }
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

void DoTest(const CScript &scriptPubKey,
    const CScript &scriptSig,
    uint32_t flags,
    const std::string &message,
    int scriptError,
    CAmount nValue)
{
    bool expect = (scriptError == SCRIPT_ERR_OK);

    ScriptError err;
    CMutableTransaction txCredit = BuildCreditingTransaction(scriptPubKey, nValue);
    CMutableTransaction tx = BuildSpendingTransaction(scriptSig, txCredit);
    CMutableTransaction tx2 = tx;
    bool result = VerifyScript(scriptSig, scriptPubKey, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&tx, 0, txCredit.vout[0].nValue, flags), &err);
    BOOST_CHECK_MESSAGE(result == expect, message);
    BOOST_CHECK_MESSAGE(err == scriptError, std::string(FormatScriptError(err)) + " where " +
                                                std::string(FormatScriptError((ScriptError_t)scriptError)) +
                                                " expected: " + message);

    // Verify that removing flags from a passing test or adding flags to a
    // failing test does not change the result, except for some special flags.
    for (int i = 0; i < 16; ++i)
    {
        uint32_t extra_flags = InsecureRand32();
        // Some flags are not purely-restrictive and thus we can't assume
        // anything about what happens when they are flipped. Keep them as-is.
        extra_flags &= ~(SCRIPT_ENABLE_SIGHASH_FORKID | SCRIPT_ENABLE_REPLAY_PROTECTION |
                         SCRIPT_ENABLE_SCHNORR_MULTISIG | SCRIPT_ENABLE_OP_REVERSEBYTES);
        uint32_t combined_flags = expect ? (flags & ~extra_flags) : (flags | extra_flags);
        // Weed out invalid flag combinations.
        if (combined_flags & SCRIPT_VERIFY_CLEANSTACK)
        {
            combined_flags |= SCRIPT_VERIFY_P2SH;
        }

        BOOST_CHECK_MESSAGE(
            VerifyScript(scriptSig, scriptPubKey, combined_flags, MAX_OPS_PER_SCRIPT,
                MutableTransactionSignatureChecker(&tx, 0, txCredit.vout[0].nValue, combined_flags), &err) == expect,
            message + strprintf(" (with %s flags %08x)", expect ? "removed" : "added", combined_flags ^ flags));
    }

#if defined(HAVE_CONSENSUS_LIB)
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx2;
    if (nValue == 0)
    {
        BOOST_CHECK_MESSAGE(
            bitcoinconsensus_verify_script(begin_ptr(scriptPubKey), scriptPubKey.size(),
                (const unsigned char *)&stream[0], stream.size(), 0, flags, MAX_OPS_PER_SCRIPT, nullptr) == expect,
            message);
    }
#endif
}

void static NegateSignatureS(std::vector<unsigned char> &vchSig)
{
    // Parse the signature.
    std::vector<unsigned char> r, s;
    r = std::vector<unsigned char>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
    s = std::vector<unsigned char>(
        vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);

    // Really ugly to implement mod-n negation here, but it would be feature creep to expose such functionality from
    // libsecp256k1.
    static const unsigned char order[33] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0,
        0x36, 0x41, 0x41};
    while (s.size() < 33)
    {
        s.insert(s.begin(), 0x00);
    }
    int carry = 0;
    for (int p = 32; p >= 1; p--)
    {
        int n = (int)order[p] - s[p] - carry;
        s[p] = (n + 256) & 0xFF;
        carry = (n < 0);
    }
    assert(carry == 0);
    if (s.size() > 1 && s[0] == 0 && s[1] < 0x80)
    {
        s.erase(s.begin());
    }

    // Reconstruct the signature.
    vchSig.clear();
    vchSig.push_back(0x30);
    vchSig.push_back(4 + r.size() + s.size());
    vchSig.push_back(0x02);
    vchSig.push_back(r.size());
    vchSig.insert(vchSig.end(), r.begin(), r.end());
    vchSig.push_back(0x02);
    vchSig.push_back(s.size());
    vchSig.insert(vchSig.end(), s.begin(), s.end());
}

namespace
{
const unsigned char vchKey0[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
const unsigned char vchKey1[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
const unsigned char vchKey2[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};

struct KeyData
{
    CKey key0, key0C, key1, key1C, key2, key2C;
    CPubKey pubkey0, pubkey0C, pubkey0H;
    CPubKey pubkey1, pubkey1C;
    CPubKey pubkey2, pubkey2C;

    KeyData()
    {
        key0.Set(vchKey0, vchKey0 + 32, false);
        key0C.Set(vchKey0, vchKey0 + 32, true);
        pubkey0 = key0.GetPubKey();
        pubkey0H = key0.GetPubKey();
        pubkey0C = key0C.GetPubKey();
        *const_cast<unsigned char *>(&pubkey0H[0]) = 0x06 | (pubkey0H[64] & 1);

        key1.Set(vchKey1, vchKey1 + 32, false);
        key1C.Set(vchKey1, vchKey1 + 32, true);
        pubkey1 = key1.GetPubKey();
        pubkey1C = key1C.GetPubKey();

        key2.Set(vchKey2, vchKey2 + 32, false);
        key2C.Set(vchKey2, vchKey2 + 32, true);
        pubkey2 = key2.GetPubKey();
        pubkey2C = key2C.GetPubKey();
    }
};


class TestBuilder
{
public:
    //! Actually executed script
    CScript script;
    //! The P2SH redeemscript
    CScript redeemscript;
    CTransactionRef creditTx;
    CMutableTransaction spendTx;
    bool havePush;
    std::vector<unsigned char> push;
    std::string comment;
    int flags;
    int scriptError;
    CAmount nValue;

    void DoPush()
    {
        if (havePush)
        {
            spendTx.vin[0].scriptSig << push;
            havePush = false;
        }
    }

    void DoPush(const std::vector<unsigned char> &data)
    {
        DoPush();
        push = data;
        havePush = true;
    }

    std::vector<uint8_t> DoSignECDSA(const CKey &key,
        const uint256 &hash,
        unsigned int lenR = 32,
        unsigned int lenS = 32) const
    {
        std::vector<uint8_t> vchSig, r, s;
        uint32_t iter = 0;
        do
        {
            key.SignECDSA(hash, vchSig, iter++);
            if ((lenS == 33) != (vchSig[5 + vchSig[3]] == 33))
            {
                NegateSignatureS(vchSig);
            }

            r = std::vector<uint8_t>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
            s = std::vector<uint8_t>(
                vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);
        } while (lenR != r.size() || lenS != s.size());

        return vchSig;
    }

    std::vector<uint8_t> DoSignSchnorr(const CKey &key, const uint256 &hash) const
    {
        std::vector<uint8_t> vchSig;

        // no need to iterate for size; schnorrs are always same size.
        key.SignSchnorr(hash, vchSig);

        return vchSig;
    }

public:
    TestBuilder(const CScript &script_, const std::string &comment_, int flags_, bool P2SH = false, CAmount nValue_ = 0)
        : script(script_), havePush(false), comment(comment_), flags(flags_), scriptError(SCRIPT_ERR_OK),
          nValue(nValue_)
    {
        CScript scriptPubKey = script;
        if (P2SH)
        {
            redeemscript = scriptPubKey;
            scriptPubKey = CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemscript)) << OP_EQUAL;
        }
        creditTx = MakeTransactionRef(BuildCreditingTransaction(scriptPubKey, nValue));
        spendTx = BuildSpendingTransaction(CScript(), *creditTx);
    }

    TestBuilder &SetScriptError(ScriptError_t err)
    {
        scriptError = err;
        return *this;
    }

    TestBuilder &Add(const CScript &scriptLocal)
    {
        DoPush();
        spendTx.vin[0].scriptSig += scriptLocal;
        return *this;
    }

    TestBuilder &Num(int num)
    {
        DoPush();
        spendTx.vin[0].scriptSig << num;
        return *this;
    }

    TestBuilder &NumULL(uint64_t num)
    {
        DoPush();
        spendTx.vin[0].scriptSig << num;
        return *this;
    }

    TestBuilder &Push(const uint256 &hash)
    {
        DoPush(ToByteVector(hash));
        return *this;
    }

    TestBuilder &Push(const std::string &hex)
    {
        DoPush(ParseHex(hex));
        return *this;
    }

    TestBuilder &Push(const CScript &_script)
    {
        DoPush(std::vector<uint8_t>(_script.begin(), _script.end()));
        return *this;
    }

    TestBuilder &PushSigECDSA(const CKey &key,
        int nHashType = SIGHASH_ALL,
        unsigned int lenR = 32,
        unsigned int lenS = 32,
        CAmount amount = 0)
    {
        uint256 hash = SignatureHash(script, spendTx, 0, nHashType, amount);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        std::vector<uint8_t> vchSig = DoSignECDSA(key, hash, lenR, lenS);
        vchSig.push_back(static_cast<unsigned char>(nHashType));
        DoPush(vchSig);
        return *this;
    }

    TestBuilder &PushSigECDSA(const CKey &key,
        SigHashType nHashType,
        unsigned int lenR = 32,
        unsigned int lenS = 32,
        CAmount amount = 0)
    {
        uint256 hash = SignatureHash(script, spendTx, 0, nHashType.getRawSigHashType(), amount);
        BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
        std::vector<uint8_t> vchSig = DoSignECDSA(key, hash, lenR, lenS);
        vchSig.push_back(static_cast<unsigned char>(nHashType.getRawSigHashType()));
        DoPush(vchSig);
        return *this;
    }

    TestBuilder &PushSigSchnorr(const CKey &key,
        SigHashType sigHashType = SigHashType(),
        CAmount amount = 0,
        uint32_t sigFlags = SCRIPT_ENABLE_SIGHASH_FORKID)
    {
        uint256 hash =
            SignatureHash(script, CTransaction(spendTx), 0, sigHashType.getRawSigHashType(), amount, nullptr);
        std::vector<uint8_t> vchSig = DoSignSchnorr(key, hash);
        vchSig.push_back(static_cast<uint8_t>(sigHashType.getRawSigHashType()));
        DoPush(vchSig);
        return *this;
    }

    TestBuilder &PushDataSigECDSA(const CKey &key,
        const std::vector<uint8_t> &data,
        unsigned int lenR = 32,
        unsigned int lenS = 32)
    {
        std::vector<uint8_t> vchHash(32);
        CSHA256().Write(data.data(), data.size()).Finalize(vchHash.data());

        DoPush(DoSignECDSA(key, uint256(vchHash), lenR, lenS));
        return *this;
    }

    TestBuilder &PushDataSigSchnorr(const CKey &key, const std::vector<uint8_t> &data)
    {
        std::vector<uint8_t> vchHash(32);
        CSHA256().Write(data.data(), data.size()).Finalize(vchHash.data());

        DoPush(DoSignSchnorr(key, uint256(vchHash)));
        return *this;
    }

    TestBuilder &PushECDSARecoveredPubKey(const std::vector<uint8_t> &rdata,
        const std::vector<uint8_t> &sdata,
        SigHashType sigHashType = SigHashType(),
        CAmount amount = 0)
    {
        // This calculates a pubkey to verify with a given ECDSA transaction
        // signature.
        uint256 hash =
            SignatureHash(script, CTransaction(spendTx), 0, sigHashType.getRawSigHashType(), amount, nullptr);

        assert(rdata.size() <= 32);
        assert(sdata.size() <= 32);

        // Our strategy: make a 'key recovery' signature, and just try all the
        // recovery IDs. If none of them work then this means the 'r' value
        // doesn't have any corresponding point, and the caller should pick a
        // different r.
        std::vector<uint8_t> vchSig(65, 0);
        std::copy(rdata.begin(), rdata.end(), vchSig.begin() + (33 - rdata.size()));
        std::copy(sdata.begin(), sdata.end(), vchSig.begin() + (65 - sdata.size()));

        CPubKey key;
        for (uint8_t recid : {0, 1, 2, 3})
        {
            vchSig[0] = 31 + recid;
            if (key.RecoverCompact(hash, vchSig))
            {
                // found a match
                break;
            }
        }
        if (!key.IsValid())
        {
            throw std::runtime_error(std::string("Could not generate pubkey for ") + HexStr(rdata));
        }
        std::vector<uint8_t> vchKey(key.begin(), key.end());

        DoPush(vchKey);
        return *this;
    }

    TestBuilder &PushECDSASigFromParts(const std::vector<uint8_t> &rdata,
        const std::vector<uint8_t> &sdata,
        SigHashType sigHashType = SigHashType())
    {
        // Constructs a DER signature out of variable-length r and s arrays &
        // adds hashtype byte.
        assert(rdata.size() <= 32);
        assert(sdata.size() <= 32);
        assert(rdata.size() > 0);
        assert(sdata.size() > 0);
        assert(rdata[0] != 0);
        assert(sdata[0] != 0);
        std::vector<uint8_t> vchSig{0x30, 0x00, 0x02};
        if (rdata[0] & 0x80)
        {
            vchSig.push_back(rdata.size() + 1);
            vchSig.push_back(0);
            vchSig.insert(vchSig.end(), rdata.begin(), rdata.end());
        }
        else
        {
            vchSig.push_back(rdata.size());
            vchSig.insert(vchSig.end(), rdata.begin(), rdata.end());
        }
        vchSig.push_back(0x02);
        if (sdata[0] & 0x80)
        {
            vchSig.push_back(sdata.size() + 1);
            vchSig.push_back(0);
            vchSig.insert(vchSig.end(), sdata.begin(), sdata.end());
        }
        else
        {
            vchSig.push_back(sdata.size());
            vchSig.insert(vchSig.end(), sdata.begin(), sdata.end());
        }
        vchSig[1] = vchSig.size() - 2;
        vchSig.push_back(static_cast<uint8_t>(sigHashType.getRawSigHashType()));
        DoPush(vchSig);
        return *this;
    }

    TestBuilder &Push(const CPubKey &pubkey)
    {
        DoPush(std::vector<uint8_t>(pubkey.begin(), pubkey.end()));
        return *this;
    }

    TestBuilder &PushRedeem()
    {
        DoPush(std::vector<unsigned char>(redeemscript.begin(), redeemscript.end()));
        return *this;
    }

    TestBuilder &EditPush(unsigned int pos, const std::string &hexin, const std::string &hexout)
    {
        assert(havePush);
        std::vector<unsigned char> datain = ParseHex(hexin);
        std::vector<unsigned char> dataout = ParseHex(hexout);
        assert(pos + datain.size() <= push.size());
        BOOST_CHECK_MESSAGE(
            std::vector<unsigned char>(push.begin() + pos, push.begin() + pos + datain.size()) == datain, comment);
        push.erase(push.begin() + pos, push.begin() + pos + datain.size());
        push.insert(push.begin() + pos, dataout.begin(), dataout.end());
        return *this;
    }

    TestBuilder &DamagePush(unsigned int pos)
    {
        assert(havePush);
        assert(pos < push.size());
        push[pos] ^= 1;
        return *this;
    }

    TestBuilder &Test()
    {
        TestBuilder copy = *this; // Make a copy so we can rollback the push.
        DoPush();
        DoTest(creditTx->vout[0].scriptPubKey, spendTx.vin[0].scriptSig, flags, comment, scriptError, nValue);
        *this = copy;
        return *this;
    }

    UniValue GetJSON()
    {
        DoPush();
        UniValue array(UniValue::VARR);
        if (nValue != 0)
        {
            UniValue amount(UniValue::VARR);
            amount.push_back(ValueFromAmount(nValue));
            array.push_back(amount);
        }
        array.push_back(FormatScript(spendTx.vin[0].scriptSig));
        array.push_back(FormatScript(creditTx->vout[0].scriptPubKey));
        array.push_back(FormatScriptFlags(flags));
        array.push_back(FormatScriptError((ScriptError_t)scriptError));
        array.push_back(comment);
        return array;
    }

    std::string GetComment() { return comment; }
    const CScript &GetScriptPubKey() { return creditTx->vout[0].scriptPubKey; }
};

std::string JSONPrettyPrint(const UniValue &univalue)
{
    std::string ret = univalue.write(4);
    // Workaround for libunivalue pretty printer, which puts a space between comma's and newlines
    size_t pos = 0;
    while ((pos = ret.find(" \n", pos)) != std::string::npos)
    {
        ret.replace(pos, 2, "\n");
        pos++;
    }
    return ret;
}

void UpdateJSONTests(std::vector<TestBuilder> &tests)
{
    std::set<std::string> tests_set;
    {
        UniValue json_tests = read_json(
            std::string(json_tests::script_tests, json_tests::script_tests + sizeof(json_tests::script_tests)));

        for (unsigned int idx = 0; idx < json_tests.size(); idx++)
        {
            const UniValue &tv = json_tests[idx];
            tests_set.insert(JSONPrettyPrint(tv.get_array()));
        }
    }

    std::string strGen;

    for (TestBuilder &test : tests)
    {
        test.Test();
        std::string str = JSONPrettyPrint(test.GetJSON());
#ifndef UPDATE_JSON_TESTS
        if (tests_set.count(str) == 0)
        {
            BOOST_CHECK_MESSAGE(false, "Missing auto script_valid test: " + test.GetComment());
        }
#endif
        strGen += str + ",\n";
    }

    return;
}
}

BOOST_AUTO_TEST_CASE(script_build_1)
{
    const KeyData keys;

    std::vector<TestBuilder> tests;

    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK", 0).PushSigECDSA(keys.key0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK, bad sig", 0)
                        .PushSigECDSA(keys.key0)
                        .DamagePush(10)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));

    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
                        "P2PKH", 0)
                        .PushSigECDSA(keys.key1)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey2C.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
                        "P2PKH, bad pubkey", 0)
                        .PushSigECDSA(keys.key2)
                        .Push(keys.pubkey2C)
                        .DamagePush(5)
                        .SetScriptError(SCRIPT_ERR_EQUALVERIFY));

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG, "P2PK anyonecanpay", 0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL | SIGHASH_ANYONECANPAY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                        "P2PK anyonecanpay marked with normal hashtype", 0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL | SIGHASH_ANYONECANPAY)
                        .EditPush(70, "81", "01")
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));

    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG, "P2SH(P2PK)", SCRIPT_VERIFY_P2SH, true)
            .PushSigECDSA(keys.key0)
            .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG, "P2SH(P2PK), bad redeemscript",
                        SCRIPT_VERIFY_P2SH, true)
                        .PushSigECDSA(keys.key0)
                        .PushRedeem()
                        .DamagePush(10)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));

    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
                        "P2SH(P2PKH), bad sig but no VERIFY_P2SH", 0, true)
                        .PushSigECDSA(keys.key0)
                        .DamagePush(10)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
                        "P2SH(P2PKH), bad sig", SCRIPT_VERIFY_P2SH, true)
                        .PushSigECDSA(keys.key0)
                        .DamagePush(10)
                        .PushRedeem()
                        .SetScriptError(SCRIPT_ERR_EQUALVERIFY));

    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "3-of-3", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key0)
                        .PushSigECDSA(keys.key1)
                        .PushSigECDSA(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "3-of-3, 2 sigs", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key0)
                        .PushSigECDSA(keys.key1)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));

    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "P2SH(2-of-3)", SCRIPT_VERIFY_P2SH, true)
                        .Num(0)
                        .PushSigECDSA(keys.key1)
                        .PushSigECDSA(keys.key2)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "P2SH(2-of-3), 1 sig", SCRIPT_VERIFY_P2SH, true)
                        .Num(0)
                        .PushSigECDSA(keys.key1)
                        .Num(0)
                        .PushRedeem()
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                        "P2PK with too much R padding but no DERSIG", 0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000"));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "P2PK with too much R padding",
                        SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                        "P2PK with too much S padding but no DERSIG", 0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL)
                        .EditPush(1, "44", "45")
                        .EditPush(37, "20", "2100"));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "P2PK with too much S padding",
                        SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL)
                        .EditPush(1, "44", "45")
                        .EditPush(37, "20", "2100")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                        "P2PK with too little R padding but no DERSIG", 0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220"));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                        "P2PK with too little R padding", SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with bad sig with too much R padding but no DERSIG", 0)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .DamagePush(10));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with bad sig with too much R padding", SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .DamagePush(10)
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with too much R padding but no DERSIG", 0)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with too much R padding", SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));

    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "BIP66 example 1, without DERSIG", 0)
            .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
            .EditPush(1, "45022100", "440220"));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "BIP66 example 1, with DERSIG",
                        SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 2, without DERSIG", 0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 2, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "BIP66 example 3, without DERSIG", 0)
            .Num(0)
            .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "BIP66 example 3, with DERSIG",
                        SCRIPT_VERIFY_DERSIG)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 4, without DERSIG", 0)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 4, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 4, with DERSIG, non-null DER-compliant signature", SCRIPT_VERIFY_DERSIG)
                        .Push("300602010102010101"));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 4, with DERSIG and NULLFAIL", SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_NULLFAIL)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 4, with DERSIG and NULLFAIL, "
                        "non-null DER-compliant signature",
                        SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_NULLFAIL)
                        .Push("300602010102010101")
                        .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "BIP66 example 5, without DERSIG", 0)
            .Num(1)
            .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG, "BIP66 example 5, with DERSIG",
                        SCRIPT_VERIFY_DERSIG)
                        .Num(1)
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 6, without DERSIG", 0)
                        .Num(1));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                        "BIP66 example 6, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(1)
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "BIP66 example 7, without DERSIG", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .PushSigECDSA(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "BIP66 example 7, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .PushSigECDSA(keys.key2)
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG << OP_NOT,
                        "BIP66 example 8, without DERSIG", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .PushSigECDSA(keys.key2)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG << OP_NOT,
                        "BIP66 example 8, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .PushSigECDSA(keys.key2)
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "BIP66 example 9, without DERSIG", 0)
                        .Num(0)
                        .Num(0)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "BIP66 example 9, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(0)
                        .Num(0)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG << OP_NOT,
                        "BIP66 example 10, without DERSIG", 0)
                        .Num(0)
                        .Num(0)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220"));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG << OP_NOT,
                        "BIP66 example 10, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(0)
                        .Num(0)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "BIP66 example 11, without DERSIG", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "BIP66 example 11, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG << OP_NOT,
                        "BIP66 example 12, without DERSIG", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2
                                          << OP_CHECKMULTISIG << OP_NOT,
                        "BIP66 example 12, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                        "P2PK with multi-byte hashtype, without DERSIG", 0)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL)
                        .EditPush(70, "01", "0101"));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                        "P2PK with multi-byte hashtype, with DERSIG", SCRIPT_VERIFY_DERSIG)
                        .PushSigECDSA(keys.key2, SIGHASH_ALL)
                        .EditPush(70, "01", "0101")
                        .SetScriptError(SCRIPT_ERR_SIG_DER));

    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG, "P2PK with high S but no LOW_S", 0)
            .PushSigECDSA(keys.key2, SIGHASH_ALL, 32, 33));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG, "P2PK with high S", SCRIPT_VERIFY_LOW_S)
            .PushSigECDSA(keys.key2, SIGHASH_ALL, 32, 33)
            .SetScriptError(SCRIPT_ERR_SIG_HIGH_S));

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                        "P2PK with hybrid pubkey but no STRICTENC", 0)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG, "P2PK with hybrid pubkey",
                        SCRIPT_VERIFY_STRICTENC)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with hybrid pubkey but no STRICTENC", 0)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with hybrid pubkey", SCRIPT_VERIFY_STRICTENC)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with invalid hybrid pubkey but no STRICTENC", 0)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL)
                        .DamagePush(10));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with invalid hybrid pubkey", SCRIPT_VERIFY_STRICTENC)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL)
                        .DamagePush(10)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "1-of-2 with the second 1 hybrid pubkey and no STRICTENC", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "1-of-2 with the second 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0H) << OP_2
                                          << OP_CHECKMULTISIG,
                        "1-of-2 with the first 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC)
                        .Num(0)
                        .PushSigECDSA(keys.key1, SIGHASH_ALL)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                        "P2PK with undefined hashtype but no STRICTENC", 0)
                        .PushSigECDSA(keys.key1, 5));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG, "P2PK with undefined hashtype",
                        SCRIPT_VERIFY_STRICTENC)
                        .PushSigECDSA(keys.key1, 5)
                        .SetScriptError(SCRIPT_ERR_SIG_HASHTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with invalid sig and undefined hashtype but no STRICTENC", 0)
                        .PushSigECDSA(keys.key1, 5)
                        .DamagePush(10));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                        "P2PK NOT with invalid sig and undefined hashtype", SCRIPT_VERIFY_STRICTENC)
                        .PushSigECDSA(keys.key1, 5)
                        .DamagePush(10)
                        .SetScriptError(SCRIPT_ERR_SIG_HASHTYPE));

    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "3-of-3 with nonzero dummy", 0)
                        .Num(1)
                        .PushSigECDSA(keys.key0)
                        .PushSigECDSA(keys.key1)
                        .PushSigECDSA(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                        "3-of-3 NOT with invalid sig and nonzero dummy", 0)
                        .Num(1)
                        .PushSigECDSA(keys.key0)
                        .PushSigECDSA(keys.key1)
                        .PushSigECDSA(keys.key2)
                        .DamagePush(10));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "2-of-2 with two identical keys and sigs pushed using OP_DUP but no SIGPUSHONLY", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key1)
                        .Add(CScript() << OP_DUP));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "2-of-2 with two identical keys and sigs pushed using OP_DUP", SCRIPT_VERIFY_SIGPUSHONLY)
                        .Num(0)
                        .PushSigECDSA(keys.key1)
                        .Add(CScript() << OP_DUP)
                        .SetScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                        "P2SH(P2PK) with non-push scriptSig but no P2SH or SIGPUSHONLY", 0, true)
                        .PushSigECDSA(keys.key2)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                        "P2PK with non-push scriptSig but with P2SH validation", 0)
                        .PushSigECDSA(keys.key2)
                        .Add(CScript() << OP_NOP8));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                        "P2SH(P2PK) with non-push scriptSig but no SIGPUSHONLY", SCRIPT_VERIFY_P2SH, true)
                        .PushSigECDSA(keys.key2)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .SetScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                        "P2SH(P2PK) with non-push scriptSig but not P2SH", SCRIPT_VERIFY_SIGPUSHONLY, true)
                        .PushSigECDSA(keys.key2)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .SetScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2
                                          << OP_CHECKMULTISIG,
                        "2-of-2 with two identical keys and sigs pushed", SCRIPT_VERIFY_SIGPUSHONLY)
                        .Num(0)
                        .PushSigECDSA(keys.key1)
                        .PushSigECDSA(keys.key1));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                        "P2PK with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_P2SH)
                        .Num(11)
                        .PushSigECDSA(keys.key0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK with unnecessary input",
                        SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH)
                        .Num(11)
                        .PushSigECDSA(keys.key0)
                        .SetScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                        "P2SH with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_P2SH, true)
                        .Num(11)
                        .PushSigECDSA(keys.key0)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2SH with unnecessary input",
                        SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH, true)
                        .Num(11)
                        .PushSigECDSA(keys.key0)
                        .PushRedeem()
                        .SetScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2SH with CLEANSTACK",
                        SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH, true)
                        .PushSigECDSA(keys.key0)
                        .PushRedeem());

    static const CAmount TEST_AMOUNT = 12345000000000;
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK FORKID",
                        SCRIPT_ENABLE_SIGHASH_FORKID, false, TEST_AMOUNT)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL | SIGHASH_FORKID, 32, 32, TEST_AMOUNT));

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK INVALID AMOUNT",
                        SCRIPT_ENABLE_SIGHASH_FORKID, false, TEST_AMOUNT)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL | SIGHASH_FORKID, 32, 32, TEST_AMOUNT + 1)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "P2PK INVALID FORKID", 0, false,
                        TEST_AMOUNT)
                        .PushSigECDSA(keys.key0, SIGHASH_ALL | SIGHASH_FORKID, 32, 32, TEST_AMOUNT)
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));

    // Test OP_CHECKDATASIG
    const uint32_t checkdatasigflags = SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_NULLFAIL;

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG, "Standard CHECKDATASIG",
                        checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {})
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
                        "CHECKDATASIG with NULLFAIL flags", checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {})
                        .Num(1)
                        .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
                        "CHECKDATASIG without NULLFAIL flags", checkdatasigflags & ~SCRIPT_VERIFY_NULLFAIL)
                        .PushDataSigECDSA(keys.key1, {})
                        .Num(1));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
                        "CHECKDATASIG empty signature", checkdatasigflags)
                        .Num(0)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
                        "CHECKDATASIG with High S but no Low S", checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {}, 32, 33)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG, "CHECKDATASIG with High S",
                        checkdatasigflags | SCRIPT_VERIFY_LOW_S)
                        .PushDataSigECDSA(keys.key1, {}, 32, 33)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_SIG_HIGH_S));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
            "CHECKDATASIG with too little R padding but no DERSIG", checkdatasigflags & ~SCRIPT_VERIFY_STRICTENC)
            .PushDataSigECDSA(keys.key1, {}, 33, 32)
            .EditPush(1, "45022100", "440220")
            .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
                        "CHECKDATASIG with too little R padding", checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {}, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG,
            "CHECKDATASIG with hybrid pubkey but no STRICTENC", checkdatasigflags & ~SCRIPT_VERIFY_STRICTENC)
            .PushDataSigECDSA(keys.key0, {})
            .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG,
                        "CHECKDATASIG with hybrid pubkey", checkdatasigflags)
                        .PushDataSigECDSA(keys.key0, {})
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG << OP_NOT,
                        "CHECKDATASIG with invalid hybrid pubkey but no STRICTENC", 0)
                        .PushDataSigECDSA(keys.key0, {})
                        .DamagePush(10)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG,
                        "CHECKDATASIG with invalid hybrid pubkey", checkdatasigflags)
                        .PushDataSigECDSA(keys.key0, {})
                        .DamagePush(10)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));

    // Test OP_CHECKDATASIGVERIFY
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "Standard CHECKDATASIGVERIFY", checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {})
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIGVERIFY with NULLFAIL flags", checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {})
                        .Num(1)
                        .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIGVERIFY without NULLFAIL flags", checkdatasigflags & ~SCRIPT_VERIFY_NULLFAIL)
                        .PushDataSigECDSA(keys.key1, {})
                        .Num(1)
                        .SetScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIGVERIFY empty signature", checkdatasigflags)
                        .Num(0)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIG with High S but no Low S", checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {}, 32, 33)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIG with High S", checkdatasigflags | SCRIPT_VERIFY_LOW_S)
                        .PushDataSigECDSA(keys.key1, {}, 32, 33)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_SIG_HIGH_S));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
            "CHECKDATASIGVERIFY with too little R padding but no DERSIG", checkdatasigflags & ~SCRIPT_VERIFY_STRICTENC)
            .PushDataSigECDSA(keys.key1, {}, 33, 32)
            .EditPush(1, "45022100", "440220")
            .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIGVERIFY with too little R padding", checkdatasigflags)
                        .PushDataSigECDSA(keys.key1, {}, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
            "CHECKDATASIGVERIFY with hybrid pubkey but no STRICTENC", checkdatasigflags & ~SCRIPT_VERIFY_STRICTENC)
            .PushDataSigECDSA(keys.key0, {})
            .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIGVERIFY with hybrid pubkey", checkdatasigflags)
                        .PushDataSigECDSA(keys.key0, {})
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIGVERIFY with invalid hybrid pubkey but no STRICTENC", 0)
                        .PushDataSigECDSA(keys.key0, {})
                        .DamagePush(10)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
                        "CHECKDATASIGVERIFY with invalid hybrid pubkey", checkdatasigflags)
                        .PushDataSigECDSA(keys.key0, {})
                        .DamagePush(10)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));

    // Update tests
    UpdateJSONTests(tests);
}

BOOST_AUTO_TEST_CASE(script_build_2)
{
    const KeyData keys;

    std::vector<TestBuilder> tests;


    // Test all six CHECK*SIG* opcodes with Schnorr signatures.
    // - STRICTENC flag on/off
    // - test with different key / mismatching key

    // CHECKSIG & Schnorr
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "CHECKSIG Schnorr", 0)
                        .PushSigSchnorr(keys.key0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "CHECKSIG Schnorr w/ STRICTENC",
                        SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG, "CHECKSIG Schnorr other key",
                        SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key1));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG << OP_NOT,
                        "CHECKSIG Schnorr mismatched key", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key1));

    // CHECKSIGVERIFY & Schnorr
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIGVERIFY << OP_1, "CHECKSIGVERIFY Schnorr", 0)
            .PushSigSchnorr(keys.key0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIGVERIFY << OP_1,
                        "CHECKSIGVERIFY Schnorr w/ STRICTENC", SCRIPT_VERIFY_STRICTENC)
                        .PushSigECDSA(keys.key0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIGVERIFY << OP_1,
                        "CHECKSIGVERIFY Schnorr other key", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key1));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIGVERIFY << OP_1,
                        "CHECKSIGVERIFY Schnorr mismatched key", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key1)
                        .SetScriptError(SCRIPT_ERR_CHECKSIGVERIFY));

    // CHECKDATASIG & Schnorr
    tests.push_back(
        TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey0) << OP_CHECKDATASIG, "CHECKDATASIG Schnorr", 0)
            .PushDataSigSchnorr(keys.key0, {}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey0) << OP_CHECKDATASIG,
                        "CHECKDATASIG Schnorr w/ STRICTENC", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key0, {}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey1) << OP_CHECKDATASIG,
                        "CHECKDATASIG Schnorr other key", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey0) << OP_CHECKDATASIG << OP_NOT,
                        "CHECKDATASIG Schnorr mismatched key", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {}));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << OP_CHECKDATASIG,
                        "CHECKDATASIG Schnorr other message", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {1}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey1) << OP_CHECKDATASIG << OP_NOT,
                        "CHECKDATASIG Schnorr wrong message", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {1}));


    // CHECKDATASIGVERIFY & Schnorr
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey0) << OP_CHECKDATASIGVERIFY << OP_1,
                        "CHECKDATASIGVERIFY Schnorr", 0)
                        .PushDataSigSchnorr(keys.key0, {}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey0) << OP_CHECKDATASIGVERIFY << OP_1,
                        "CHECKDATASIGVERIFY Schnorr w/ STRICTENC", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key0, {}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey1) << OP_CHECKDATASIGVERIFY << OP_1,
                        "CHECKDATASIGVERIFY Schnorr other key", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey0) << OP_CHECKDATASIGVERIFY << OP_1,
                        "CHECKDATASIGVERIFY Schnorr mismatched key", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {})
                        .SetScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << OP_CHECKDATASIGVERIFY << OP_1,
                        "CHECKDATASIGVERIFY Schnorr other message", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {1}));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(keys.pubkey1) << OP_CHECKDATASIGVERIFY << OP_1,
                        "CHECKDATASIGVERIFY Schnorr wrong message", SCRIPT_VERIFY_STRICTENC)
                        .PushDataSigSchnorr(keys.key1, {1})
                        .SetScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));

    // CHECKMULTISIG 1-of-1 & Schnorr
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG Schnorr 1-of-1 working w/ STRICTENC",
                        SCRIPT_VERIFY_STRICTENC | SCRIPT_ENABLE_SCHNORR_MULTISIG)
                        .Num(1)
                        .PushSigSchnorr(keys.key0)
                        .SetScriptError(SCRIPT_ERR_OK));

    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG Schnorr w/ no STRICTENC", 0)
                        .Num(0)
                        .PushSigSchnorr(keys.key0)
                        .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG Schnorr w/ STRICTENC", SCRIPT_VERIFY_STRICTENC)
                        .Num(0)
                        .PushSigSchnorr(keys.key0)
                        .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));

    // Test multisig with multiple Schnorr signatures
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "Schnorr 3-of-3", 0)
                        .Num(0)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "Schnorr-ECDSA-mixed 3-of-3", 0)
                        .Num(0)
                        .PushSigECDSA(keys.key0)
                        .PushSigECDSA(keys.key1)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));

    // CHECKMULTISIGVERIFY 1-of-1 & Schnorr
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIGVERIFY << OP_1,
            "CHECKMULTISIGVERIFY Schnorr w/ no STRICTENC", 0)
            .Num(0)
            .PushSigSchnorr(keys.key0)
            .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIGVERIFY << OP_1,
            "CHECKMULTISIGVERIFY Schnorr w/ STRICTENC", SCRIPT_VERIFY_STRICTENC)
            .Num(0)
            .PushSigSchnorr(keys.key0)
            .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));

    // Test damaged Schnorr signatures
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG << OP_NOT, "Schnorr P2PK, bad sig", 0)
            .PushSigSchnorr(keys.key0)
            .DamagePush(10));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG << OP_NOT,
                        "Schnorr P2PK, bad sig STRICTENC", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key0)
                        .DamagePush(10));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG << OP_NOT,
                        "Schnorr P2PK, bad sig NULLFAIL", SCRIPT_VERIFY_NULLFAIL)
                        .PushSigSchnorr(keys.key0)
                        .DamagePush(10)
                        .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));

    // Make sure P2PKH works with Schnorr
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
                        "Schnorr P2PKH", 0)
                        .PushSigSchnorr(keys.key1)
                        .Push(keys.pubkey1C));

    // Test of different pubkey encodings with Schnnor
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
                        "Schnorr P2PK with compressed pubkey", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key0, SigHashType()));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                        "Schnorr P2PK with uncompressed pubkey", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key0, SigHashType()));
    tests.push_back(
        TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG, "Schnorr P2PK with uncompressed pubkey but "
                                                                            "COMPRESSED_PUBKEYTYPE set",
            SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE)
            .PushSigSchnorr(keys.key0, SigHashType())
            .SetScriptError(SCRIPT_ERR_NONCOMPRESSED_PUBKEY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                        "Schnorr P2PK with hybrid pubkey", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key0, SigHashType())
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                        "Schnorr P2PK with hybrid pubkey but no STRICTENC", 0)
                        .PushSigSchnorr(keys.key0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                        "Schnorr P2PK NOT with damaged hybrid pubkey but no STRICTENC", 0)
                        .PushSigSchnorr(keys.key0)
                        .DamagePush(10));

    // Ensure sighash types get checked with schnorr
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                        "Schnorr P2PK with undefined basehashtype and STRICTENC", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key1, SigHashType(5))
                        .SetScriptError(SCRIPT_ERR_SIG_HASHTYPE));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey0.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
                        "Schnorr P2PKH with invalid sighashtype but no STRICTENC", 0)
                        .PushSigSchnorr(keys.key0, SigHashType(0x21), 0, 0)
                        .Push(keys.pubkey0));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey0.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
                        "Schnorr P2PKH with invalid sighashtype and STRICTENC", SCRIPT_VERIFY_STRICTENC)
                        .PushSigSchnorr(keys.key0, SigHashType(0x21), (CAmount)0, SCRIPT_VERIFY_STRICTENC)
                        .Push(keys.pubkey0)
                        .SetScriptError(SCRIPT_ERR_SIG_HASHTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG, "Schnorr P2PK anyonecanpay", 0)
                        .PushSigSchnorr(keys.key1, SigHashType().withAnyoneCanPay()));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                        "Schnorr P2PK anyonecanpay marked with normal hashtype", 0)
                        .PushSigSchnorr(keys.key1, SigHashType().withAnyoneCanPay())
                        .EditPush(64, "81", "01")
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG, "Schnorr P2PK with forkID",
                        SCRIPT_VERIFY_STRICTENC | SCRIPT_ENABLE_SIGHASH_FORKID)
                        .PushSigSchnorr(keys.key1, SigHashType().withForkId()));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                        "Schnorr P2PK with non-forkID sig", SCRIPT_VERIFY_STRICTENC | SCRIPT_ENABLE_SIGHASH_FORKID)
                        .PushSigSchnorr(keys.key1)
                        .SetScriptError(SCRIPT_ERR_MUST_USE_FORKID));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                        "Schnorr P2PK with cheater forkID bit", SCRIPT_VERIFY_STRICTENC | SCRIPT_ENABLE_SIGHASH_FORKID)
                        .PushSigSchnorr(keys.key1)
                        .EditPush(64, "01", "41")
                        .SetScriptError(SCRIPT_ERR_EVAL_FALSE));

    {
        // There is a point with x = 7 + order but not x = 7.
        // Since r = x mod order, this can have valid signatures, as
        // demonstrated here.
        std::vector<uint8_t> rdata{7};
        std::vector<uint8_t> sdata{7};
        tests.push_back(
            TestBuilder(CScript() << OP_CHECKSIG, "recovered-pubkey CHECKSIG 7,7 (wrapped r)", SCRIPT_VERIFY_STRICTENC)
                .PushECDSASigFromParts(rdata, sdata)
                .PushECDSARecoveredPubKey(rdata, sdata));
    }
    {
        // Arbitrary r value that is 29 bytes long, to give room for varying
        // the length of s:
        std::vector<uint8_t> rdata = ParseHex("776879206d757374207765207375666665722077697468206563647361");
        std::vector<uint8_t> sdata(58 - rdata.size() - 1, 33);
        tests.push_back(
            TestBuilder(CScript() << OP_CHECKSIG, "recovered-pubkey CHECKSIG with 63-byte DER", SCRIPT_VERIFY_STRICTENC)
                .PushECDSASigFromParts(rdata, sdata)
                .PushECDSARecoveredPubKey(rdata, sdata));
    }
    {
        // 64-byte ECDSA sig dose not works
        std::vector<uint8_t> rdata = ParseHex("776879206d757374207765207375666665722077697468206563647361");
        std::vector<uint8_t> sdata(58 - rdata.size(), 33);
        tests.push_back(TestBuilder(CScript() << OP_CHECKSIG, "recovered-pubkey CHECKSIG with 64-byte DER; schnorrflag",
                            SCRIPT_VERIFY_STRICTENC)
                            .PushECDSASigFromParts(rdata, sdata)
                            .PushECDSARecoveredPubKey(rdata, sdata)
                            .SetScriptError(SCRIPT_ERR_EVAL_FALSE));
    }
    {
        // Try 64-byte ECDSA sig again, in multisig.
        std::vector<uint8_t> rdata = ParseHex("776879206d757374207765207375666665722077697468206563647361");
        std::vector<uint8_t> sdata(58 - rdata.size(), 33);
        tests.push_back(TestBuilder(CScript() << OP_1 << OP_SWAP << OP_1 << OP_CHECKMULTISIG,
                            "recovered-pubkey CHECKMULTISIG with 64-byte DER", SCRIPT_VERIFY_STRICTENC)
                            .Num(0)
                            .PushECDSASigFromParts(rdata, sdata)
                            .PushECDSARecoveredPubKey(rdata, sdata)
                            .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));
    }

    // New-multisig tests follow. New multisig will activate with a bunch of
    // related flags active from other upgrades, so we do tests with this group
    // of flags turned on:
    uint32_t newmultisigflags =
        SCRIPT_ENABLE_SCHNORR_MULTISIG | SCRIPT_VERIFY_NULLFAIL | SCRIPT_VERIFY_MINIMALDATA | SCRIPT_VERIFY_STRICTENC;

    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                        "CHECKMULTISIG 2-of-3 w/ SCHNORR_MULTISIG "
                        "(return-false still valid via legacy mode)",
                        newmultisigflags)
                        .Num(0)
                        .Num(0)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << OP_0 << OP_0 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 0-of-0 w/ SCHNORR_MULTISIG", newmultisigflags)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << OP_0 << OP_0 << OP_CHECKMULTISIGVERIFY,
                        "CHECKMULTISIGVERIFY 0-of-0 w/ SCHNORR_MULTISIG", newmultisigflags)
                        .Add(CScript() << OP_1)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << OP_0 << OP_0 << OP_CHECKMULTISIGVERIFY,
                        "CHECKMULTISIG 0-of-0 w/ SCHNORR_MULTISIG 1 bit", newmultisigflags)
                        .Num(1)
                        .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));
    tests.push_back(TestBuilder(CScript() << OP_0 << OP_0 << OP_CHECKMULTISIGVERIFY,
                        "CHECKMULTISIG 0-of-0 w/ SCHNORR_MULTISIG all bits", newmultisigflags)
                        .Num(0xffffffff)
                        .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));
    tests.push_back(TestBuilder(CScript() << OP_0 << OP_0 << OP_CHECKMULTISIGVERIFY,
                        "CHECKMULTISIG 0-of-0 w/ SCHNORR_MULTISIG overflow bit", newmultisigflags)
                        .NumULL(0x100000000ULL)
                        .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));

    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(ParseHex("BEEF")) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 0-of-1 w/ SCHNORR_MULTISIG, null dummy", newmultisigflags)
                        .Num(0));

    // Tests of schnorr checkmultisig actually turned on (flag on & dummy element is not null).
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 1-of-1 Schnorr", newmultisigflags)
                        .Num(0b1)
                        .PushSigSchnorr(keys.key0));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIGVERIFY,
                        "CHECKMULTISIGVERIFY 1-of-1 Schnorr", newmultisigflags)
                        .Add(CScript() << OP_1)
                        .Num(0b1)
                        .PushSigSchnorr(keys.key0));

    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 1-of-1 Schnorr, nonminimal bits", newmultisigflags)
                        .Push("0100")
                        .PushSigSchnorr(keys.key0)
                        .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 3-of-3 Schnorr", newmultisigflags)
                        .Num(0b111)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 3-of-3 Schnorr bad sig", newmultisigflags)
                        .Num(0b111)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key0)
                        .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(
        TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                              << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_0 << OP_EQUAL,
            "CHECKMULTISIG 3-of-3 Schnorr expected multisig fail", newmultisigflags)
            .Num(0)
            .Add(CScript() << OP_0 << OP_0 << OP_0));
    tests.push_back(TestBuilder(CScript() << OP_4 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 4-of-3 Schnorr", newmultisigflags)
                        .Num(0b1111)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_SIG_COUNT));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 (110) Schnorr", newmultisigflags)
                        .Num(0b110)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 (101) Schnorr", newmultisigflags)
                        .Num(0b101)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 (011) Schnorr", newmultisigflags)
                        .Num(0b011)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 Schnorr, mismatched bits Schnorr", newmultisigflags)
                        .Num(0b011)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 Schnorr, all bits set", newmultisigflags)
                        .Num(0b111)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_INVALID_BIT_COUNT));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 Schnorr, extra high bit set", newmultisigflags)
                        .Num(0b1110)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .SetScriptError(SCRIPT_ERR_INVALID_BIT_RANGE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 Schnorr, too high bit set", newmultisigflags)
                        .Num(0b1010)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .SetScriptError(SCRIPT_ERR_INVALID_BIT_RANGE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 Schnorr, too few bits set", newmultisigflags)
                        .Num(0b010)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .SetScriptError(SCRIPT_ERR_INVALID_BIT_COUNT));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 Schnorr, with no bits set "
                        "(attempt to malleate return-false)",
                        newmultisigflags)
                        .Push("00")
                        .Num(0)
                        .Num(0)
                        .SetScriptError(SCRIPT_ERR_INVALID_BIT_COUNT));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG null dummy with schnorr sigs "
                        "(with SCHNORR_MULTISIG on)",
                        newmultisigflags)
                        .Num(0)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .SetScriptError(SCRIPT_ERR_SIG_BADLENGTH));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 Schnorr, misordered signatures", newmultisigflags)
                        .Num(0b011)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key0)
                        .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(
        TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << OP_DUP << OP_2DUP
                              << OP_2DUP << ToByteVector(keys.pubkey2C) << OP_8 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 2-of-8 Schnorr, right way to represent 0b10000001", newmultisigflags)
            .Num(-1)
            .PushSigSchnorr(keys.key0)
            .PushSigSchnorr(keys.key2));
    tests.push_back(
        TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << OP_DUP << OP_2DUP
                              << OP_2DUP << ToByteVector(keys.pubkey2C) << OP_8 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 2-of-8 Schnorr, wrong way to represent 0b10000001", newmultisigflags)
            .Num(0b10000001)
            .PushSigSchnorr(keys.key0)
            .PushSigSchnorr(keys.key2)
            .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));
    tests.push_back(TestBuilder(CScript() << OP_OVER << OP_DUP << OP_DUP << OP_2DUP << OP_3DUP << OP_3DUP << OP_3DUP
                                          << OP_3DUP << 20 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_OVER << OP_DUP << OP_DUP << OP_2DUP
                                          << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << 20 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 20-of-20 Schnorr", newmultisigflags)
                        .Push("ffff0f")
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_OVER << OP_DUP << OP_DUP << OP_2DUP << OP_3DUP << OP_3DUP << OP_3DUP
                                          << OP_3DUP << 20 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_OVER << OP_DUP << OP_DUP << OP_2DUP
                                          << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << 20 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 20-of-20 Schnorr, checkbits +1", newmultisigflags)
                        .Push("000010")
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_INVALID_BIT_RANGE));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0C) << OP_DUP << ToByteVector(keys.pubkey1C) << OP_3DUP
                              << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << 21 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 1-of-21 Schnorr", newmultisigflags)
            .Push("000010")
            .PushSigSchnorr(keys.key0)
            .SetScriptError(SCRIPT_ERR_PUBKEY_COUNT));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << OP_DUP << OP_2DUP
                              << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << 20 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 1-of-20 Schnorr, first key", newmultisigflags)
            .Push("010000")
            .PushSigSchnorr(keys.key0));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << OP_DUP << OP_2DUP
                              << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << 20 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 1-of-20 Schnorr, first key, wrong endianness", newmultisigflags)
            .Push("000001")
            .PushSigSchnorr(keys.key0)
            .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0C) << OP_2DUP << OP_2DUP << OP_3DUP
                                          << OP_3DUP << OP_3DUP << OP_3DUP << OP_3DUP << 20 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 1-of-20 Schnorr, truncating zeros not allowed", newmultisigflags)
                        .Num(1)
                        .PushSigSchnorr(keys.key0)
                        .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0C) << OP_DUP << OP_2DUP << OP_3DUP << OP_3DUP
                              << OP_3DUP << OP_3DUP << OP_3DUP << ToByteVector(keys.pubkey1C) << 20 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 1-of-20 Schnorr, last key", newmultisigflags)
            .Push("000008")
            .PushSigSchnorr(keys.key1));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0C) << OP_DUP << OP_2DUP << OP_3DUP << OP_3DUP
                              << OP_3DUP << OP_3DUP << OP_3DUP << ToByteVector(keys.pubkey1C) << 20 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 1-of-20 Schnorr, last key, wrong endianness", newmultisigflags)
            .Push("080000")
            .PushSigSchnorr(keys.key1)
            .SetScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0C) << OP_DUP << OP_2DUP << OP_3DUP << OP_3DUP
                              << OP_3DUP << OP_3DUP << OP_3DUP << ToByteVector(keys.pubkey1C) << 20 << OP_CHECKMULTISIG,
            "CHECKMULTISIG 1-of-20 Schnorr, last key, "
            "truncating zeros not allowed",
            newmultisigflags)
            .Push("0800")
            .PushSigSchnorr(keys.key1)
            .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(ParseHex("BEEF")) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 (110) Schnorr, first key garbage", newmultisigflags)
                        .Num(0b110)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(ParseHex("BEEF")) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 (011) Schnorr, first key garbage", newmultisigflags)
                        .Num(0b011)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(ParseHex("BEEF")) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 (011) Schnorr, last key garbage", newmultisigflags)
                        .Num(0b011)
                        .PushSigSchnorr(keys.key0)
                        .PushSigSchnorr(keys.key1));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(ParseHex("BEEF")) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 2-of-3 (110) Schnorr, last key garbage", newmultisigflags)
                        .Num(0b110)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_0 << OP_0 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 0-of-0 with SCHNORR_MULTISIG, dummy must be null", newmultisigflags)
                        .Push("00")
                        .SetScriptError(SCRIPT_ERR_INVALID_BITFIELD_SIZE));
    tests.push_back(TestBuilder(CScript() << OP_0 << ToByteVector(ParseHex("BEEF")) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 0-of-1 with SCHNORR_MULTISIG, "
                        "dummy need not be null",
                        newmultisigflags)
                        .Push("00"));
    tests.push_back(
        TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIGVERIFY << OP_1,
            "OP_CHECKMULTISIGVERIFY Schnorr", newmultisigflags)
            .Num(0b1)
            .PushSigSchnorr(keys.key0));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0) << OP_1 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 1-of-1 ECDSA signature in Schnorr mode", newmultisigflags)
                        .Num(0b1)
                        .PushSigECDSA(keys.key0)
                        .SetScriptError(SCRIPT_ERR_SIG_NONSCHNORR));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                        "CHECKMULTISIG 3-of-3 Schnorr with mixed-in ECDSA signature", newmultisigflags)
                        .Num(0b111)
                        .PushSigECDSA(keys.key0)
                        .PushSigSchnorr(keys.key1)
                        .PushSigSchnorr(keys.key2)
                        .SetScriptError(SCRIPT_ERR_SIG_NONSCHNORR));

    std::set<std::string> tests_set;

    {
        UniValue json_tests = read_json(
            std::string(json_tests::script_tests, json_tests::script_tests + sizeof(json_tests::script_tests)));

        for (unsigned int idx = 0; idx < json_tests.size(); idx++)
        {
            const UniValue &tv = json_tests[idx];
            tests_set.insert(JSONPrettyPrint(tv.get_array()));
        }
    }
    UpdateJSONTests(tests);
}

BOOST_AUTO_TEST_CASE(script_json_test)
{
    // Read tests from test/data/script_tests.json
    // Format is an array of arrays
    // Inner arrays are [ ["wit"..., nValue]?, "scriptSig", "scriptPubKey", "flags", "expected_scripterror" ]
    // ... where scriptSig and scriptPubKey are stringified
    // scripts.
    UniValue tests =
        read_json(std::string(json_tests::script_tests, json_tests::script_tests + sizeof(json_tests::script_tests)));

    for (unsigned int idx = 0; idx < tests.size(); idx++)
    {
        UniValue test = tests[idx];
        string strTest = test.write();
        CAmount nValue = 0;
        unsigned int pos = 0;
        if (test.size() > 0 && test[pos].isArray())
        {
            nValue = AmountFromValue(test[pos][0]);
            pos++;
        }

        // Allow size > 3; extra stuff ignored (useful for comments)
        if (test.size() < 4 + pos)
        {
            if (test.size() != 1)
                BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        string scriptSigString = test[pos++].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        string scriptPubKeyString = test[pos++].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = ParseScriptFlags(test[pos++].get_str());
        int scriptError = ParseScriptError(test[pos++].get_str());

        DoTest(scriptPubKey, scriptSig, scriptflags, strTest, scriptError, nValue);
    }
}

BOOST_AUTO_TEST_CASE(script_PushData)
{
    // Check that PUSHDATA1, PUSHDATA2, and PUSHDATA4 create the same value on
    // the stack as the 1-75 opcodes do.
    static const unsigned char direct[] = {1, 0x5a};
    static const unsigned char pushdata1[] = {OP_PUSHDATA1, 1, 0x5a};
    static const unsigned char pushdata2[] = {OP_PUSHDATA2, 1, 0, 0x5a};
    static const unsigned char pushdata4[] = {OP_PUSHDATA4, 1, 0, 0, 0, 0x5a};

    ScriptError err;
    vector<vector<unsigned char> > directStack;
    BOOST_CHECK(EvalScript(directStack, CScript(&direct[0], &direct[sizeof(direct)]), SCRIPT_VERIFY_P2SH,
        MAX_OPS_PER_SCRIPT, BaseSignatureChecker(), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    vector<vector<unsigned char> > pushdata1Stack;
    BOOST_CHECK(EvalScript(pushdata1Stack, CScript(&pushdata1[0], &pushdata1[sizeof(pushdata1)]), SCRIPT_VERIFY_P2SH,
        MAX_OPS_PER_SCRIPT, BaseSignatureChecker(), &err));
    BOOST_CHECK(pushdata1Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    vector<vector<unsigned char> > pushdata2Stack;
    BOOST_CHECK(EvalScript(pushdata2Stack, CScript(&pushdata2[0], &pushdata2[sizeof(pushdata2)]), SCRIPT_VERIFY_P2SH,
        MAX_OPS_PER_SCRIPT, BaseSignatureChecker(), &err));
    BOOST_CHECK(pushdata2Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    vector<vector<unsigned char> > pushdata4Stack;
    BOOST_CHECK(EvalScript(pushdata4Stack, CScript(&pushdata4[0], &pushdata4[sizeof(pushdata4)]), SCRIPT_VERIFY_P2SH,
        MAX_OPS_PER_SCRIPT, BaseSignatureChecker(), &err));
    BOOST_CHECK(pushdata4Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
}

CScript sign_multisig(const CScript &scriptPubKey,
    const std::vector<CKey> &keys,
    const CTransaction &transaction,
    CAmount amt)
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

BOOST_AUTO_TEST_CASE(script_CHECKMULTISIG12)
{
    ScriptError err;
    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);

    CScript scriptPubKey12;
    scriptPubKey12 << OP_1 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << OP_2
                   << OP_CHECKMULTISIG;

    CMutableTransaction txFrom12 = BuildCreditingTransaction(scriptPubKey12, 1);
    CMutableTransaction txTo12 = BuildSpendingTransaction(CScript(), txFrom12);

    CScript goodsig1 = sign_multisig(scriptPubKey12, key1, CTransaction(txTo12), txFrom12.vout[0].nValue);
    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey12, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    txTo12.vout[0].nValue = 2;
    BOOST_CHECK(!VerifyScript(goodsig1, scriptPubKey12, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    CScript goodsig2 = sign_multisig(scriptPubKey12, key2, txTo12, txFrom12.vout[0].nValue);
    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey12, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    CScript badsig1 = sign_multisig(scriptPubKey12, key3, txTo12, txFrom12.vout[0].nValue);
    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey12, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));
}

BOOST_AUTO_TEST_CASE(script_CHECKMULTISIG23)
{
    ScriptError err;
    CKey key1, key2, key3, key4;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);
    key4.MakeNewKey(false);

    CScript scriptPubKey23;
    scriptPubKey23 << OP_2 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey())
                   << ToByteVector(key3.GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom23 = BuildCreditingTransaction(scriptPubKey23, 0);
    CMutableTransaction txTo23 = BuildSpendingTransaction(CScript(), txFrom23);

    std::vector<CKey> keys;
    keys.push_back(key1);
    keys.push_back(key2);
    CScript goodsig1 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key1);
    keys.push_back(key3);
    CScript goodsig2 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key2);
    keys.push_back(key3);
    CScript goodsig3 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(VerifyScript(goodsig3, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key2);
    keys.push_back(key2); // Can't re-use sig
    CScript badsig1 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key2);
    keys.push_back(key1); // sigs must be in correct order
    CScript badsig2 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(!VerifyScript(badsig2, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key3);
    keys.push_back(key2); // sigs must be in correct order
    CScript badsig3 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(!VerifyScript(badsig3, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key4);
    keys.push_back(key2); // sigs must match pubkeys
    CScript badsig4 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(!VerifyScript(badsig4, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key1);
    keys.push_back(key4); // sigs must match pubkeys
    CScript badsig5 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(!VerifyScript(badsig5, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    keys.clear(); // Must have signatures
    CScript badsig6 = sign_multisig(scriptPubKey23, keys, txTo23, txFrom23.vout[0].nValue);
    BOOST_CHECK(!VerifyScript(badsig6, scriptPubKey23, flags, MAX_OPS_PER_SCRIPT,
        MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_INVALID_STACK_OPERATION, ScriptErrorString(err));
}

BOOST_AUTO_TEST_CASE(script_combineSigs)
{
    // Test the CombineSignatures function
    CAmount amount = 0;
    CBasicKeyStore keystore;
    vector<CKey> keys;
    vector<CPubKey> pubkeys;
    for (int i = 0; i < 3; i++)
    {
        CKey key;
        key.MakeNewKey(i % 2 == 1);
        keys.push_back(key);
        pubkeys.push_back(key.GetPubKey());
        keystore.AddKey(key);
    }

    CMutableTransaction txFrom = BuildCreditingTransaction(GetScriptForDestination(keys[0].GetPubKey().GetID()), 0);
    CMutableTransaction txTo = BuildSpendingTransaction(CScript(), txFrom);
    CScript &scriptPubKey = txFrom.vout[0].scriptPubKey;
    CScript &scriptSig = txTo.vin[0].scriptSig;

    CScript empty;
    CScript combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, empty);
    BOOST_CHECK(combined.empty());

    // Single signature case:
    SignSignature(keystore, txFrom, txTo, 0); // changes scriptSig
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), scriptSig, empty);
    BOOST_CHECK(combined == scriptSig);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, scriptSig);
    BOOST_CHECK(combined == scriptSig);
    CScript scriptSigCopy = scriptSig;
    // Signing again will give a different, valid signature:
    SignSignature(keystore, txFrom, txTo, 0);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), scriptSigCopy, scriptSig);
    BOOST_CHECK(combined == scriptSigCopy || combined == scriptSig);

    // P2SH, single-signature case:
    CScript pkSingle;
    pkSingle << ToByteVector(keys[0].GetPubKey()) << OP_CHECKSIG;
    keystore.AddCScript(pkSingle);
    scriptPubKey = GetScriptForDestination(CScriptID(pkSingle));
    SignSignature(keystore, txFrom, txTo, 0);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), scriptSig, empty);
    BOOST_CHECK(combined == scriptSig);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, scriptSig);
    BOOST_CHECK(combined == scriptSig);
    scriptSigCopy = scriptSig;
    SignSignature(keystore, txFrom, txTo, 0);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), scriptSigCopy, scriptSig);
    BOOST_CHECK(combined == scriptSigCopy || combined == scriptSig);
    // dummy scriptSigCopy with placeholder, should always choose non-placeholder:
    scriptSigCopy = CScript() << OP_0 << vector<unsigned char>(pkSingle.begin(), pkSingle.end());
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), scriptSigCopy, scriptSig);
    BOOST_CHECK(combined == scriptSig);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), scriptSig, scriptSigCopy);
    BOOST_CHECK(combined == scriptSig);

    // Hardest case:  Multisig 2-of-3
    scriptPubKey = GetScriptForMultisig(2, pubkeys);
    keystore.AddCScript(scriptPubKey);
    SignSignature(keystore, txFrom, txTo, 0);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), scriptSig, empty);
    BOOST_CHECK(combined == scriptSig);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, scriptSig);
    BOOST_CHECK(combined == scriptSig);

    // A couple of partially-signed versions:
    vector<unsigned char> sig1;
    uint256 hash1 = SignatureHash(scriptPubKey, txTo, 0, SIGHASH_ALL | SIGHASH_FORKID, 0);
    BOOST_CHECK(hash1 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(keys[0].SignECDSA(hash1, sig1));
    sig1.push_back(SIGHASH_ALL | SIGHASH_FORKID);
    vector<unsigned char> sig2;
    uint256 hash2 = SignatureHash(scriptPubKey, txTo, 0, SIGHASH_NONE | SIGHASH_FORKID, 0);
    BOOST_CHECK(hash2 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(keys[1].SignECDSA(hash2, sig2));
    sig2.push_back(SIGHASH_NONE | SIGHASH_FORKID);
    vector<unsigned char> sig3;
    uint256 hash3 = SignatureHash(scriptPubKey, txTo, 0, SIGHASH_SINGLE | SIGHASH_FORKID, 0);
    BOOST_CHECK(hash3 != SIGNATURE_HASH_ERROR);
    BOOST_CHECK(keys[2].SignECDSA(hash3, sig3));
    sig3.push_back(SIGHASH_SINGLE | SIGHASH_FORKID);

    // Not fussy about order (or even existence) of placeholders or signatures:
    CScript partial1a = CScript() << OP_0 << sig1 << OP_0;
    CScript partial1b = CScript() << OP_0 << OP_0 << sig1;
    CScript partial2a = CScript() << OP_0 << sig2;
    CScript partial2b = CScript() << sig2 << OP_0;
    CScript partial3a = CScript() << sig3;
    CScript partial3b = CScript() << OP_0 << OP_0 << sig3;
    CScript partial3c = CScript() << OP_0 << sig3 << OP_0;
    CScript complete12 = CScript() << OP_0 << sig1 << sig2;
    CScript complete13 = CScript() << OP_0 << sig1 << sig3;
    CScript complete23 = CScript() << OP_0 << sig2 << sig3;

    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial1a, partial1b);
    BOOST_CHECK(combined == partial1a);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial1a, partial2a);
    BOOST_CHECK(combined == complete12);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial2a, partial1a);
    BOOST_CHECK(combined == complete12);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial1b, partial2b);
    BOOST_CHECK(combined == complete12);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial3b, partial1b);
    BOOST_CHECK(combined == complete13);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial2a, partial3a);
    BOOST_CHECK(combined == complete23);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial3b, partial2b);
    BOOST_CHECK(combined == complete23);
    combined =
        CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), partial3b, partial3a);
    BOOST_CHECK(combined == partial3c);
}

BOOST_AUTO_TEST_CASE(script_standard_push)
{
    ScriptError err;
    for (int i = 0; i < 67000; i++)
    {
        CScript script;
        script << i;
        BOOST_CHECK_MESSAGE(script.IsPushOnly(), "Number " << i << " is not pure push.");
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << OP_1, SCRIPT_VERIFY_MINIMALDATA, MAX_OPS_PER_SCRIPT,
                                BaseSignatureChecker(), &err),
            "Number " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    }

    for (unsigned int i = 0; i <= MAX_SCRIPT_ELEMENT_SIZE; i++)
    {
        std::vector<unsigned char> data(i, '\111');
        CScript script;
        script << data;
        BOOST_CHECK_MESSAGE(script.IsPushOnly(), "Length " << i << " is not pure push.");
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << OP_1, SCRIPT_VERIFY_MINIMALDATA, MAX_OPS_PER_SCRIPT,
                                BaseSignatureChecker(), &err),
            "Length " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    }
}

BOOST_AUTO_TEST_CASE(script_IsPushOnly_on_invalid_scripts)
{
    // IsPushOnly returns false when given a script containing only pushes that
    // are invalid due to truncation. IsPushOnly() is consensus critical
    // because P2SH evaluation uses it, although this specific behavior should
    // not be consensus critical as the P2SH evaluation would fail first due to
    // the invalid push. Still, it doesn't hurt to test it explicitly.
    static const unsigned char direct[] = {1};
    BOOST_CHECK(!CScript(direct, direct + sizeof(direct)).IsPushOnly());
}

BOOST_AUTO_TEST_CASE(script_GetScriptAsm)
{
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_NOP2, true));
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY, true));
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_NOP2));
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY));

    string derSig("304502207fa7a6d1e0ee81132a269ad84e68d695483745cde8b541e3bf630749894e342a022100c1f7ab20e13e22fb95281a"
                  "870f3dcf38d782e53023ee313d741ad0cfbc0c5090");
    string pubKey("03b0da749730dc9b4b1f4a14d6902877a92541f5368778853d9c4a0cb7802dcfb2");
    vector<unsigned char> vchPubKey = ToByteVector(ParseHex(pubKey));

    BOOST_CHECK_EQUAL(
        derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(
        derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[ALL] " + pubKey,
        ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[NONE] " + pubKey,
        ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[SINGLE] " + pubKey,
        ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[ALL|ANYONECANPAY] " + pubKey,
        ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[NONE|ANYONECANPAY] " + pubKey,
        ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[SINGLE|ANYONECANPAY] " + pubKey,
        ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey, true));

    BOOST_CHECK_EQUAL(
        derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey));
    BOOST_CHECK_EQUAL(
        derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey));
    BOOST_CHECK_EQUAL(
        derSig + "01 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey));
    BOOST_CHECK_EQUAL(
        derSig + "02 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey));
    BOOST_CHECK_EQUAL(
        derSig + "03 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey));
    BOOST_CHECK_EQUAL(
        derSig + "81 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey));
    BOOST_CHECK_EQUAL(
        derSig + "82 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey));
    BOOST_CHECK_EQUAL(
        derSig + "83 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey));
}


class QuickAddress
{
public:
    QuickAddress()
    {
        secret.MakeNewKey(true);
        pubkey = secret.GetPubKey();
        addr = pubkey.GetID();
    }
    QuickAddress(const CKey &k)
    {
        secret = k;
        pubkey = secret.GetPubKey();
        addr = pubkey.GetID();
    }
    QuickAddress(unsigned char key) // make a very simple key for testing only
    {
        secret.MakeNewKey(true);
        unsigned char *c = (unsigned char *)secret.begin();
        *c = key;
        c++;
        for (int i = 1; i < 32; i++, c++)
        {
            *c = 0;
        }
        pubkey = secret.GetPubKey();
        addr = pubkey.GetID();
    }

    CKey secret;
    CPubKey pubkey;
    CKeyID addr; // 160 bit normal address
};

CTransaction tx1x1(const COutPoint &utxo,
    const CScript &txo,
    CAmount amt,
    const CKey &key,
    const CScript &prevOutScript,
    bool p2pkh = true)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = utxo;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = txo;
    tx.vout[0].nValue = amt;
    tx.vin[0].scriptSig = CScript();
    tx.nLockTime = 0;

    unsigned int sighashType = SIGHASH_ALL | SIGHASH_FORKID;
    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(prevOutScript, tx, 0, sighashType, amt, 0);
    BOOST_CHECK(hash != SIGNATURE_HASH_ERROR);
    if (!key.SignECDSA(hash, vchSig))
    {
        assert(0);
    }
    vchSig.push_back((unsigned char)sighashType);
    tx.vin[0].scriptSig << vchSig;
    if (p2pkh)
    {
        tx.vin[0].scriptSig << ToByteVector(key.GetPubKey());
    }

    return tx;
}


// I want to create a script test that includes some CHECKSIGVERIFY instructions,
// however I don't have a transaction to verify.  So instead of signing the hash of the
// tx, I sign the hash of the public key.
class SigPubkeyHashChecker : public BaseSignatureChecker
{
public:
    virtual bool CheckSig(const std::vector<unsigned char> &scriptSig,
        const std::vector<unsigned char> &vchPubKey,
        const CScript &scriptCode) const override
    {
        CPubKey pub = CPubKey(vchPubKey);
        uint256 hash = pub.GetHash();
        if (!pub.VerifyECDSA(hash, scriptSig))
            return false;
        return true;
    }

    bool CheckLockTime(const CScriptNum &nLockTime) const override { return false; }
    bool CheckSequence(const CScriptNum &nSequence) const override { return false; }
    virtual ~SigPubkeyHashChecker() {}
};


static CScript ScriptFromHex(const char *hex)
{
    std::vector<unsigned char> data = ParseHex(hex);
    return CScript(data.begin(), data.end());
}


BOOST_AUTO_TEST_CASE(script_FindAndDelete)
{
    // Exercise the FindAndDelete functionality
    CScript s;
    CScript d;
    CScript expect;

    s = CScript() << OP_1 << OP_2;
    d = CScript(); // delete nothing should be a no-op
    expect = s;
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 0);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_1 << OP_2 << OP_3;
    d = CScript() << OP_2;
    expect = CScript() << OP_1 << OP_3;
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 1);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_3 << OP_1 << OP_3 << OP_3 << OP_4 << OP_3;
    d = CScript() << OP_3;
    expect = CScript() << OP_1 << OP_4;
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 4);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff03"); // PUSH 0x02ff03 onto stack
    d = ScriptFromHex("0302ff03");
    expect = CScript();
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 1);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff030302ff03"); // PUSH 0x2ff03 PUSH 0x2ff03
    d = ScriptFromHex("0302ff03");
    expect = CScript();
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 2);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff030302ff03");
    d = ScriptFromHex("02");
    expect = s; // FindAndDelete matches entire opcodes
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 0);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff030302ff03");
    d = ScriptFromHex("ff");
    expect = s;
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 0);
    BOOST_CHECK(s == expect);

    // This is an odd edge case: strip of the push-three-bytes
    // prefix, leaving 02ff03 which is push-two-bytes:
    s = ScriptFromHex("0302ff030302ff03");
    d = ScriptFromHex("03");
    expect = CScript() << ParseHex("ff03") << ParseHex("ff03");
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 2);
    BOOST_CHECK(s == expect);

    // Byte sequence that spans multiple opcodes:
    s = ScriptFromHex("02feed5169"); // PUSH(0xfeed) OP_1 OP_VERIFY
    d = ScriptFromHex("feed51");
    expect = s;
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 0); // doesn't match 'inside' opcodes
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("02feed5169"); // PUSH(0xfeed) OP_1 OP_VERIFY
    d = ScriptFromHex("02feed51");
    expect = ScriptFromHex("69");
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 1);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("516902feed5169");
    d = ScriptFromHex("feed51");
    expect = s;
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 0);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("516902feed5169");
    d = ScriptFromHex("02feed51");
    expect = ScriptFromHex("516969");
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 1);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_0 << OP_0 << OP_1 << OP_1;
    d = CScript() << OP_0 << OP_1;
    expect = CScript() << OP_0 << OP_1; // FindAndDelete is single-pass
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 1);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_0 << OP_0 << OP_1 << OP_0 << OP_1 << OP_1;
    d = CScript() << OP_0 << OP_1;
    expect = CScript() << OP_0 << OP_1; // FindAndDelete is single-pass
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 2);
    BOOST_CHECK(s == expect);

    // Another weird edge case:
    // End with invalid push (not enough data)...
    s = ScriptFromHex("0003feed");
    d = ScriptFromHex("03feed"); // ... can remove the invalid push
    expect = ScriptFromHex("00");
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 1);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0003feed");
    d = ScriptFromHex("00");
    expect = ScriptFromHex("03feed");
    BOOST_CHECK_EQUAL(s.FindAndDelete(d), 1);
    BOOST_CHECK(s == expect);
}

BOOST_AUTO_TEST_CASE(IsWitnessProgram)
{
    // Valid version: [0,16]
    // Valid program_len: [2,40]
    for (int version = -1; version <= 17; version++)
    {
        for (unsigned int program_len = 1; program_len <= 41; program_len++)
        {
            CScript script;
            std::vector<uint8_t> program(program_len, '\42');
            int parsed_version;
            std::vector<uint8_t> parsed_program;
            script << version << program;
            bool result = script.IsWitnessProgram(parsed_version, parsed_program);
            bool expected = version >= 0 && version <= 16 && program_len >= 2 && program_len <= 40;
            BOOST_CHECK_EQUAL(result, expected);
            if (result)
            {
                BOOST_CHECK_EQUAL(version, parsed_version);
                BOOST_CHECK(program == parsed_program);
            }
        }
    }
    // Tests with 1 and 3 stack elements
    {
        CScript script;
        script << OP_0;
        BOOST_CHECK_MESSAGE(!script.IsWitnessProgram(), "Failed IsWitnessProgram check with 1 stack element");
    }
    {
        CScript script;
        script << OP_0 << std::vector<uint8_t>(20, '\42') << OP_1;
        BOOST_CHECK_MESSAGE(!script.IsWitnessProgram(), "Failed IsWitnessProgram check with 3 stack elements");
    }
}

BOOST_AUTO_TEST_CASE(script_debugger)
{
    CScript testScript = CScript() << 0 << 1;
    CScript testRedeemScript = CScript() << OP_IF << OP_IF << 1 << OP_ELSE << 2 << OP_ENDIF << OP_ELSE << 3 << OP_ENDIF;
    BaseSignatureChecker sigChecker;
    ScriptMachine sm(0, sigChecker, 0xffffffff, 0xffffffff);

    bool result = sm.Eval(testScript);
    BOOST_CHECK(result);
    sm.BeginStep(testRedeemScript);
    while (sm.isMoreSteps())
    {
        unsigned int pos = sm.getPos();
        auto info = sm.Peek();
        if (pos == 4)
        {
            BOOST_CHECK(std::get<0>(info) == true);
            BOOST_CHECK(std::get<1>(info) == OP_2);
        }
        // you could print stepping info:
        // printf("pos %d: %s %s datalen: %d stack len: %d\n", pos, std::get<0>(info) ? "running" : "skipping",
        //       GetOpName(std::get<1>(info)), std::get<2>(info).size(), sm.getStack().size());
        if (!sm.Step())
            break;
    }
    sm.EndStep();

    auto finalStack = sm.getStack();
    BOOST_CHECK(finalStack.size() == 1);
    BOOST_CHECK(finalStack[0][0] == 2);

    testRedeemScript = CScript() << OP_IF << OP_IF << OP_FROMALTSTACK << OP_ELSE << OP_INVALIDOPCODE << OP_ENDIF
                                 << OP_ELSE << 3 << OP_ENDIF;
    sm.Reset();
    sm.Eval(CScript() << 0 << 1);
    result = sm.Eval(testRedeemScript);
    BOOST_CHECK(result == false); // should get stuck at OP_INVALIDOPCODE
    auto error = sm.getError();
    BOOST_CHECK(error == SCRIPT_ERR_BAD_OPCODE);
    unsigned int pos = sm.getPos();
    BOOST_CHECK(pos == 5);

    sm.Reset();
    sm.Eval(CScript() << 1 << 1);
    result = sm.Eval(testRedeemScript);
    BOOST_CHECK(result == false); // should get stuck at OP_FROMALTSTACK, because nothing in altstack
    error = sm.getError();
    BOOST_CHECK(error == SCRIPT_ERR_INVALID_ALTSTACK_OPERATION);
    pos = sm.getPos();
    BOOST_CHECK(pos == 3);

    std::vector<StackDataType> altStack;
    StackDataType item;
    item.push_back(4);
    altStack.push_back(item);
    sm.Reset();
    sm.Eval(CScript() << 1 << 1);
    sm.setAltStack(altStack);
    result = sm.Eval(testRedeemScript);
    BOOST_CHECK(result == true); // should work because altstack was seeded
    auto &stk = sm.getStack();
    BOOST_CHECK(stk.size() == 1);
    BOOST_CHECK(stk[0][0] == 4);
}

BOOST_AUTO_TEST_CASE(script_can_append_self)
{
    CScript s, d;

    s = ScriptFromHex("00");
    s += s;
    d = ScriptFromHex("0000");
    BOOST_CHECK(s == d);

    // check doubling a script that's large enough to require reallocation
    static const char hex[] = "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504"
                              "e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f";
    s = CScript() << ParseHex(hex) << OP_CHECKSIG;
    d = CScript() << ParseHex(hex) << OP_CHECKSIG << ParseHex(hex) << OP_CHECKSIG;
    s += s;
    BOOST_CHECK(s == d);
}

BOOST_AUTO_TEST_SUITE_END()
