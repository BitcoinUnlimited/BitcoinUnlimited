// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"

#include "core_io.h" // freeze for debug only
#include "pubkey.h"
#include "script/script.h"
#include "util.h"
#include "utilstrencodings.h"

using namespace std;

typedef vector<uint8_t> valtype;

bool fAcceptDatacarrier = DEFAULT_ACCEPT_DATACARRIER;
unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

CScriptID::CScriptID(const CScript &in) : uint160(Hash160(in.begin(), in.end())) {}
const char *GetTxnOutputType(txnouttype t)
{
    switch (t)
    {
    case TX_NONSTANDARD:
        return "nonstandard";
    case TX_PUBKEY:
        return "pubkey";
    case TX_PUBKEYHASH:
        return "pubkeyhash";
    case TX_SCRIPTHASH:
        return "scripthash";
    case TX_MULTISIG:
        return "multisig";
    case TX_CLTV:
        return "cltv"; // CLTV HODL Freeze
    case TX_LABELPUBLIC:
        return "publiclabel";
    case TX_NULL_DATA:
        return "nulldata";
    }
    return nullptr;
}

static bool MatchPayToPubkey(const CScript &script, valtype &pubkey)
{
    // Standard tx, sender provides pubkey, receiver adds signature
    // Template: "CScript() << OP_PUBKEY << OP_CHECKSIG"

    if (script.size() == CPubKey::PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::PUBLIC_KEY_SIZE &&
        script.back() == OP_CHECKSIG)
    {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::PUBLIC_KEY_SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }

    if (script.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE &&
        script.back() == OP_CHECKSIG)
    {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }
    return false;
}

static bool MatchPayToPubkeyHash(const CScript &script, valtype &pubkeyhash)
{
    // Bitcoin address tx, sender provides hash of pubkey, receiver provides signature and pubkey
    // Template: "OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG"

    if (script.size() == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 &&
        script[2] == CPubKey::PUBLIC_KEY_HASH160_SIZE && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG)
    {
        pubkeyhash = valtype(script.begin() + 3, script.begin() + CPubKey::PUBLIC_KEY_HASH160_SIZE + 3);
        return true;
    }
    return false;
}

/** Test for "small positive integer" script opcodes - OP_1 through OP_16. */
static constexpr bool IsSmallInteger(opcodetype opcode) { return opcode >= OP_1 && opcode <= OP_16; }
/** Check if a script is of the TX_LABELPUBLIC type
 *
 * param script const CScript& a reference to the script to evaluate
 * param dataCarriage std::vector<valtype> a reference to vector containing
 *                  2 elements: the big int valu used to mark the txns and the
 *                  actual string to display along with the txs.
 *
 * return boolean
 */
static bool MatchLabelPublic(const CScript &script, std::vector<valtype> &dataCarriage)
{
    // LabelPublc OP_RETURN data size format small
    // Template: "CScript() << OP_RETURN << OP_BIGINTEGER << OP_DATA"
    // data deve contenere il OP_DATA
    if (script.size() < 1 || script[0] != OP_RETURN || !script.IsPushOnly(script.begin() + 1))
    {
        return false;
    }

    valtype data;
    opcodetype opcode;
    CScript::const_iterator s = script.begin() + 1;
    script.GetOp(s, opcode, data);

    uint8_t declaredLen = 0;

    try
    {
        CScriptNum dataId(data, true, 5);
        if (IsSmallInteger(opcode))
        {
            declaredLen = CScript::DecodeOP_N(opcode);
        }
        if (dataId.getint() > 0)
        {
            declaredLen = dataId.getint();
        }
        if (declaredLen == 0)
        {
            // this is not the expected format for LABELPUBLIC
            return false;
        }
        dataCarriage.emplace_back(data);
    }
    catch (scriptnum_error &)
    {
        return false;
    }

    if (script.GetOp(s, opcode, data))
    {
        std::string labelPublic = std::string(data.begin(), data.end());
        if (labelPublic.size() == declaredLen)
        {
            dataCarriage.emplace_back(std::move(data));
            return true;
        }
    }

    return false;
}

