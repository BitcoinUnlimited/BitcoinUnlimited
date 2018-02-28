// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SIGCACHE_H
#define BITCOIN_SCRIPT_SIGCACHE_H

#include "script/interpreter.h"

#include <vector>

// DoS prevention: limit cache size to 32MB (over 1000000 entries on 64-bit
// systems). Due to how we count cache size, actual memory usage is slightly
// more (~32.25 MB)
static const unsigned int DEFAULT_MAX_SIG_CACHE_SIZE = 32;

class CPubKey;

class CachingTransactionSignatureChecker : public TransactionSignatureChecker
{
private:
    bool store;

public:
    CachingTransactionSignatureChecker(const CTransaction *txToIn,
        unsigned int nInIn,
        const CAmount &amount,
        unsigned int flags,
        bool storeIn = true)
        : TransactionSignatureChecker(txToIn, nInIn, amount, flags), store(storeIn)
    {
    }

    bool VerifySignature(const std::vector<unsigned char> &vchSig,
        const CPubKey &vchPubKey,
        const uint256 &sighash) const;
};

void InitSignatureCache();

#endif // BITCOIN_SCRIPT_SIGCACHE_H
