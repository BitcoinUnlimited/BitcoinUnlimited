// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "interpreter.h"

#include "bitfield.h"
#include "bitmanip.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/script_error.h"
#include "uint256.h"
#include "util.h"
const std::string strMessageMagic = "Bitcoin Signed Message:\n";

extern uint256 SignatureHashLegacy(const CScript &scriptCode,
    const CTransaction &txTo,
    unsigned int nIn,
    uint32_t nHashType,
    const CAmount &amount,
    size_t *nHashedOut);

using namespace std;

typedef vector<unsigned char> valtype;

bool CastToBool(const valtype &vch)
{
    for (unsigned int i = 0; i < vch.size(); i++)
    {
        if (vch[i] != 0)
        {
            // Can be negative zero
            if (i == vch.size() - 1 && vch[i] == 0x80)
                return false;
            return true;
        }
    }
    return false;
}

static uint32_t GetHashType(const valtype &vchSig)
{
    if (vchSig.size() == 0)
    {
        return 0;
    }

    return vchSig[vchSig.size() - 1];
}

/**
 * Script is a stack machine (like Forth) that evaluates a predicate
 * returning a bool indicating valid or not.  There are no loops.
 */
#define stacktop(i) (stack.at(stack.size() + (i)))
#define altstacktop(i) (altstack.at(altstack.size() + (i)))
static inline void popstack(vector<valtype> &stack)
{
    if (stack.empty())
        throw runtime_error("popstack(): stack empty");
    stack.pop_back();
}

static void CleanupScriptCode(CScript &scriptCode, const std::vector<uint8_t> &vchSig, uint32_t flags)
{
    // Drop the signature in scripts when SIGHASH_FORKID is not used.
    uint32_t sigHashType = GetHashType(vchSig);
    if (!(flags & SCRIPT_ENABLE_SIGHASH_FORKID) || !(sigHashType & SIGHASH_FORKID))
    {
        scriptCode.FindAndDelete(CScript(vchSig));
    }
}

bool static IsCompressedOrUncompressedPubKey(const valtype &vchPubKey)
{
    if (vchPubKey.size() < CPubKey::COMPRESSED_PUBLIC_KEY_SIZE)
    {
        //  Non-canonical public key: too short
        return false;
    }
    if (vchPubKey[0] == 0x04)
    {
        if (vchPubKey.size() != CPubKey::PUBLIC_KEY_SIZE)
        {
            //  Non-canonical public key: invalid length for uncompressed key
            return false;
        }
    }
    else if (vchPubKey[0] == 0x02 || vchPubKey[0] == 0x03)
    {
        if (vchPubKey.size() != 33)
        {
            //  Non-canonical public key: invalid length for compressed key
            return false;
        }
    }
    else
    {
        //  Non-canonical public key: neither compressed nor uncompressed
        return false;
    }
    return true;
}

static bool IsCompressedPubKey(const valtype &vchPubKey)
{
    if (vchPubKey.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE)
    {
        //  Non-canonical public key: invalid length for compressed key
        return false;
    }
    if (vchPubKey[0] != 0x02 && vchPubKey[0] != 0x03)
    {
        //  Non-canonical public key: invalid prefix for compressed key
        return false;
    }
    return true;
}

/**
 * A canonical signature exists of: <30> <total len> <02> <len R> <R> <02> <len S> <S> <hashtype>
 * Where R and S are not negative (their first byte has its highest bit not set), and not
 * excessively padded (do not start with a 0 byte, unless an otherwise negative number follows,
 * in which case a single 0 byte is necessary and even required).
 *
 * See https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
 *
 * This function is consensus-critical since BIP66.
 */
bool static IsValidSignatureEncoding(const std::vector<unsigned char> &sig)
{
    // Format: 0x30 [total-length] 0x02 [R-length] [R] 0x02 [S-length] [S] [sighash]
    // * total-length: 1-byte length descriptor of everything that follows,
    //   excluding the sighash byte.
    // * R-length: 1-byte length descriptor of the R value that follows.
    // * R: arbitrary-length big-endian encoded R value. It must use the shortest
    //   possible encoding for a positive integers (which means no null bytes at
    //   the start, except a single one when the next byte has its highest bit set).
    // * S-length: 1-byte length descriptor of the S value that follows.
    // * S: arbitrary-length big-endian encoded S value. The same rules apply.
    // * sighash: 1-byte value indicating what data is hashed (not part of the DER
    //   signature)

    // Minimum and maximum size constraints.
    if (sig.size() < 9)
        return false;
    if (sig.size() > 73)
        return false;

    // A signature is of type 0x30 (compound).
    if (sig[0] != 0x30)
        return false;

    // Make sure the length covers the entire signature.
    if (sig[1] != sig.size() - 3)
        return false;

    // Extract the length of the R element.
    unsigned int lenR = sig[3];

    // Make sure the length of the S element is still inside the signature.
    if (5 + lenR >= sig.size())
        return false;

    // Extract the length of the S element.
    unsigned int lenS = sig[5 + lenR];

    // Verify that the length of the signature matches the sum of the length
    // of the elements.
    if ((size_t)(lenR + lenS + 7) != sig.size())
        return false;

    // Check whether the R element is an integer.
    if (sig[2] != 0x02)
        return false;

    // Zero-length integers are not allowed for R.
    if (lenR == 0)
        return false;

    // Negative numbers are not allowed for R.
    if (sig[4] & 0x80)
        return false;

    // Null bytes at the start of R are not allowed, unless R would
    // otherwise be interpreted as a negative number.
    if (lenR > 1 && (sig[4] == 0x00) && !(sig[5] & 0x80))
        return false;

    // Check whether the S element is an integer.
    if (sig[lenR + 4] != 0x02)
        return false;

    // Zero-length integers are not allowed for S.
    if (lenS == 0)
        return false;

    // Negative numbers are not allowed for S.
    if (sig[lenR + 6] & 0x80)
        return false;

    // Null bytes at the start of S are not allowed, unless S would otherwise be
    // interpreted as a negative number.
    if (lenS > 1 && (sig[lenR + 6] == 0x00) && !(sig[lenR + 7] & 0x80))
        return false;

    return true;
}


//! Check signature encoding without sighash byte
//! This is a copy of Bitcoin ABC's code, written mainly by deadalnix, from
//! revision: f8283a3f284fc4722c1d6583b8746a17831d3bd0
/**
 * A canonical signature exists of: <30> <total len> <02> <len R> <R> <02> <len
 * S> <S> <hashtype>, where R and S are not negative (their first byte has its
 * highest bit not set), and not excessively padded (do not start with a 0 byte,
 * unless an otherwise negative number follows, in which case a single 0 byte is
 * necessary and even required).
 *
 * See https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
 *
 * This function is consensus-critical since BIP66.
 */