static bool MatchFreezeCLTV(const CScript &script, std::vector<valtype> &pubkeys)
{
    // Freeze tx using CLTV ; nFreezeLockTime CLTV DROP (0x21 pubkeys) checksig
    // {TX_CLTV, CScript() << OP_BIGINTEGER << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_PUBKEYS << OP_CHECKSIG},

    if ((script.size() < 1) || script.back() != OP_CHECKSIG)
    {
        return false;
    }

    valtype data;
    opcodetype opcode;
    CScript::const_iterator s = script.begin();
    script.GetOp(s, opcode, data);

    try
    {
        // extracting the bignum in a try-catch because if the provided number is not
        // a big int CScriptNum will raise an error.
        CScriptNum nLockFreezeTime(data, true, 5);

        pubkeys.emplace_back(data);
        if ((*s != OP_CHECKLOCKTIMEVERIFY) || (*(s + 1) != OP_DROP))
        {
            return false;
        }

        // starting from pubkeys (4/5 byte nlock time + 1 OP_CLTV + 1 OP_DROP)
        s = s + 2;
        if (!script.GetOp(s, opcode, data))
        {
            return false;
        }
        if (!CPubKey::ValidSize(data))
        {
            return false;
        }
        pubkeys.emplace_back(std::move(data));
        // after key extraction we should still have one byte which represent OP_CHECKSIG
        return (s + 1 == script.end());
    }
    catch (scriptnum_error &)
    {
        return false;
    }
}

static bool MatchMultisig(const CScript &script, unsigned int &required, std::vector<valtype> &pubkeys)
{
    // Sender provides N pubkeys, receivers provides M signatures
    // Template: "CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG"
    opcodetype opcode;
    valtype data;
    CScript::const_iterator it = script.begin();
    if (script.size() < 1 || script.back() != OP_CHECKMULTISIG)
    {
        return false;
    }

    if (!script.GetOp(it, opcode, data) || !IsSmallInteger(opcode))
    {
        return false;
    }
    required = CScript::DecodeOP_N(opcode);
    while (script.GetOp(it, opcode, data) && CPubKey::ValidSize(data))
    {
        if (opcode < 0 || opcode > OP_PUSHDATA4 || !CheckMinimalPush(data, opcode))
        {
            return false;
        }
        pubkeys.emplace_back(std::move(data));
    }
    if (!IsSmallInteger(opcode))
    {
        return false;
    }
    unsigned int keys = CScript::DecodeOP_N(opcode);
    if (pubkeys.size() != keys || keys < required)
    {
        return false;
    }
    return (it + 1 == script.end());
}


