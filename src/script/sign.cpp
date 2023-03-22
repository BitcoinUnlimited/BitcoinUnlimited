// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/sign.h"

#include "key.h"
#include "keystore.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "uint256.h"

using valtype = std::vector<uint8_t>;

TransactionSignatureCreator::TransactionSignatureCreator(const CKeyStore *keystoreIn,
    const CTransaction *txToIn,
    unsigned int nInIn,
    const CAmount &amountIn,
    uint32_t nHashTypeIn,
    uint32_t nSigTypeIn)
    : BaseSignatureCreator(keystoreIn), txTo(txToIn), nIn(nInIn), amount(amountIn), nHashType(nHashTypeIn),
      nSigType(nSigTypeIn),
      checker(txTo, nIn, amount, (nHashTypeIn & SIGHASH_FORKID) ? SCRIPT_ENABLE_SIGHASH_FORKID : 0)
{
}

bool TransactionSignatureCreator::CreateSig(std::vector<uint8_t> &vchSig,
    const CKeyID &address,
    const CScript &scriptCode) const
{
    CKey key;
    if (!keystore->GetKey(address, key))
    {
        return false;
    }

    uint256 hash = SignatureHash(scriptCode, *txTo, nIn, nHashType, amount);
    if (nSigType == SIGTYPE_ECDSA)
    {
        if (!key.SignECDSA(hash, vchSig))
        {
            return false;
        }
    }
    else if (nSigType == SIGTYPE_SCHNORR)
    {
        if (!key.SignSchnorr(hash, vchSig))
        {
            return false;
        }
    }
    else
    {
        LOGA("CreateSig(): Invalid signature type requested \n");
        return false;
    }
    vchSig.push_back((uint8_t)nHashType);
    return true;
}

static bool Sign1(const CKeyID &address,
    const BaseSignatureCreator &creator,
    const CScript &scriptCode,
    CScript &scriptSigRet)
{
    std::vector<uint8_t> vchSig;
    if (!creator.CreateSig(vchSig, address, scriptCode))
    {
        return false;
    }
    scriptSigRet << vchSig;
    return true;
}

static bool SignN(const std::vector<valtype> &multisigdata,
    const BaseSignatureCreator &creator,
    const CScript &scriptCode,
    CScript &scriptSigRet)
{
    int nSigned = 0;
    int nRequired = multisigdata.front()[0];
    for (unsigned int i = 1; i < multisigdata.size() - 1 && nSigned < nRequired; i++)
    {
        const valtype &pubkey = multisigdata[i];
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (Sign1(keyID, creator, scriptCode, scriptSigRet))
        {
            ++nSigned;
        }
    }
    return nSigned == nRequired;
}

/**
 * Sign scriptPubKey using signature made with creator.
 * Signatures are returned in scriptSigRet (or returns false if scriptPubKey can't be signed),
 * unless whichTypeRet is TX_SCRIPTHASH, in which case scriptSigRet is the redemption script.
 * Returns false if scriptPubKey could not be completely satisfied.
 */
static bool SignStep(const BaseSignatureCreator &creator,
    const CScript &scriptPubKey,
    CScript &scriptSigRet,
    txnouttype &whichTypeRet,
    uint32_t scriptFlags)
{
    scriptSigRet.clear();

    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, whichTypeRet, vSolutions, scriptFlags))
    {
        return false;
    }

    CKeyID keyID;
    switch (whichTypeRet)
    {
    // These are OP_RETURN unspendable outputs so they should never be an input that needs signing
    case TX_LABELPUBLIC:
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        return false;
    case TX_PUBKEY:
        keyID = CPubKey(vSolutions[0]).GetID();
        return Sign1(keyID, creator, scriptPubKey, scriptSigRet);

    case TX_CLTV:
        keyID = CPubKey(vSolutions[1]).GetID();
        return Sign1(keyID, creator, scriptPubKey, scriptSigRet);

    case TX_PUBKEYHASH:
        keyID = CKeyID(uint160(vSolutions[0]));
        if (!Sign1(keyID, creator, scriptPubKey, scriptSigRet))
        {
            return false;
        }
        else
        {
            CPubKey vch;
            creator.KeyStore().GetPubKey(keyID, vch);
            scriptSigRet << ToByteVector(vch);
        }
        return true;

    case TX_SCRIPTHASH:
    {
        ScriptID scriptid;
        if (vSolutions[0].size() == 20)
        {
            scriptid = uint160(vSolutions[0]); // p2sh_20
        }
        else if (vSolutions[0].size() == 32)
        {
            scriptid = uint256(vSolutions[0]); // p2sh_32
        }
        else
        {
            assert(!"Unexpected state in SignStep() for vSolutions[0]!"); // should never happen
        }
        return creator.KeyStore().GetCScript(scriptid, scriptSigRet);
    }

    case TX_MULTISIG:
        scriptSigRet << OP_0; // workaround CHECKMULTISIG bug
        return (SignN(vSolutions, creator, scriptPubKey, scriptSigRet));
    }

    return false;
}