bool IsValidSignatureEncodingWithoutSigHash(const valtype &sig)
{
    // Format: 0x30 [total-length] 0x02 [R-length] [R] 0x02 [S-length] [S]
    // * total-length: 1-byte length descriptor of everything that follows,
    // excluding the sighash byte.
    // * R-length: 1-byte length descriptor of the R value that follows.
    // * R: arbitrary-length big-endian encoded R value. It must use the
    // shortest possible encoding for a positive integers (which means no null
    // bytes at the start, except a single one when the next byte has its
    // highest bit set).
    // * S-length: 1-byte length descriptor of the S value that follows.
    // * S: arbitrary-length big-endian encoded S value. The same rules apply.

    // Minimum and maximum size constraints.
    if (sig.size() < 8 || sig.size() > 72)
    {
        return false;
    }

    //
    // Check that the signature is a compound structure of proper size.
    //

    // A signature is of type 0x30 (compound).
    if (sig[0] != 0x30)
    {
        return false;
    }

    // Make sure the length covers the entire signature.
    // Remove:
    // * 1 byte for the coupound type.
    // * 1 byte for the length of the signature.
    if (sig[1] != sig.size() - 2)
    {
        return false;
    }

    //
    // Check that R is an positive integer of sensible size.
    //

    // Check whether the R element is an integer.
    if (sig[2] != 0x02)
    {
        return false;
    }

    // Extract the length of the R element.
    const uint32_t lenR = sig[3];

    // Zero-length integers are not allowed for R.
    if (lenR == 0)
    {
        return false;
    }

    // Negative numbers are not allowed for R.
    if (sig[4] & 0x80)
    {
        return false;
    }

    // Make sure the length of the R element is consistent with the signature
    // size.
    // Remove:
    // * 1 byte for the coumpound type.
    // * 1 byte for the length of the signature.
    // * 2 bytes for the integer type of R and S.
    // * 2 bytes for the size of R and S.
    // * 1 byte for S itself.
    if (lenR > (sig.size() - 7))
    {
        return false;
    }

    // Null bytes at the start of R are not allowed, unless R would otherwise be
    // interpreted as a negative number.
    //
    // /!\ This check can only be performed after we checked that lenR is
    //     consistent with the size of the signature or we risk to access out of
    //     bound elements.
    if (lenR > 1 && (sig[4] == 0x00) && !(sig[5] & 0x80))
    {
        return false;
    }

    //
    // Check that S is an positive integer of sensible size.
    //

    // S's definition starts after R's definition:
    // * 1 byte for the coumpound type.
    // * 1 byte for the length of the signature.
    // * 1 byte for the size of R.
    // * lenR bytes for R itself.
    // * 1 byte to get to S.
    const uint32_t startS = lenR + 4;

    // Check whether the S element is an integer.
    if (sig[startS] != 0x02)
    {
        return false;
    }

    // Extract the length of the S element.
    const uint32_t lenS = sig[startS + 1];

    // Zero-length integers are not allowed for S.
    if (lenS == 0)
    {
        return false;
    }

    // Negative numbers are not allowed for S.
    if (sig[startS + 2] & 0x80)
    {
        return false;
    }

    // Verify that the length of S is consistent with the size of the signature
    // including metadatas:
    // * 1 byte for the integer type of S.
    // * 1 byte for the size of S.
    if (size_t(startS + lenS + 2) != sig.size())
    {
        return false;
    }

    // Null bytes at the start of S are not allowed, unless S would otherwise be
    // interpreted as a negative number.
    //
    // /!\ This check can only be performed after we checked that lenR and lenS
    //     are consistent with the size of the signature or we risk to access
    //     out of bound elements.
    if (lenS > 1 && (sig[startS + 2] == 0x00) && !(sig[startS + 3] & 0x80))
    {
        return false;
    }

    return true;
}

bool static IsLowDERSignature(const valtype &vchSig, ScriptError *serror, const bool check_sighash)
{
    if (check_sighash)
    {
        if (!IsValidSignatureEncoding(vchSig))
            return set_error(serror, SCRIPT_ERR_SIG_DER);
    }
    else
    {
        if (!IsValidSignatureEncodingWithoutSigHash(vchSig))
            return set_error(serror, SCRIPT_ERR_SIG_DER);
    }
    // https://bitcoin.stackexchange.com/a/12556:
    //     Also note that inside transaction signatures, an extra hashtype byte
    //     follows the actual signature data.
    std::vector<unsigned char> vchSigCopy(vchSig.begin(), vchSig.begin() + vchSig.size() - (check_sighash ? 1 : 0));
    // If the S value is above the order of the curve divided by two, its
    // complement modulo the order could have been used instead, which is
    // one byte shorter when encoded correctly.
    if (!CPubKey::CheckLowS(vchSigCopy))
    {
        return set_error(serror, SCRIPT_ERR_SIG_HIGH_S);
    }
    return true;
}

static bool IsDefinedHashtypeSignature(const valtype &vchSig)
{
    if (vchSig.size() == 0)
    {
        return false;
    }
    uint32_t nHashType = GetHashType(vchSig) & ~(SIGHASH_ANYONECANPAY | SIGHASH_FORKID);
    if (nHashType < SIGHASH_ALL || nHashType > SIGHASH_SINGLE)
        return false;

    return true;
}

static bool CheckSignatureEncodingSigHashChoice(const vector<unsigned char> &vchSig,
    unsigned int flags,
    ScriptError *serror,
    const bool check_sighash)
{
    // Empty signature. Not strictly DER encoded, but allowed to provide a
    // compact way to provide an invalid signature for use with CHECK(MULTI)SIG
    if (vchSig.size() == 0)
    {
        return true;
    }

    if (vchSig.size() == 64 + ((check_sighash == true) ? 1 : 0)) // 64 sig length plus 1 sighashtype
    {
        // In a generic-signature context, 64-byte signatures are interpreted
        // as Schnorr signatures (always correctly encoded) when flag set.
        if (check_sighash && ((flags & SCRIPT_VERIFY_STRICTENC) != 0))
        {
            if (!IsDefinedHashtypeSignature(vchSig))
                return set_error(serror, SCRIPT_ERR_SIG_HASHTYPE);

            // schnorr sigs must use forkid sighash if forkid flag set
            if ((flags & SCRIPT_ENABLE_SIGHASH_FORKID) && ((vchSig[64] & SIGHASH_FORKID) == 0))
                return set_error(serror, SCRIPT_ERR_MUST_USE_FORKID);
        }
        return true;
    }

    if ((flags & (SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC)) != 0)
    {
        if (check_sighash)
        {
            if (!IsValidSignatureEncoding(vchSig))
                return set_error(serror, SCRIPT_ERR_SIG_DER);
        }
        else
        {
            if (!IsValidSignatureEncodingWithoutSigHash(vchSig))
                return set_error(serror, SCRIPT_ERR_SIG_DER);
        }
    }
    if ((flags & SCRIPT_VERIFY_LOW_S) != 0 && !IsLowDERSignature(vchSig, serror, check_sighash))
    {
        // serror is set
        return false;
    }
    else if (check_sighash && ((flags & SCRIPT_VERIFY_STRICTENC) != 0) && !IsDefinedHashtypeSignature(vchSig))
    {
        return set_error(serror, SCRIPT_ERR_SIG_HASHTYPE);
    }
    return true;
}


// For CHECKSIG etc.
bool CheckSignatureEncoding(const vector<unsigned char> &vchSig, unsigned int flags, ScriptError *serror)
{
    return CheckSignatureEncodingSigHashChoice(vchSig, flags, serror, true);
}

// For CHECKDATASIG / CHECKDATASIGVERIFY
bool CheckDataSignatureEncoding(const valtype &vchSig, uint32_t flags, ScriptError *serror)
{
    return CheckSignatureEncodingSigHashChoice(vchSig, flags, serror, false);
}

static bool CheckTransactionECDSASignatureEncoding(const valtype &vchSig, uint32_t flags, ScriptError *serror)
{
    // In an ECDSA-only context, 64-byte signatures + 1 sighash type bit are forbidden since they are Schnorr.
    if (vchSig.size() == 65)
        return set_error(serror, SCRIPT_ERR_SIG_BADLENGTH);
    return CheckSignatureEncodingSigHashChoice(vchSig, flags, serror, true);
}

