// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <openssl/rand.h>
#include <string>
#include <vector>

/* clang-format off */
// must be first for windows
#include "compat.h"
/* clang-format on */
#include "base58.h"
#include "primitives/transaction.h"
#include "script/sign.h"
#include "streams.h"
#include "uint256.h"
#include "utilstrencodings.h"

#include "stdio.h"

namespace
{
uint256 GetPrevoutHash(const CTransaction &txTo)
{
    CHashWriter ss(SER_GETHASH, 0);
    for (unsigned int n = 0; n < txTo.vin.size(); n++)
    {
        ss << txTo.vin[n].prevout;
    }
    return ss.GetHash();
}

uint256 GetSequenceHash(const CTransaction &txTo)
{
    CHashWriter ss(SER_GETHASH, 0);
    for (unsigned int n = 0; n < txTo.vin.size(); n++)
    {
        ss << txTo.vin[n].nSequence;
    }
    return ss.GetHash();
}

uint256 GetOutputsHash(const CTransaction &txTo)
{
    CHashWriter ss(SER_GETHASH, 0);
    for (unsigned int n = 0; n < txTo.vout.size(); n++)
    {
        ss << txTo.vout[n];
    }
    return ss.GetHash();
}

#if 0 // Not used right now
CKey LoadKey(unsigned char *src)
{
    CKey secret;
    secret.Set(src, src + 32, true);
    return secret;
}

CKey LoadKey(uint256 val)
{
    CKey secret;
    unsigned char *src = val.begin();
    secret.Set(src, src+32, true);
    return secret;
}
#endif

#if 0
// From core_read.cpp #include "core_io.h"
bool DecodeHexTx(CTransaction &tx, const std::string &strHexTx)
{
    if (!IsHex(strHexTx))
        return false;

    std::vector<unsigned char> txData(ParseHex(strHexTx));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    try
    {
        ssData >> tx;
    }
    catch (const std::exception &)
    {
        return false;
    }

    return true;
}

bool DecodeHexTx(CTransaction &tx, const char* cHexTx)
{
    std::string strHexTx(cHexTx);
    return DecodeHexTx(tx, strHexTx);
}
#endif
} // end anon namespace

uint256 SignatureHashBitcoinCash(const CScript &scriptCode,
    const CTransaction &txTo,
    unsigned int nIn,
    uint32_t nHashType,
    const CAmount &amount,
    size_t *nHashedOut)
{
    // printf("sighash: scriptSize %d, nIn %d, nHashType %d, amount %lld\n", scriptCode.size(), nIn, nHashType, amount);
    // for(unsigned int i=0;i<scriptCode.size(); i++)
    //    printf("%02x",scriptCode[i]);
    // printf("\n");
    static const uint256 one(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));

    if (nHashType & SIGHASH_FORKID)
    {
        uint256 hashPrevouts;
        uint256 hashSequence;
        uint256 hashOutputs;

        if (!(nHashType & SIGHASH_ANYONECANPAY))
        {
            hashPrevouts = GetPrevoutHash(txTo);
        }

        if (!(nHashType & SIGHASH_ANYONECANPAY) && (nHashType & 0x1f) != SIGHASH_SINGLE &&
            (nHashType & 0x1f) != SIGHASH_NONE)
        {
            hashSequence = GetSequenceHash(txTo);
        }

        if ((nHashType & 0x1f) != SIGHASH_SINGLE && (nHashType & 0x1f) != SIGHASH_NONE)
        {
            hashOutputs = GetOutputsHash(txTo);
        }
        else if ((nHashType & 0x1f) == SIGHASH_SINGLE && nIn < txTo.vout.size())
        {
            CHashWriter ss(SER_GETHASH, 0);
            ss << txTo.vout[nIn];
            hashOutputs = ss.GetHash();
        }

        CHashWriter ss(SER_GETHASH, 0);
        // Version
        ss << txTo.nVersion;
        // Input prevouts/nSequence (none/all, depending on flags)
        ss << hashPrevouts;
        ss << hashSequence;
        // The input being signed (replacing the scriptSig with scriptCode +
        // amount). The prevout may already be contained in hashPrevout, and the
        // nSequence may already be contain in hashSequence.
        ss << txTo.vin[nIn].prevout;
        ss << static_cast<const CScriptBase &>(scriptCode);
        ss << amount;
        ss << txTo.vin[nIn].nSequence;
        // Outputs (none/one/all, depending on flags)
        ss << hashOutputs;
        // Locktime
        ss << txTo.nLockTime;
        // Sighash type
        ss << nHashType;

        uint256 sighash = ss.GetHash();
        // printf("SigHash: %s\n", sighash.GetHex().c_str());
        return sighash;
    }
    return one;
}


#if 0
std::string SigHash(
    std::string txHex,
    unsigned int inputIdx,
    int64_t inputAmount,
    uint32_t nHashType)
{
    CTransaction tx;
    if (!DecodeHexTx(tx, txHex)) return std::string();
    if (inputIdx >= tx.vin.size()) return std::string();

    CTxIn& inp = tx.vin[inputIdx];
    CScript scriptCode = inp.scriptSig;
    scriptCode.FindAndDelete(CScript(vchSig));

    CAmount amount = inputAmt;
    size_t nHashedOut = 0;
    uint256 ret = SignatureHashBitcoinCash(inp.scriptSig, tx, inputIdx, nHashType, amount, nHashedOut);
    return ret.GetHex();
}



// Note that this does not contain the sighashtype
std::string SignHash(std::string hexhash, std::string hexkey)
{
    std::vector<unsigned char> sig;
    uint256 binkey;
    binkey.SetHex(hexkey);
    CKey key = LoadKey(binkey);

    uint256 hash;
    hash.SetHex(hexhash);

    if (!key.Sign(hash, sig))
        return std::string();
    return GetHex(sig);
}
#endif


#if 0
std::string SignTx(std::string txHex, unsigned int inputIdx, int64_t inputAmount, uint32_t nHashType, std::string hexkey)
{
    CTransaction tx;
    if (!DecodeHexTx(tx, txHex)) return std::string();
    if (inputIdx >= tx.vin.size()) return std::string();

    uint256 binkey;
    binkey.SetHex(hexkey);
    CKey key = LoadKey(binkey);

    uint256 sighash = SigHash(tx, inputIdx, inputAmount, nHashType);

    std::vector<unsigned char> sig;
    if (!key.Sign(sighash, sig))
        return std::string();
    sig.push_back((unsigned char)nHashType);
    return GetHex(sig);
}
#endif

#if 0
extern "C" int SignTxHex(char* txHex, unsigned int inputIdx, int64_t inputAmount, uint32_t nHashType, char* hexkey, char* result, unsigned int resultLen)
{
    CTransaction tx;
    result[0] = 0;
    if (!DecodeHexTx(tx, txHex)) return 0;
    if (inputIdx >= tx.vin.size()) return 0;

    uint256 binkey;
    binkey.SetHex(hexkey);
    CKey key = LoadKey(binkey);

    uint256 sighash = SigHash(tx, inputIdx, inputAmount, nHashType);

    std::vector<unsigned char> sig;
    if (!key.Sign(sighash, sig)) return 0;
    sig.push_back((unsigned char)nHashType);
    std::string s = GetHex(sig);
    if (s.size() >= resultLen) return 0;
    strncpy(result, s.c_str(), resultLen);
    return 1;
}
#endif

#if 0
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
#endif
