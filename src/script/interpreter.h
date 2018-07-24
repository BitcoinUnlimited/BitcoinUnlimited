// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_INTERPRETER_H
#define BITCOIN_SCRIPT_INTERPRETER_H

#include "primitives/transaction.h"
#include "script_error.h"

#include <stdint.h>
#include <string>
#include <vector>

class CPubKey;
class CScript;
class CTransaction;
class uint256;

/** Signature hash types/flags */
enum
{
    SIGHASH_ALL = 1,
    SIGHASH_NONE = 2,
    SIGHASH_SINGLE = 3,
    SIGHASH_FORKID = 0x40,
    SIGHASH_ANYONECANPAY = 0x80,
};

/** Data signature types (for OP_DATASIGVERIFY) */
enum
{
    DATASIG_COMPACT_ECDSA = 1,
};

/** Script verification flags */
enum
{
    SCRIPT_VERIFY_NONE = 0,

    // Evaluate P2SH subscripts (softfork safe, BIP16).
    SCRIPT_VERIFY_P2SH = (1U << 0),

    // Passing a non-strict-DER signature or one with undefined hashtype to a checksig operation causes script failure.
    // Evaluating a pubkey that is not (0x04 + 64 bytes) or (0x02 or 0x03 + 32 bytes) by checksig causes script failure.
    // (softfork safe, but not used or intended as a consensus rule).
    SCRIPT_VERIFY_STRICTENC = (1U << 1),

    // Passing a non-strict-DER signature to a checksig operation causes script failure (softfork safe, BIP62 rule 1)
    SCRIPT_VERIFY_DERSIG = (1U << 2),

    // Passing a non-strict-DER signature or one with S > order/2 to a checksig operation causes script failure
    // (softfork safe, BIP62 rule 5).
    SCRIPT_VERIFY_LOW_S = (1U << 3),

    // verify dummy stack item consumed by CHECKMULTISIG is of zero-length (softfork safe, BIP62 rule 7).
    SCRIPT_VERIFY_NULLDUMMY = (1U << 4),

    // Using a non-push operator in the scriptSig causes script failure (softfork safe, BIP62 rule 2).
    SCRIPT_VERIFY_SIGPUSHONLY = (1U << 5),

    // Require minimal encodings for all push operations (OP_0... OP_16, OP_1NEGATE where possible, direct
    // pushes up to 75 bytes, OP_PUSHDATA up to 255 bytes, OP_PUSHDATA2 for anything larger). Evaluating
    // any other push causes the script to fail (BIP62 rule 3).
    // In addition, whenever a stack element is interpreted as a number, it must be of minimal length (BIP62 rule 4).
    // (softfork safe)
    SCRIPT_VERIFY_MINIMALDATA = (1U << 6),

    // Discourage use of NOPs reserved for upgrades (NOP1-10)
    //
    // Provided so that nodes can avoid accepting or mining transactions
    // containing executed NOP's whose meaning may change after a soft-fork,
    // thus rendering the script invalid; with this flag set executing
    // discouraged NOPs fails the script. This verification flag will never be
    // a mandatory flag applied to scripts in a block. NOPs that are not
    // executed, e.g.  within an unexecuted IF ENDIF block, are *not* rejected.
    SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS = (1U << 7),

    // Require that only a single stack element remains after evaluation. This changes the success criterion from
    // "At least one stack element must remain, and when interpreted as a boolean, it must be true" to
    // "Exactly one stack element must remain, and when interpreted as a boolean, it must be true".
    // (softfork safe, BIP62 rule 6)
    // Note: CLEANSTACK should never be used without P2SH.
    SCRIPT_VERIFY_CLEANSTACK = (1U << 8),

    // Verify CHECKLOCKTIMEVERIFY
    //
    // See BIP65 for details.
    SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY = (1U << 9),

    // support CHECKSEQUENCEVERIFY opcode
    //
    // See BIP112 for details
    SCRIPT_VERIFY_CHECKSEQUENCEVERIFY = (1U << 10),

    // Signature(s) must be empty vector if an CHECK(MULTI)SIG operation failed
    SCRIPT_VERIFY_NULLFAIL = (1U << 14),