/**
 * Check that the signature provided to authentify a transaction is properly
 * encoded Schnorr signature (or null). Signatures passed to the new-mode
 * OP_CHECKMULTISIG and its verify variant must be checked using this function.
 */
static bool CheckTransactionSchnorrSignatureEncoding(const valtype &vchSig, uint32_t flags, ScriptError *serror)
{
    // Insist that this sig is Schnorr
    if (vchSig.size() != 65)
        return set_error(serror, SCRIPT_ERR_SIG_NONSCHNORR);
    return CheckSignatureEncodingSigHashChoice(vchSig, flags, serror, true);
}

bool CheckPubKeyEncoding(const valtype &vchPubKey, unsigned int flags, ScriptError *serror)
{
    if ((flags & SCRIPT_VERIFY_STRICTENC) != 0 && !IsCompressedOrUncompressedPubKey(vchPubKey))
    {
        return set_error(serror, SCRIPT_ERR_PUBKEYTYPE);
    }

    // Only compressed keys are accepted when
    // SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE is enabled.
    if (flags & SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE && !IsCompressedPubKey(vchPubKey))
    {
        return set_error(serror, SCRIPT_ERR_NONCOMPRESSED_PUBKEY);
    }
    return true;
}

static inline bool IsOpcodeDisabled(opcodetype opcode, uint32_t flags)
{
    switch (opcode)
    {
    case OP_2MUL:
    case OP_2DIV:
    case OP_INVERT:
    case OP_MUL:
    case OP_LSHIFT:
    case OP_RSHIFT:
        // disabled opcodes
        return true;
    default:
        break;
    }

    return false;
}

bool EvalScript(vector<vector<unsigned char> > &stack,
    const CScript &script,
    unsigned int flags,
    unsigned int maxOps,
    const BaseSignatureChecker &checker,
    ScriptError *serror,
    unsigned char *sighashtype)
{
    ScriptMachine sm(flags, checker, maxOps, 0xffffffff);
    sm.setStack(stack);
    bool result = sm.Eval(script);
    stack = sm.getStack();
    if (serror)
        *serror = sm.getError();
    if (sighashtype)
        *sighashtype = sm.getSigHashType();
    return result;
}


static const CScriptNum bnZero(0);
static const CScriptNum bnOne(1);
static const CScriptNum bnFalse(0);
static const CScriptNum bnTrue(1);
static const StackDataType vchFalse(0);
static const StackDataType vchZero(0);
static const StackDataType vchTrue(1, 1);

// Returns info about the next instruction to be run
std::tuple<bool, opcodetype, StackDataType, ScriptError> ScriptMachine::Peek()
{
    ScriptError err;
    opcodetype opcode;
    StackDataType vchPushValue;
    auto oldpc = pc;
    if (!script->GetOp(pc, opcode, vchPushValue))
        set_error(&err, SCRIPT_ERR_BAD_OPCODE);
    else if (vchPushValue.size() > MAX_SCRIPT_ELEMENT_SIZE)
        set_error(&err, SCRIPT_ERR_PUSH_SIZE);
    pc = oldpc;
    bool fExec = !count(vfExec.begin(), vfExec.end(), false);
    return std::tuple<bool, opcodetype, StackDataType, ScriptError>(fExec, opcode, vchPushValue, err);
}


bool ScriptMachine::BeginStep(const CScript &_script)
{
    script = &_script;

    pc = pbegin = script->begin();
    pend = script->end();
    pbegincodehash = pc;

    sighashtype = 0;
    stats.nOpCount = 0;
    vfExec.clear();

    set_error(&error, SCRIPT_ERR_UNKNOWN_ERROR);
    if (script->size() > MAX_SCRIPT_SIZE)
    {
        script = nullptr;
        return set_error(&error, SCRIPT_ERR_SCRIPT_SIZE);
    }
    return true;
}


int ScriptMachine::getPos() { return (pc - pbegin); }
bool ScriptMachine::Eval(const CScript &_script)
{
    bool ret;

    if (!(ret = BeginStep(_script)))
        return ret;

    while (pc < pend)
    {
        ret = Step();
        if (!ret)
            break;
    }
    if (ret)
        ret = EndStep();
    script = nullptr; // Ensure that the ScriptMachine does not hold script for longer than this scope

    return ret;
}

