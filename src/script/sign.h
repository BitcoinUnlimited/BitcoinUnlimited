// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SIGN_H
#define BITCOIN_SCRIPT_SIGN_H

#include "hashwrapper.h"
#include "key.h"
#include "script/interpreter.h"

#include <vector>

class CKey;
class CKeyID;
class CKeyStore;
class CScript;
class CTransaction;

struct CMutableTransaction;

/** Virtual base class for signature creators. */
class BaseSignatureCreator
{
protected:
    const CKeyStore *keystore;

public:
    BaseSignatureCreator(const CKeyStore *keystoreIn) : keystore(keystoreIn) {}
    const CKeyStore &KeyStore() const { return *keystore; };
    virtual ~BaseSignatureCreator() {}
    virtual const BaseSignatureChecker &Checker() const = 0;

    /** Create a singular (non-script) signature. */
    virtual bool CreateSig(std::vector<unsigned char> &vchSig,
        const CKeyID &keyid,
        const CScript &scriptCode) const = 0;
};

/** A signature creator for transactions. */
class TransactionSignatureCreator : public BaseSignatureCreator
{
    const CTransaction *txTo;
    unsigned int nIn;
    CAmount amount;
    uint32_t nHashType;
    const TransactionSignatureChecker checker;

public:
    TransactionSignatureCreator(const CKeyStore *keystoreIn,
        const CTransaction *txToIn,
        unsigned int nInIn,
        const CAmount &amountIn,
        uint32_t nHashTypeIn = SIGHASH_ALL);
    const BaseSignatureChecker &Checker() const { return checker; }
    bool CreateSig(std::vector<unsigned char> &vchSig, const CKeyID &keyid, const CScript &scriptCode) const;
};

/** A signature creator that just produces 72-byte empty signatyres. */
class DummySignatureCreator : public BaseSignatureCreator
{
public:
    DummySignatureCreator(const CKeyStore *keystoreIn) : BaseSignatureCreator(keystoreIn) {}
    const BaseSignatureChecker &Checker() const;
    bool CreateSig(std::vector<unsigned char> &vchSig, const CKeyID &keyid, const CScript &scriptCode) const;
};

/** Produce a script signature using a generic signature creator. */
bool ProduceSignature(const BaseSignatureCreator &creator, const CScript &scriptPubKey, CScript &scriptSig);

/** Produce a script signature for a transaction. */
bool SignSignature(const CKeyStore &keystore,
    const CScript &fromPubKey,
    CMutableTransaction &txTo,
    unsigned int nIn,
    const CAmount &amount,
    uint32_t nHashType = SIGHASH_ALL | SIGHASH_FORKID);
bool SignSignature(const CKeyStore &keystore,
    const CTransaction &txFrom,
    CMutableTransaction &txTo,
    unsigned int nIn,
    uint32_t nHashType = SIGHASH_ALL | SIGHASH_FORKID);

/** Combine two script signatures using a generic signature checker, intelligently, possibly with OP_0 placeholders. */
CScript CombineSignatures(const CScript &scriptPubKey,
    const BaseSignatureChecker &checker,
    const CScript &scriptSig1,
    const CScript &scriptSig2);

template <typename BYTEARRAY>
std::vector<unsigned char> signmessage(const BYTEARRAY &data, const CKey &key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic << data;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) // signing will only fail if the key is bogus
    {
        return std::vector<unsigned char>();
    }
    return vchSig;
}

/** sign arbitrary data using the same algorithm as the signmessage/verifymessage RPCs and OP_CHECKDATASIG(VERIFY) */
extern template std::vector<unsigned char> signmessage(const std::vector<unsigned char> &data, const CKey &key);
extern template std::vector<unsigned char> signmessage(const std::string &data, const CKey &key);


#endif // BITCOIN_SCRIPT_SIGN_H