    // Do we accept signature using SIGHASH_FORKID
    //
    //
    SCRIPT_ENABLE_SIGHASH_FORKID = (1U << 16),

    // Enable Replay protection.
    SCRIPT_ENABLE_REPLAY_PROTECTION = (1U << 17),

    // Enable new opcodes.
    //
    SCRIPT_ENABLE_MAY152018_OPCODES = (1U << 18),
};

bool CheckSignatureEncoding(const std::vector<unsigned char> &vchSig, unsigned int flags, ScriptError *serror);

// WARNING:
// SIGNATURE_HASH_ERROR represents the special value of uint256(1) that is used by the legacy SignatureHash
// function to signal errors in calculating the signature hash. This export is ONLY meant to check for the
// consensus-critical oddities of the legacy signature validation code and SHOULD NOT be used to signal
// problems during signature hash calculations for any current BCH signature hash functions!
extern const uint256 SIGNATURE_HASH_ERROR;

// If you are signing you may call this function and the BitcoinCash or Legacy method will be chosen based on nHashType
uint256 SignatureHash(const CScript &scriptCode,
    const CTransaction &txTo,
    unsigned int nIn,
    uint32_t nHashType,
    const CAmount &amount,
    size_t *nHashedOut = NULL);

class BaseSignatureChecker
{
public:
    virtual bool CheckSig(const std::vector<unsigned char> &scriptSig,
        const std::vector<unsigned char> &vchPubKey,
        const CScript &scriptCode) const
    {
        return false;
    }

    virtual bool CheckLockTime(const CScriptNum &nLockTime) const { return false; }
    virtual bool CheckSequence(const CScriptNum &nSequence) const { return false; }
    virtual ~BaseSignatureChecker() {}
};

class TransactionSignatureChecker : public BaseSignatureChecker
{
private:
    const CTransaction *txTo;
    unsigned int nIn;
    const CAmount amount;
    mutable size_t nBytesHashed;
    mutable size_t nSigops;
    unsigned int nFlags;

protected:
    virtual bool VerifySignature(const std::vector<unsigned char> &vchSig,
        const CPubKey &vchPubKey,
        const uint256 &sighash) const;

public:
    TransactionSignatureChecker(const CTransaction *txToIn,
        unsigned int nInIn,
        const CAmount &amountIn,
        unsigned int flags = SCRIPT_ENABLE_SIGHASH_FORKID)
        : txTo(txToIn), nIn(nInIn), amount(amountIn), nBytesHashed(0), nSigops(0), nFlags(flags)
    {
    }
    bool CheckSig(const std::vector<unsigned char> &scriptSig,
        const std::vector<unsigned char> &vchPubKey,
        const CScript &scriptCode) const;
    bool CheckLockTime(const CScriptNum &nLockTime) const;
    bool CheckSequence(const CScriptNum &nSequence) const;
    size_t GetBytesHashed() const { return nBytesHashed; }
    size_t GetNumSigops() const { return nSigops; }
};

class MutableTransactionSignatureChecker : public TransactionSignatureChecker
{
private:
    const CTransaction txTo;

public:
    MutableTransactionSignatureChecker(const CMutableTransaction *txToIn,
        unsigned int nInIn,
        const CAmount &amountIn,
        unsigned int flags = SCRIPT_ENABLE_SIGHASH_FORKID)
        : TransactionSignatureChecker(&txTo, nInIn, amountIn, flags), txTo(*txToIn)
    {
    }
};

bool EvalScript(std::vector<std::vector<unsigned char> > &stack,
    const CScript &script,
    unsigned int flags,
    const BaseSignatureChecker &checker,
    ScriptError *error = NULL,
    unsigned char *sighashtype = NULL);
bool VerifyScript(const CScript &scriptSig,
    const CScript &scriptPubKey,
    unsigned int flags,
    const BaseSignatureChecker &checker,
    ScriptError *error = NULL,
    unsigned char *sighashtype = NULL);

// string prefixed to data when validating signed messages either via DATASIGVERIFY or RPC call.  This ensures
// that the signature was intended for use on this blockchain.
extern const std::string strMessageMagic;

#endif // BITCOIN_SCRIPT_INTERPRETER_H