bool ScriptMachine::EndStep()
{
    script = nullptr; // let go of our use of the script
    if (!vfExec.empty())
        return set_error(&error, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
    return set_success(&error);
}

bool ScriptMachine::Step()
{
    bool fRequireMinimal = (flags & SCRIPT_VERIFY_MINIMALDATA) != 0;
    opcodetype opcode;
    StackDataType vchPushValue;
    ScriptError *serror = &error;
    try
    {
        {
            bool fExec = !count(vfExec.begin(), vfExec.end(), false);

            //
            // Read instruction
            //
            if (!script->GetOp(pc, opcode, vchPushValue))
                return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
            if (vchPushValue.size() > MAX_SCRIPT_ELEMENT_SIZE)
                return set_error(serror, SCRIPT_ERR_PUSH_SIZE);

            // Note how OP_RESERVED does not count towards the opcode limit.
            if (opcode > OP_16 && ++stats.nOpCount > maxOps)
                return set_error(serror, SCRIPT_ERR_OP_COUNT);

            // Some opcodes are disabled.
            if (IsOpcodeDisabled(opcode, flags))
            {
                return set_error(serror, SCRIPT_ERR_DISABLED_OPCODE);
            }

            if (fExec && 0 <= opcode && opcode <= OP_PUSHDATA4)
            {
                if (fRequireMinimal && !CheckMinimalPush(vchPushValue, opcode))
                {
                    return set_error(serror, SCRIPT_ERR_MINIMALDATA);
                }
                stack.push_back(vchPushValue);
            }
            else if (fExec || (OP_IF <= opcode && opcode <= OP_ENDIF))
            {
                switch (opcode)
                {
                //
                // Push value
                //
                case OP_1NEGATE:
                case OP_1:
                case OP_2:
                case OP_3:
                case OP_4:
                case OP_5:
                case OP_6:
                case OP_7:
                case OP_8:
                case OP_9:
                case OP_10:
                case OP_11:
                case OP_12:
                case OP_13:
                case OP_14:
                case OP_15:
                case OP_16:
                {
                    // ( -- value)
                    CScriptNum bn((int)opcode - (int)(OP_1 - 1));
                    stack.push_back(bn.getvch());
                    // The result of these opcodes should always be the minimal way to push the data
                    // they push, so no need for a CheckMinimalPush here.
                }
                break;

                //
                // Control
                //
                case OP_NOP:
                    break;

                case OP_CHECKLOCKTIMEVERIFY:
                {
                    if (!(flags & SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY))
                    {
                        break;
                    }

                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    // Note that elsewhere numeric opcodes are limited to
                    // operands in the range -2**31+1 to 2**31-1, however it is
                    // legal for opcodes to produce results exceeding that
                    // range. This limitation is implemented by CScriptNum's
                    // default 4-byte limit.
                    //
                    // If we kept to that limit we'd have a year 2038 problem,
                    // even though the nLockTime field in transactions
                    // themselves is uint32 which only becomes meaningless
                    // after the year 2106.
                    //
                    // Thus as a special case we tell CScriptNum to accept up
                    // to 5-byte bignums, which are good until 2**39-1, well
                    // beyond the 2**32-1 limit of the nLockTime field itself.
                    const CScriptNum nLockTime(stacktop(-1), fRequireMinimal, 5);

                    // In the rare event that the argument may be < 0 due to
                    // some arithmetic being done first, you can always use
                    // 0 MAX CHECKLOCKTIMEVERIFY.
                    if (nLockTime < 0)
                        return set_error(serror, SCRIPT_ERR_NEGATIVE_LOCKTIME);

                    // Actually compare the specified lock time with the transaction.
                    if (!checker.CheckLockTime(nLockTime))
                        return set_error(serror, SCRIPT_ERR_UNSATISFIED_LOCKTIME);

                    break;
                }

                case OP_CHECKSEQUENCEVERIFY:
                {
                    if (!(flags & SCRIPT_VERIFY_CHECKSEQUENCEVERIFY))
                    {
                        break;
                    }

                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    // nSequence, like nLockTime, is a 32-bit unsigned integer
                    // field. See the comment in CHECKLOCKTIMEVERIFY regarding
                    // 5-byte numeric operands.
                    const CScriptNum nSequence(stacktop(-1), fRequireMinimal, 5);

                    // In the rare event that the argument may be < 0 due to
                    // some arithmetic being done first, you can always use
                    // 0 MAX CHECKSEQUENCEVERIFY.
                    if (nSequence < 0)
                        return set_error(serror, SCRIPT_ERR_NEGATIVE_LOCKTIME);

                    // To provide for future soft-fork extensibility, if the
                    // operand has the disabled lock-time flag set,
                    // CHECKSEQUENCEVERIFY behaves as a NOP.
                    if ((nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) != 0)
                        break;

                    // Compare the specified sequence number with the input.
                    if (!checker.CheckSequence(nSequence))
                        return set_error(serror, SCRIPT_ERR_UNSATISFIED_LOCKTIME);

                    break;
                }

                case OP_NOP1:
                case OP_NOP4:
                case OP_NOP5:
                case OP_NOP6:
                case OP_NOP7:
                case OP_NOP8:
                case OP_NOP9:
                case OP_NOP10:
                {
                    if (flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS)
                        return set_error(serror, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
                }
                break;

                case OP_IF:
                case OP_NOTIF:
                {
                    // <expression> if [statements] [else [statements]] endif
                    bool fValue = false;
                    if (fExec)
                    {
                        if (stack.size() < 1)
                            return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                        valtype &vch = stacktop(-1);
                        fValue = CastToBool(vch);
                        if (opcode == OP_NOTIF)
                            fValue = !fValue;
                        popstack(stack);
                    }
                    vfExec.push_back(fValue);
                }
                break;

                case OP_ELSE:
                {
                    if (vfExec.empty())
                        return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                    vfExec.back() = !vfExec.back();
                }
                break;

                case OP_ENDIF:
                {
                    if (vfExec.empty())
                        return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                    vfExec.pop_back();
                }
                break;

                case OP_VERIFY:
                {
                    // (true -- ) or
                    // (false -- false) and return
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    bool fValue = CastToBool(stacktop(-1));
                    if (fValue)
                        popstack(stack);
                    else
                        return set_error(serror, SCRIPT_ERR_VERIFY);
                }
                break;

                case OP_RETURN:
                {
                    return set_error(serror, SCRIPT_ERR_OP_RETURN);
                }
                break;


                //
                // Stack ops
                //
                case OP_TOALTSTACK:
                {
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    altstack.push_back(stacktop(-1));
                    popstack(stack);
                }
                break;

                case OP_FROMALTSTACK:
                {
                    if (altstack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_ALTSTACK_OPERATION);
                    stack.push_back(altstacktop(-1));
                    popstack(altstack);
                }
                break;

                case OP_2DROP:
                {
                    // (x1 x2 -- )
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    popstack(stack);
                    popstack(stack);
                }
                break;

                case OP_2DUP:
                {
                    // (x1 x2 -- x1 x2 x1 x2)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-2);
                    valtype vch2 = stacktop(-1);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                }
                break;

                case OP_3DUP:
                {
                    // (x1 x2 x3 -- x1 x2 x3 x1 x2 x3)
                    if (stack.size() < 3)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-3);
                    valtype vch2 = stacktop(-2);
                    valtype vch3 = stacktop(-1);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                    stack.push_back(vch3);
                }
                break;

                case OP_2OVER:
                {
                    // (x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2)
                    if (stack.size() < 4)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-4);
                    valtype vch2 = stacktop(-3);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                }
                break;

                case OP_2ROT:
                {
                    // (x1 x2 x3 x4 x5 x6 -- x3 x4 x5 x6 x1 x2)
                    if (stack.size() < 6)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-6);
                    valtype vch2 = stacktop(-5);
                    stack.erase(stack.end() - 6, stack.end() - 4);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                }
                break;

                case OP_2SWAP:
                {
                    // (x1 x2 x3 x4 -- x3 x4 x1 x2)
                    if (stack.size() < 4)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    swap(stacktop(-4), stacktop(-2));
                    swap(stacktop(-3), stacktop(-1));
                }
                break;

                case OP_IFDUP:
                {
                    // (x - 0 | x x)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-1);
                    if (CastToBool(vch))
                        stack.push_back(vch);
                }
                break;

                case OP_DEPTH:
                {
                    // -- stacksize
                    CScriptNum bn(stack.size());
                    stack.push_back(bn.getvch());
                }
                break;

                case OP_DROP:
                {
                    // (x -- )
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    popstack(stack);
                }
                break;

                case OP_DUP:
                {
                    // (x -- x x)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-1);
                    stack.push_back(vch);
                }
                break;

                case OP_NIP:
                {
                    // (x1 x2 -- x2)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    stack.erase(stack.end() - 2);
                }
                break;

                case OP_OVER:
                {
                    // (x1 x2 -- x1 x2 x1)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-2);
                    stack.push_back(vch);
                }
                break;

                case OP_PICK:
                case OP_ROLL:
                {
                    // (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
                    // (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    int n = CScriptNum(stacktop(-1), fRequireMinimal).getint();
                    popstack(stack);
                    if (n < 0 || n >= (int)stack.size())
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-n - 1);
                    if (opcode == OP_ROLL)
                        stack.erase(stack.end() - n - 1);
                    stack.push_back(vch);
                }
                break;

                case OP_ROT:
                {
                    // (x1 x2 x3 -- x2 x3 x1)
                    //  x2 x1 x3  after first swap
                    //  x2 x3 x1  after second swap
                    if (stack.size() < 3)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    swap(stacktop(-3), stacktop(-2));
                    swap(stacktop(-2), stacktop(-1));
                }
                break;

                case OP_SWAP:
                {
                    // (x1 x2 -- x2 x1)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    swap(stacktop(-2), stacktop(-1));
                }
                break;

                case OP_TUCK:
                {
                    // (x1 x2 -- x2 x1 x2)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-1);
                    stack.insert(stack.end() - 2, vch);
                }
                break;


                case OP_SIZE:
                {
                    // (in -- in size)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    CScriptNum bn(stacktop(-1).size());
                    stack.push_back(bn.getvch());
                }
                break;


                //
                // Bitwise logic
                //
                case OP_AND:
                case OP_OR:
                case OP_XOR:
                {
                    // (x1 x2 - out)
                    if (stack.size() < 2)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }
                    valtype &vch1 = stacktop(-2);
                    valtype &vch2 = stacktop(-1);

                    // Inputs must be the same size
                    if (vch1.size() != vch2.size())
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_OPERAND_SIZE);
                    }

                    // To avoid allocating, we modify vch1 in place.
                    switch (opcode)
                    {
                    case OP_AND:
                        for (size_t i = 0; i < vch1.size(); ++i)
                        {
                            vch1[i] &= vch2[i];
                        }
                        break;
                    case OP_OR:
                        for (size_t i = 0; i < vch1.size(); ++i)
                        {
                            vch1[i] |= vch2[i];
                        }
                        break;
                    case OP_XOR:
                        for (size_t i = 0; i < vch1.size(); ++i)
                        {
                            vch1[i] ^= vch2[i];
                        }
                        break;
                    default:
                        break;
                    }

                    // And pop vch2.
                    popstack(stack);
                }
                break;

                case OP_EQUAL:
                case OP_EQUALVERIFY:
                    // case OP_NOTEQUAL: // use OP_NUMNOTEQUAL
                    {
                        // (x1 x2 - bool)
                        if (stack.size() < 2)
                            return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        valtype &vch1 = stacktop(-2);
                        valtype &vch2 = stacktop(-1);
                        bool fEqual = (vch1 == vch2);
                        // OP_NOTEQUAL is disabled because it would be too easy to say
                        // something like n != 1 and have some wiseguy pass in 1 with extra
                        // zero bytes after it (numerically, 0x01 == 0x0001 == 0x000001)
                        // if (opcode == OP_NOTEQUAL)
                        //    fEqual = !fEqual;
                        popstack(stack);
                        popstack(stack);
                        stack.push_back(fEqual ? vchTrue : vchFalse);
                        if (opcode == OP_EQUALVERIFY)
                        {
                            if (fEqual)
                                popstack(stack);
                            else
                                return set_error(serror, SCRIPT_ERR_EQUALVERIFY);
                        }
                    }
                    break;


                //
                // Numeric
                //
                case OP_1ADD:
                case OP_1SUB:
                case OP_NEGATE:
                case OP_ABS:
                case OP_NOT:
                case OP_0NOTEQUAL:
                {
                    // (in -- out)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    CScriptNum bn(stacktop(-1), fRequireMinimal);
                    switch (opcode)
                    {
                    case OP_1ADD:
                        bn += bnOne;
                        break;
                    case OP_1SUB:
                        bn -= bnOne;
                        break;
                    case OP_NEGATE:
                        bn = -bn;
                        break;
                    case OP_ABS:
                        if (bn < bnZero)
                            bn = -bn;
                        break;
                    case OP_NOT:
                        bn = (bn == bnZero);
                        break;
                    case OP_0NOTEQUAL:
                        bn = (bn != bnZero);
                        break;
                    default:
                        assert(!"invalid opcode");
                        break;
                    }
                    popstack(stack);
                    stack.push_back(bn.getvch());
                }
                break;

                case OP_ADD:
                case OP_SUB:
                case OP_DIV:
                case OP_MOD:
                case OP_BOOLAND:
                case OP_BOOLOR:
                case OP_NUMEQUAL:
                case OP_NUMEQUALVERIFY:
                case OP_NUMNOTEQUAL:
                case OP_LESSTHAN:
                case OP_GREATERTHAN:
                case OP_LESSTHANOREQUAL:
                case OP_GREATERTHANOREQUAL:
                case OP_MIN:
                case OP_MAX:
                {
                    // (x1 x2 -- out)
                    if (stack.size() < 2)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }
                    CScriptNum bn1(stacktop(-2), fRequireMinimal);
                    CScriptNum bn2(stacktop(-1), fRequireMinimal);
                    CScriptNum bn(0);
                    switch (opcode)
                    {
                    case OP_ADD:
                        bn = bn1 + bn2;
                        break;

                    case OP_SUB:
                        bn = bn1 - bn2;
                        break;

                    case OP_DIV:
                        // denominator must not be 0
                        if (bn2 == 0)
                        {
                            return set_error(serror, SCRIPT_ERR_DIV_BY_ZERO);
                        }
                        bn = bn1 / bn2;
                        break;

                    case OP_MOD:
                        // divisor must not be 0
                        if (bn2 == 0)
                        {
                            return set_error(serror, SCRIPT_ERR_MOD_BY_ZERO);
                        }
                        bn = bn1 % bn2;
                        break;

                    case OP_BOOLAND:
                        bn = (bn1 != bnZero && bn2 != bnZero);
                        break;
                    case OP_BOOLOR:
                        bn = (bn1 != bnZero || bn2 != bnZero);
                        break;
                    case OP_NUMEQUAL:
                        bn = (bn1 == bn2);
                        break;
                    case OP_NUMEQUALVERIFY:
                        bn = (bn1 == bn2);
                        break;
                    case OP_NUMNOTEQUAL:
                        bn = (bn1 != bn2);
                        break;
                    case OP_LESSTHAN:
                        bn = (bn1 < bn2);
                        break;
                    case OP_GREATERTHAN:
                        bn = (bn1 > bn2);
                        break;
                    case OP_LESSTHANOREQUAL:
                        bn = (bn1 <= bn2);
                        break;
                    case OP_GREATERTHANOREQUAL:
                        bn = (bn1 >= bn2);
                        break;
                    case OP_MIN:
                        bn = (bn1 < bn2 ? bn1 : bn2);
                        break;
                    case OP_MAX:
                        bn = (bn1 > bn2 ? bn1 : bn2);
                        break;
                    default:
                        assert(!"invalid opcode");
                        break;
                    }
                    popstack(stack);
                    popstack(stack);
                    stack.push_back(bn.getvch());

                    if (opcode == OP_NUMEQUALVERIFY)
                    {
                        if (CastToBool(stacktop(-1)))
                            popstack(stack);
                        else
                            return set_error(serror, SCRIPT_ERR_NUMEQUALVERIFY);
                    }
                }
                break;

                case OP_WITHIN:
                {
                    // (x min max -- out)
                    if (stack.size() < 3)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    CScriptNum bn1(stacktop(-3), fRequireMinimal);
                    CScriptNum bn2(stacktop(-2), fRequireMinimal);
                    CScriptNum bn3(stacktop(-1), fRequireMinimal);
                    bool fValue = (bn2 <= bn1 && bn1 < bn3);
                    popstack(stack);
                    popstack(stack);
                    popstack(stack);
                    stack.push_back(fValue ? vchTrue : vchFalse);
                }
                break;


                //
                // Crypto
                //
                case OP_RIPEMD160:
                case OP_SHA1:
                case OP_SHA256:
                case OP_HASH160:
                case OP_HASH256:
                {
                    // (in -- hash)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype &vch = stacktop(-1);
                    valtype vchHash((opcode == OP_RIPEMD160 || opcode == OP_SHA1 || opcode == OP_HASH160) ? 20 : 32);
                    if (opcode == OP_RIPEMD160)
                        CRIPEMD160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_SHA1)
                        CSHA1().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_SHA256)
                        CSHA256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_HASH160)
                        CHash160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_HASH256)
                        CHash256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    popstack(stack);
                    stack.push_back(vchHash);
                }
                break;

                case OP_CODESEPARATOR:
                {
                    // Hash starts after the code separator
                    pbegincodehash = pc;
                }
                break;

                case OP_CHECKSIG:
                case OP_CHECKSIGVERIFY:
                {
                    // (sig pubkey -- bool)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    valtype &vchSig = stacktop(-2);
                    valtype &vchPubKey = stacktop(-1);

                    // Subset of script starting at the most recent codeseparator
                    CScript scriptCode(pbegincodehash, pend);

                    // Drop the signature in scripts when SIGHASH_FORKID is
                    // not used.
                    uint32_t nHashType = GetHashType(vchSig);
                    // BU remember the sighashtype so we can use it to choose when to allow this tx
                    sighashtype |= nHashType;

                    // Drop the signature, since there's no way for a signature to sign itself
                    scriptCode.FindAndDelete(CScript(vchSig));

                    if (vchSig.size() != 0)
                        stats.consensusSigCheckCount += 1; // 2020-05-15 sigchecks consensus rule

                    if (!CheckSignatureEncoding(vchSig, flags, serror) ||
                        !CheckPubKeyEncoding(vchPubKey, flags, serror))
                    {
                        // serror is set
                        return false;
                    }
                    bool fSuccess = checker.CheckSig(vchSig, vchPubKey, scriptCode);

                    if (!fSuccess && (flags & SCRIPT_VERIFY_NULLFAIL) && vchSig.size())
                        return set_error(serror, SCRIPT_ERR_SIG_NULLFAIL);

                    popstack(stack);
                    popstack(stack);
                    stack.push_back(fSuccess ? vchTrue : vchFalse);
                    if (opcode == OP_CHECKSIGVERIFY)
                    {
                        if (fSuccess)
                            popstack(stack);
                        else
                            return set_error(serror, SCRIPT_ERR_CHECKSIGVERIFY);
                    }
                }
                break;

                case OP_CHECKMULTISIG:
                case OP_CHECKMULTISIGVERIFY:
                {
                    // ([sig ...] num_of_signatures [pubkey ...] num_of_pubkeys -- bool)

                    int idxKeyCount = 1;
                    if ((int)stack.size() < idxKeyCount)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    int nKeysCount = CScriptNum(stacktop(-idxKeyCount), fRequireMinimal).getint();
                    if (nKeysCount < 0 || nKeysCount > MAX_PUBKEYS_PER_MULTISIG)
                        return set_error(serror, SCRIPT_ERR_PUBKEY_COUNT);
                    stats.nOpCount += nKeysCount;
                    if (stats.nOpCount > maxOps)
                        return set_error(serror, SCRIPT_ERR_OP_COUNT);
                    int idxTopKey = idxKeyCount + 1;

                    // stack depth of nSigsCount
                    const size_t idxSigCount = idxTopKey + nKeysCount;

                    if (stack.size() < idxSigCount)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }

                    const int nSigsCount = CScriptNum(stacktop(-idxSigCount), fRequireMinimal).getint();
                    if (nSigsCount < 0 || nSigsCount > nKeysCount)
                        return set_error(serror, SCRIPT_ERR_SIG_COUNT);

                    // stack depth of the top signature
                    const size_t idxTopSig = idxSigCount + 1;

                    // stack depth of the dummy element
                    const size_t idxDummy = idxTopSig + nSigsCount;
                    if (stack.size() < idxDummy)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }

                    // Subset of script starting at the most recent codeseparator
                    CScript scriptCode(pbegincodehash, pend);

                    // Assuming success is usually a bad idea, but the schnorr path can only succeed.
                    bool fSuccess = true;

                    if ((flags & SCRIPT_ENABLE_SCHNORR_MULTISIG) && stacktop(-idxDummy).size() != 0)
                    {
                        stats.consensusSigCheckCount += nSigsCount; // 2020-05-15 sigchecks consensus rule
                        // SCHNORR MULTISIG
                        static_assert(MAX_PUBKEYS_PER_MULTISIG < 32,
                            "Multisig dummy element decoded as bitfield can't represent more than 32 keys");
                        uint32_t checkBits = 0;

                        // Dummy element is to be interpreted as a bitfield
                        // that represent which pubkeys should be checked.
                        valtype &vchDummy = stacktop(-idxDummy);
                        if (!DecodeBitfield(vchDummy, nKeysCount, checkBits, serror))
                        {
                            // serror is set
                            return false;
                        }

                        // The bitfield doesn't set the right number of
                        // signatures.
                        if (countBits(checkBits) != uint32_t(nSigsCount))
                        {
                            return set_error(serror, SCRIPT_ERR_INVALID_BIT_COUNT);
                        }

                        const size_t idxBottomKey = idxTopKey + nKeysCount - 1;
                        const size_t idxBottomSig = idxTopSig + nSigsCount - 1;

                        int iKey = 0;
                        for (int iSig = 0; iSig < nSigsCount; iSig++, iKey++)
                        {
                            if ((checkBits >> iKey) == 0)
                            {
                                // This is a sanity check and should be unreacheable because we've checked above that
                                // the number of bits in checkBits == the number of signatures.
                                // But just in case this check ensures termination of the subsequent while loop.
                                return set_error(serror, SCRIPT_ERR_INVALID_BIT_RANGE);
                            }

                            // Find the next suitable key.
                            while (((checkBits >> iKey) & 0x01) == 0)
                            {
                                iKey++;
                            }

                            if (iKey >= nKeysCount)
                            {
                                // This is a sanity check and should be unreacheable.
                                return set_error(serror, SCRIPT_ERR_PUBKEY_COUNT);
                            }

                            // Check the signature.
                            valtype &vchSig = stacktop(-idxBottomSig + iSig);
                            valtype &vchPubKey = stacktop(-idxBottomKey + iKey);

                            // Note that only pubkeys associated with a signature are checked for validity.
                            if (!CheckTransactionSchnorrSignatureEncoding(vchSig, flags, serror) ||
                                !CheckPubKeyEncoding(vchPubKey, flags, serror))
                            {
                                // serror is set
                                return false;
                            }

                            // Check signature
                            if (!checker.CheckSig(vchSig, vchPubKey, scriptCode))
                            {
                                // This can fail if the signature is empty, which also is a NULLFAIL error as the
                                // bitfield should have been null in this situation.
                                return set_error(serror, SCRIPT_ERR_SIG_NULLFAIL);
                            }
                        }

                        if ((checkBits >> iKey) != 0)
                        {
                            // This is a sanity check and should be unreacheable.
                            return set_error(serror, SCRIPT_ERR_INVALID_BIT_COUNT);
                        }
                        // If the operation failed, we require that all signatures must be empty vector
                        if (!fSuccess && (flags & SCRIPT_VERIFY_NULLFAIL))
                        {
                            return set_error(serror, SCRIPT_ERR_SIG_NULLFAIL);
                        }
                    }
                    else
                    {
                        // LEGACY MULTISIG (ECDSA / NULL)
                        // 2020-05-15 sigchecks consensus rule
                        // Determine whether all signatures are null
                        bool allNull = true;
                        for (int i = 0; i < nSigsCount; i++)
                        {
                            if (stacktop(-idxTopSig - i).size())
                            {
                                allNull = false;
                                break;
                            }
                        }

                        if (!allNull)
                            stats.consensusSigCheckCount += nKeysCount; // 2020-05-15 sigchecks consensus rule

                        // Remove signature for pre-fork scripts
                        for (int k = 0; k < nSigsCount; k++)
                        {
                            valtype &vchSig = stacktop(-idxTopSig - k);
                            CleanupScriptCode(scriptCode, vchSig, flags);
                        }

                        int nSigsRemaining = nSigsCount;
                        int nKeysRemaining = nKeysCount;
                        while (fSuccess && nSigsRemaining > 0)
                        {
                            valtype &vchSig = stacktop(-idxTopSig - (nSigsCount - nSigsRemaining));
                            valtype &vchPubKey = stacktop(-idxTopKey - (nKeysCount - nKeysRemaining));

                            // Note how this makes the exact order of pubkey/signature evaluation distinguishable
                            // by CHECKMULTISIG NOT if the STRICTENC flag is set. See the script_(in)valid tests for
                            // details.
                            if (!CheckTransactionECDSASignatureEncoding(vchSig, flags, serror) ||
                                !CheckPubKeyEncoding(vchPubKey, flags, serror))
                            {
                                // serror is set
                                return false;
                            }

                            // Check signature
                            bool fOk = checker.CheckSig(vchSig, vchPubKey, scriptCode);

                            if (fOk)
                            {
                                nSigsRemaining--;
                            }
                            nKeysRemaining--;

                            // If there are more signatures left than keys left, then too many signatures have failed.
                            // Exit early, without checking any further signatures.
                            if (nSigsRemaining > nKeysRemaining)
                            {
                                fSuccess = false;
                            }
                        }

                        // If the operation failed, we require that all signatures must be empty vector
                        if (!fSuccess && (flags & SCRIPT_VERIFY_NULLFAIL) && !allNull)
                        {
                            return set_error(serror, SCRIPT_ERR_SIG_NULLFAIL);
                        }
                    }

                    // Clean up stack of all arguments
                    for (size_t i = 0; i < idxDummy; i++)
                    {
                        popstack(stack);
                    }

                    if (opcode == OP_CHECKMULTISIGVERIFY)
                    {
                        if (!fSuccess)
                        {
                            return set_error(serror, SCRIPT_ERR_CHECKMULTISIGVERIFY);
                        }
                    }
                    else
                    {
                        stack.push_back(fSuccess ? vchTrue : vchFalse);
                    }
                }
                break;

                case OP_CHECKDATASIG:
                case OP_CHECKDATASIGVERIFY:
                {
                    // (sig message pubkey -- bool)
                    if (stack.size() < 3)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }

                    valtype &vchSig = stacktop(-3);
                    valtype &vchMessage = stacktop(-2);
                    valtype &vchPubKey = stacktop(-1);

                    if (!CheckDataSignatureEncoding(vchSig, flags, serror) ||
                        !CheckPubKeyEncoding(vchPubKey, flags, serror))
                    {
                        // serror is set
                        return false;
                    }

                    bool fSuccess = false;
                    if (vchSig.size())
                    {
                        valtype vchHash(32);
                        CSHA256().Write(vchMessage.data(), vchMessage.size()).Finalize(vchHash.data());
                        uint256 messagehash(vchHash);
                        CPubKey pubkey(vchPubKey);
                        fSuccess = checker.VerifySignature(vchSig, pubkey, messagehash);
                        stats.consensusSigCheckCount += 1; // 2020-05-15 sigchecks consensus rule
                    }

                    if (!fSuccess && (flags & SCRIPT_VERIFY_NULLFAIL) && vchSig.size())
                    {
                        return set_error(serror, SCRIPT_ERR_SIG_NULLFAIL);
                    }

                    popstack(stack);
                    popstack(stack);
                    popstack(stack);
                    stack.push_back(fSuccess ? vchTrue : vchFalse);
                    if (opcode == OP_CHECKDATASIGVERIFY)
                    {
                        if (fSuccess)
                        {
                            popstack(stack);
                        }
                        else
                        {
                            return set_error(serror, SCRIPT_ERR_CHECKDATASIGVERIFY);
                        }
                    }
                }
                break;

                //
                // Byte string operations
                //
                case OP_CAT:
                {
                    // (x1 x2 -- out)
                    if (stack.size() < 2)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }
                    valtype &vch1 = stacktop(-2);
                    valtype &vch2 = stacktop(-1);
                    if (vch1.size() + vch2.size() > MAX_SCRIPT_ELEMENT_SIZE)
                    {
                        return set_error(serror, SCRIPT_ERR_PUSH_SIZE);
                    }
                    vch1.insert(vch1.end(), vch2.begin(), vch2.end());
                    popstack(stack);
                }
                break;

                case OP_SPLIT:
                {
                    // (in position -- x1 x2)
                    if (stack.size() < 2)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }

                    const valtype &data = stacktop(-2);

                    // Make sure the split point is apropriate.
                    uint64_t position = CScriptNum(stacktop(-1), fRequireMinimal).getint();
                    if (position > data.size())
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_SPLIT_RANGE);
                    }

                    // Prepare the results in their own buffer as `data`
                    // will be invalidated.
                    valtype n1(data.begin(), data.begin() + position);
                    valtype n2(data.begin() + position, data.end());

                    // Replace existing stack values by the new values.
                    stacktop(-2) = std::move(n1);
                    stacktop(-1) = std::move(n2);
                }
                break;

                case OP_REVERSEBYTES:
                {
                    if (!(flags & SCRIPT_ENABLE_OP_REVERSEBYTES))
                    {
                        return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
                    }

                    // (in -- out)
                    if (stack.size() < 1)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }

                    valtype &data = stacktop(-1);
                    std::reverse(data.begin(), data.end());
                }
                break;

                //
                // Conversion operations
                //
                case OP_NUM2BIN:
                {
                    // (in size -- out)
                    if (stack.size() < 2)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }

                    uint64_t size = CScriptNum(stacktop(-1), fRequireMinimal).getint();
                    if (size > MAX_SCRIPT_ELEMENT_SIZE)
                    {
                        return set_error(serror, SCRIPT_ERR_PUSH_SIZE);
                    }

                    popstack(stack);
                    valtype &rawnum = stacktop(-1);

                    // Try to see if we can fit that number in the number of
                    // byte requested.
                    CScriptNum::MinimallyEncode(rawnum);
                    if (rawnum.size() > size)
                    {
                        // We definitively cannot.
                        return set_error(serror, SCRIPT_ERR_IMPOSSIBLE_ENCODING);
                    }

                    // We already have an element of the right size, we
                    // don't need to do anything.
                    if (rawnum.size() == size)
                    {
                        break;
                    }

                    uint8_t signbit = 0x00;
                    if (rawnum.size() > 0)
                    {
                        signbit = rawnum.back() & 0x80;
                        rawnum[rawnum.size() - 1] &= 0x7f;
                    }

                    rawnum.reserve(size);
                    while (rawnum.size() < size - 1)
                    {
                        rawnum.push_back(0x00);
                    }

                    rawnum.push_back(signbit);
                }
                break;

                case OP_BIN2NUM:
                {
                    // (in -- out)
                    if (stack.size() < 1)
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    }

                    valtype &n = stacktop(-1);
                    CScriptNum::MinimallyEncode(n);

                    // The resulting number must be a valid number.
                    if (!CScriptNum::IsMinimallyEncoded(n))
                    {
                        return set_error(serror, SCRIPT_ERR_INVALID_NUMBER_RANGE);
                    }
                }
                break;

                default:
                    return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
                }
            }

            // Size limits
            if (stack.size() + altstack.size() > MAX_STACK_SIZE)
                return set_error(serror, SCRIPT_ERR_STACK_SIZE);
        }
    }
    catch (scriptnum_error &e)
    {
        return set_error(serror, e.errNum);
    }
    catch (...)
    {
        return set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);
    }

    return set_success(serror);
}

