// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

#include <openssl/rand.h>
#include <string>
#include <vector>


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
