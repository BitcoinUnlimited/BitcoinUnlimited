// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASHLIB_H
#define CASHLIB_H

#include "stdint.h"
/** Convert binary data to a hex string.  The provided result buffer must be 2*length+1 bytes.
 */
SLAPI int Bin2Hex(unsigned char *val, int length, char *result, unsigned int resultLen);

/** Given a private key, return its corresponding public key */
SLAPI int GetPubKey(unsigned char *keyData, unsigned char *result, unsigned int resultLen);

/** Sign one input of a transaction
    All buffer arguments should be in binary-serialized data.
    The transaction (txData) must contain the COutPoint (tx hash and vout) of all relevant inputs,
    however, it is not necessary to provide the spend script.
*/
SLAPI int SignTx(unsigned char *txData,
    int txbuflen,
    unsigned int inputIdx,
    int64_t inputAmount,
    unsigned char *prevoutScript,
    uint32_t priorScriptLen,
    uint32_t nHashType,
    unsigned char *keyData,
    unsigned char *result,
                      unsigned int resultLen);

/** Calculates the sha256 of data, and places it in result.  Result must be 32 bytes */
SLAPI void sha256(const unsigned char* data, unsigned char len, unsigned char* result);

/** Calculates the double sha256 of data and places it in result. Result must be 32 bytes */
SLAPI void hash256(const unsigned char* data, unsigned char len, unsigned char* result);

/** Calculates the RIPEMD160 of the SHA256 of data and places it in result. Result must be 20 bytes */
SLAPI void hash160(const unsigned char* data, unsigned char len, unsigned char* result);


/** Return random bytes from cryptographically acceptable random sources */
SLAPI int RandomBytes(unsigned char *buf, int num);

#endif /* CASHLIB_H */