bool BaseSignatureChecker::VerifySignature(const std::vector<uint8_t> &vchSig,
    const CPubKey &pubkey,
    const uint256 &sighash) const
{
    if (vchSig.size() == 64)
    {
        return pubkey.VerifySchnorr(sighash, vchSig);
    }
    else
    {
        return pubkey.VerifyECDSA(sighash, vchSig);
    }
}

bool TransactionSignatureChecker::CheckSig(const vector<unsigned char> &vchSigIn,
    const vector<unsigned char> &vchPubKey,
    const CScript &scriptCode) const
{
    CPubKey pubkey(vchPubKey);
    if (!pubkey.IsValid())
        return false;

    // Hash type is one byte tacked on to the end of the signature
    vector<unsigned char> vchSig(vchSigIn);
    if (vchSig.empty())
        return false;
    int nHashType = vchSig.back();
    vchSig.pop_back();

    uint256 sighash;
    size_t nHashed = 0;
    // If BCH sighash is possible, check the bit, otherwise ignore the bit.  This is needed because
    // the bit is undefined (can be any value) before the fork. See block 264084 tx 102
    if (nFlags & SCRIPT_ENABLE_SIGHASH_FORKID)
    {
        if (nHashType & SIGHASH_FORKID)
            sighash = SignatureHash(scriptCode, *txTo, nIn, nHashType, amount, &nHashed);
        else
            return false;
    }
    else
    {
        sighash = SignatureHashLegacy(scriptCode, *txTo, nIn, nHashType, amount, &nHashed);
    }
    nBytesHashed += nHashed;
    ++nSigops;

    if (!VerifySignature(vchSig, pubkey, sighash))
        return false;

    return true;
}