bool ProduceSignature(const BaseSignatureCreator &creator,
    const CScript &fromPubKey,
    CScript &scriptSig,
    uint32_t scriptFlags)
{
    txnouttype whichType;
    if (!SignStep(creator, fromPubKey, scriptSig, whichType, scriptFlags))
    {
        return false;
    }

    if (whichType == TX_SCRIPTHASH)
    {
        // Solver returns the subscript that need to be evaluated;
        // the final scriptSig is the signatures from that
        // and then the serialized subscript:
        CScript subscript = scriptSig;

        txnouttype subType;
        bool fSolved = SignStep(creator, subscript, scriptSig, subType, scriptFlags) && subType != TX_SCRIPTHASH;
        // Append serialized subscript whether or not it is completely signed:
        scriptSig << valtype(subscript.begin(), subscript.end());
        if (!fSolved)
        {
            return false;
        }
    }

    // Test solution
    // We can hard-code maxOps because this client has no templates capable of producing and signing longer scripts.
    // Additionally, while this constant is currently being raised it will eventually settle to a very high const
    // value.  There is no reason to break layering by using the tweak only to take that out later.
    ScriptImportedState sis(&creator.Checker(), CTransactionRef(nullptr), std::vector<CTxOut>(), 0, 0);
    return VerifyScript(scriptSig, fromPubKey, scriptFlags, MAX_OPS_PER_SCRIPT, sis);
}

bool SignSignature(uint32_t scriptFlags,
    const CKeyStore &keystore,
    const CScript &fromPubKey,
    CMutableTransaction &txTo,
    unsigned int nIn,
    const CAmount &amount,
    uint32_t nHashType,
    uint32_t nSigType)
{
    assert(nIn < txTo.vin.size());
    CTxIn &txin = txTo.vin[nIn];

    CTransaction txToConst(txTo);
    TransactionSignatureCreator creator(&keystore, &txToConst, nIn, amount, nHashType, nSigType);

    return ProduceSignature(creator, fromPubKey, txin.scriptSig, scriptFlags);
}

bool SignSignature(uint32_t scriptFlags,
    const CKeyStore &keystore,
    const CTransaction &txFrom,
    CMutableTransaction &txTo,
    unsigned int nIn,
    uint32_t nHashType,
    uint32_t nSigType)
{
    assert(nIn < txTo.vin.size());
    CTxIn &txin = txTo.vin[nIn];
    assert(txin.prevout.n < txFrom.vout.size());
    const CTxOut &txout = txFrom.vout[txin.prevout.n];

    return SignSignature(scriptFlags, keystore, txout.scriptPubKey, txTo, nIn, txout.nValue, nHashType);
}

static CScript PushAll(const std::vector<valtype> &values)
{
    CScript result;
    for (const valtype &v : values)
    {
        result << v;
    }
    return result;
}

static CScript CombineMultisig(const CScript &scriptPubKey,
    const BaseSignatureChecker &checker,
    const std::vector<valtype> &vSolutions,
    const std::vector<valtype> &sigs1,
    const std::vector<valtype> &sigs2)
{
    // Combine all the signatures we've got:
    std::set<valtype> allsigs;
    for (const valtype &v : sigs1)
    {
        if (!v.empty())
        {
            allsigs.insert(v);
        }
    }
    for (const valtype &v : sigs2)
    {
        if (!v.empty())
        {
            allsigs.insert(v);
        }
    }

    // Build a map of pubkey -> signature by matching sigs to pubkeys:
    assert(vSolutions.size() > 1);
    unsigned int nSigsRequired = vSolutions.front()[0];
    unsigned int nPubKeys = vSolutions.size() - 2;
    std::map<valtype, valtype> sigs;
    for (const valtype &sig : allsigs)
    {
        for (unsigned int i = 0; i < nPubKeys; i++)
        {
            const valtype &pubkey = vSolutions[i + 1];
            if (sigs.count(pubkey))
            {
                continue; // Already got a sig for this pubkey
            }

            if (checker.CheckSig(sig, pubkey, scriptPubKey))
            {
                sigs[pubkey] = sig;
                break;
            }
        }
    }
    // Now build a merged CScript:
    unsigned int nSigsHave = 0;
    CScript result;
    result << OP_0; // pop-one-too-many workaround
    for (unsigned int i = 0; i < nPubKeys && nSigsHave < nSigsRequired; i++)
    {
        if (sigs.count(vSolutions[i + 1]))
        {
            result << sigs[vSolutions[i + 1]];
            ++nSigsHave;
        }
    }
    // Fill any missing with OP_0:
    for (unsigned int i = nSigsHave; i < nSigsRequired; i++)
        result << OP_0;

    return result;
}