/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Solver(const CScript &scriptPubKey, txnouttype &typeRet, std::vector<valtype> &vSolutionsRet)
{
    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash())
    {
        typeRet = TX_SCRIPTHASH;
        vector<unsigned char> hashBytes(scriptPubKey.begin() + 2, scriptPubKey.begin() + 22);
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    std::vector<valtype> vData;
    // This need to go after general check about unspendable output (OP_RETURN)
    // otherwise all transactions of the TX_LABELPUBLIC type will be masked
    if (MatchLabelPublic(scriptPubKey, vData))
    {
        typeRet = TX_LABELPUBLIC;
        vSolutionsRet.insert(vSolutionsRet.end(), vData.begin(), vData.end());
        return true;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin() + 1))
    {
        typeRet = TX_NULL_DATA;
        return true;
    }

    std::vector<uint8_t> data;
    if (MatchPayToPubkey(scriptPubKey, data))
    {
        typeRet = TX_PUBKEY;
        vSolutionsRet.push_back(std::move(data));
        return true;
    }

    if (MatchPayToPubkeyHash(scriptPubKey, data))
    {
        typeRet = TX_PUBKEYHASH;
        vSolutionsRet.push_back(std::move(data));
        return true;
    }

    if (MatchFreezeCLTV(scriptPubKey, vData))
    {
        typeRet = TX_CLTV;
        vSolutionsRet.insert(vSolutionsRet.end(), vData.begin(), vData.end());
        return true;
    }

    unsigned int required;
    std::vector<std::vector<uint8_t> > keys;
    if (MatchMultisig(scriptPubKey, required, keys))
    {
        typeRet = TX_MULTISIG;
        // safe as required is in range 1..16
        vSolutionsRet.push_back({static_cast<uint8_t>(required)});
        vSolutionsRet.insert(vSolutionsRet.end(), keys.begin(), keys.end());
        // safe as size is in range 1..16
        vSolutionsRet.push_back({static_cast<uint8_t>(keys.size())});
        return true;
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

bool ExtractDestination(const CScript &scriptPubKey, CTxDestination &addressRet)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY)
    {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;
    }
    else if (whichType == TX_PUBKEYHASH)
    {
        addressRet = CKeyID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_SCRIPTHASH)
    {
        addressRet = CScriptID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_CLTV)
    {
        CPubKey pubKey(vSolutions[1]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}

bool ExtractDestinations(const CScript &scriptPubKey,
    txnouttype &typeRet,
    vector<CTxDestination> &addressRet,
    int &nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, typeRet, vSolutions))
        return false;
    if (typeRet == TX_NULL_DATA)
    {
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG)
    {
        nRequiredRet = vSolutions.front()[0];
        for (unsigned int i = 1; i < vSolutions.size() - 1; i++)
        {
            CPubKey pubKey(vSolutions[i]);
            if (!pubKey.IsValid())
                continue;

            CTxDestination address = pubKey.GetID();
            addressRet.push_back(address);
        }

        if (addressRet.empty())
            return false;
    }
    else
    {
        // Freeze TX_CLTV also here
        nRequiredRet = 1;
        CTxDestination address;
        if (!ExtractDestination(scriptPubKey, address))
            return false;
        addressRet.push_back(address);
    }

    return true;
}

namespace
{
class CScriptVisitor : public boost::static_visitor<bool>
{
private:
    CScript *script;

public:
    CScriptVisitor(CScript *scriptin) { script = scriptin; }
    bool operator()(const CNoDestination &dest) const
    {
        script->clear();
        return false;
    }

    bool operator()(const CKeyID &keyID) const
    {
        script->clear();
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const
    {
        script->clear();
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return true;
    }
};
}

CScript GetScriptForDestination(const CTxDestination &dest)
{
    CScript script;

    boost::apply_visitor(CScriptVisitor(&script), dest);
    return script;
}

CScript GetScriptForRawPubKey(const CPubKey &pubKey)
{
    return CScript() << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey> &keys)
{
    CScript script;

    script << CScript::EncodeOP_N(nRequired);
    for (const CPubKey &key : keys)
        script << ToByteVector(key);
    script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
    return script;
}

CScript GetScriptForFreeze(CScriptNum nFreezeLockTime, const CPubKey &pubKey)
{
    // TODO Perhaps add limit tests for nLockTime eg. 10 year max lock
    return CScript() << nFreezeLockTime << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                     << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

/*
 * Create an OP_RETURN script (thanks coinspark)
 *
 */
CScript GetScriptLabelPublic(const string &labelPublic)
{
    int sizeLabelPublic = labelPublic.size();

    CScript scriptDataPublic;

    if (sizeLabelPublic <= 0)
    {
        scriptDataPublic = CScript();
    }
    else
    {
        // length byte + data (https://en.bitcoin.it/wiki/Script);
        // scriptDataPublic = bytearray((sizeLabelPublic,))+ labelPublic;
        scriptDataPublic = CScript() << OP_RETURN << CScriptNum(sizeLabelPublic)
                                     << std::vector<unsigned char>(labelPublic.begin(), labelPublic.end());
    }
    return scriptDataPublic;
}

bool IsValidDestination(const CTxDestination &dest) { return dest.which() != 0; }