bool TransactionSignatureChecker::CheckLockTime(const CScriptNum &nLockTime) const
{
    // There are two kinds of nLockTime: lock-by-blockheight
    // and lock-by-blocktime, distinguished by whether
    // nLockTime < LOCKTIME_THRESHOLD.
    //
    // We want to compare apples to apples, so fail the script
    // unless the type of nLockTime being tested is the same as
    // the nLockTime in the transaction.
    if (!((txTo->nLockTime < LOCKTIME_THRESHOLD && nLockTime < LOCKTIME_THRESHOLD) ||
            (txTo->nLockTime >= LOCKTIME_THRESHOLD && nLockTime >= LOCKTIME_THRESHOLD)))
        return false;

    // Now that we know we're comparing apples-to-apples, the
    // comparison is a simple numeric one.
    if (nLockTime > (int64_t)txTo->nLockTime)
        return false;

    // Finally the nLockTime feature can be disabled and thus
    // CHECKLOCKTIMEVERIFY bypassed if every txin has been
    // finalized by setting nSequence to maxint. The
    // transaction would be allowed into the blockchain, making
    // the opcode ineffective.
    //
    // Testing if this vin is not final is sufficient to
    // prevent this condition. Alternatively we could test all
    // inputs, but testing just this input minimizes the data
    // required to prove correct CHECKLOCKTIMEVERIFY execution.
    if (CTxIn::SEQUENCE_FINAL == txTo->vin[nIn].nSequence)
        return false;

    return true;
}