static CScript CombineSignatures(const CScript &scriptPubKey,
    const BaseSignatureChecker &checker,
    const txnouttype txType,
    const std::vector<valtype> &vSolutions,
    std::vector<valtype> &sigs1,
    std::vector<valtype> &sigs2,
    const uint32_t flags)
{
    switch (txType)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        // Don't know anything about this, assume bigger one is correct:
        if (sigs1.size() >= sigs2.size())
        {
            return PushAll(sigs1);
        }
        return PushAll(sigs2);
    case TX_CLTV: // Freeze CLTV contains pubkey
    case TX_PUBKEY:
    case TX_PUBKEYHASH:
        // Signatures are bigger than placeholders or empty scripts:
        if (sigs1.empty() || sigs1[0].empty())
        {
            return PushAll(sigs2);
        }
        return PushAll(sigs1);
    case TX_SCRIPTHASH:
        if (sigs1.empty() || sigs1.back().empty())
        {
            return PushAll(sigs2);
        }
        else if (sigs2.empty() || sigs2.back().empty())
        {
            return PushAll(sigs1);
        }
        else
        {
            // Recur to combine:
            valtype spk = sigs1.back();
            CScript pubKey2(spk.begin(), spk.end());

            txnouttype txType2;
            std::vector<std::vector<uint8_t> > vSolutions2;
            Solver(pubKey2, txType2, vSolutions2, flags);
            sigs1.pop_back();
            sigs2.pop_back();
            CScript result = CombineSignatures(pubKey2, checker, txType2, vSolutions2, sigs1, sigs2, flags);
            result << spk;
            return result;
        }
    case TX_MULTISIG:
        return CombineMultisig(scriptPubKey, checker, vSolutions, sigs1, sigs2);
    // These are OP_RETURN unspendable outputs so they should never be an input that needs signing
    case TX_LABELPUBLIC:
        return CScript();
    }

    return CScript();
}

CScript CombineSignatures(const CScript &scriptPubKey,
    const BaseSignatureChecker &checker,
    const CScript &scriptSig1,
    const CScript &scriptSig2,
    const uint32_t flags)
{
    txnouttype txType;
    std::vector<std::vector<uint8_t> > vSolutions;
    Solver(scriptPubKey, txType, vSolutions, flags);

    std::vector<valtype> stack1;
    // scriptSig should have no ops in them, only data pushes.  Send MAX_OPS_PER_SCRIPT to mirror existing
    // behavior exactly.
    EvalScript(stack1, scriptSig1, SCRIPT_VERIFY_STRICTENC, MAX_OPS_PER_SCRIPT, ScriptImportedState());
    std::vector<valtype> stack2;
    EvalScript(stack2, scriptSig2, SCRIPT_VERIFY_STRICTENC, MAX_OPS_PER_SCRIPT, ScriptImportedState());

    return CombineSignatures(scriptPubKey, checker, txType, vSolutions, stack1, stack2, flags);
}

namespace
{
/** Dummy signature checker which accepts all signatures. */
class DummySignatureChecker : public BaseSignatureChecker
{
public:
    DummySignatureChecker() {}
    bool CheckSig(const std::vector<uint8_t> &scriptSig,
        const std::vector<uint8_t> &vchPubKey,
        const CScript &scriptCode) const override
    {
        return true;
    }
};
const DummySignatureChecker dummyChecker;
} // namespace

const BaseSignatureChecker &DummySignatureCreator::Checker() const { return dummyChecker; }
bool DummySignatureCreator::CreateSig(std::vector<uint8_t> &vchSig,
    const CKeyID &keyid,
    const CScript &scriptCode) const
{
    // Create a dummy signature that is a valid DER-encoding
    vchSig.assign(72, '\000');
    vchSig[0] = 0x30;
    vchSig[1] = 69;
    vchSig[2] = 0x02;
    vchSig[3] = 33;
    vchSig[4] = 0x01;
    vchSig[4 + 33] = 0x02;
    vchSig[5 + 33] = 32;
    vchSig[6 + 33] = 0x01;
    vchSig[6 + 33 + 32] = SIGHASH_ALL;
    return true;
}


template std::vector<uint8_t> signmessage(const std::vector<uint8_t> &data, const CKey &key);
template std::vector<uint8_t> signmessage(const std::string &data, const CKey &key);