bool TransactionSignatureChecker::CheckSequence(const CScriptNum &nSequence) const
{
    // Relative lock times are supported by comparing the passed
    // in operand to the sequence number of the input.
    const int64_t txToSequence = (int64_t)txTo->vin[nIn].nSequence;

    // Fail if the transaction's version number is not set high
    // enough to trigger BIP 68 rules.
    if (static_cast<uint32_t>(txTo->nVersion) < 2)
        return false;

    // Sequence numbers with their most significant bit set are not
    // consensus constrained. Testing that the transaction's sequence
    // number do not have this bit set prevents using this property
    // to get around a CHECKSEQUENCEVERIFY check.
    if (txToSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG)
        return false;

    // Mask off any bits that do not have consensus-enforced meaning
    // before doing the integer comparisons
    const uint32_t nLockTimeMask = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | CTxIn::SEQUENCE_LOCKTIME_MASK;
    const int64_t txToSequenceMasked = txToSequence & nLockTimeMask;
    const CScriptNum nSequenceMasked = nSequence & nLockTimeMask;

    // There are two kinds of nSequence: lock-by-blockheight
    // and lock-by-blocktime, distinguished by whether
    // nSequenceMasked < CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG.
    //
    // We want to compare apples to apples, so fail the script
    // unless the type of nSequenceMasked being tested is the same as
    // the nSequenceMasked in the transaction.
    if (!((txToSequenceMasked < CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG &&
              nSequenceMasked < CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) ||
            (txToSequenceMasked >= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG &&
                nSequenceMasked >= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG)))
    {
        return false;
    }

    // Now that we know we're comparing apples-to-apples, the
    // comparison is a simple numeric one.
    if (nSequenceMasked > txToSequenceMasked)
        return false;

    return true;
}

bool VerifyScript(const CScript &scriptSig,
    const CScript &scriptPubKey,
    unsigned int flags,
    unsigned int maxOps,
    const BaseSignatureChecker &checker,
    ScriptError *serror,
    ScriptMachineResourceTracker *tracker)
{
    set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);

    if ((flags & SCRIPT_VERIFY_SIGPUSHONLY) != 0 && !scriptSig.IsPushOnly())
    {
        return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);
    }

    vector<vector<unsigned char> > stackCopy;
    ScriptMachine sm(flags, checker, maxOps, 0xffffffff);
    if (!sm.Eval(scriptSig))
    {
        if (serror)
            *serror = sm.getError();
        return false;
    }
    if (flags & SCRIPT_VERIFY_P2SH)
        stackCopy = sm.getStack();

    sm.ClearAltStack();
    if (!sm.Eval(scriptPubKey))
    {
        if (serror)
            *serror = sm.getError();
        return false;
    }

    {
        const vector<vector<unsigned char> > &smStack = sm.getStack();
        if (smStack.empty())
        {
            return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
        }
        if (CastToBool(smStack.back()) == false)
        {
            return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
        }
    }

    // Additional validation for spend-to-script-hash transactions:
    if ((flags & SCRIPT_VERIFY_P2SH) && scriptPubKey.IsPayToScriptHash())
    {
        // scriptSig must be literals-only or validation fails
        if (!scriptSig.IsPushOnly())
            return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);

        // Restore stack.
        sm.setStack(stackCopy);

        // stack cannot be empty here, because if it was the
        // P2SH  HASH <> EQUAL  scriptPubKey would be evaluated with
        // an empty stack and the EvalScript above would return false.
        assert(!stackCopy.empty());

        const valtype &pubKeySerialized = stackCopy.back();
        CScript pubKey2(pubKeySerialized.begin(), pubKeySerialized.end());
        sm.PopStack();

        // Bail out early if SCRIPT_DISALLOW_SEGWIT_RECOVERY is not set, the
        // redeem script is a p2sh segwit program, and it was the only item
        // pushed onto the stack.
        if ((flags & SCRIPT_DISALLOW_SEGWIT_RECOVERY) == 0 && sm.getStack().empty() && pubKey2.IsWitnessProgram())
        {
            return set_success(serror);
        }

        sm.ClearAltStack();
        if (!sm.Eval(pubKey2))
        {
            if (serror)
                *serror = sm.getError();
            return false;
        }

        {
            const vector<vector<unsigned char> > &smStack = sm.getStack();
            if (smStack.empty())
            {
                return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
            }
            if (!CastToBool(smStack.back()))
            {
                return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
            }
        }
    }

    if (tracker)
    {
        auto smStats = sm.getStats();
        tracker->update(smStats);
    }

    // The CLEANSTACK check is only performed after potential P2SH evaluation,
    // as the non-P2SH evaluation of a P2SH script will obviously not result in
    // a clean stack (the P2SH inputs remain).
    if ((flags & SCRIPT_VERIFY_CLEANSTACK) != 0)
    {
        // Disallow CLEANSTACK without P2SH, as otherwise a switch CLEANSTACK->P2SH+CLEANSTACK
        // would be possible, which is not a softfork (and P2SH should be one).
        assert((flags & SCRIPT_VERIFY_P2SH) != 0);
        if (sm.getStack().size() != 1)
        {
            return set_error(serror, SCRIPT_ERR_CLEANSTACK);
        }
    }

    return set_success(serror);
}
