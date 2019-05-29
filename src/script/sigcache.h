// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
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
        const CAmount &amountIn,
        unsigned int flags,
        bool storeIn = true)
        : TransactionSignatureChecker(txToIn, nInIn, amountIn, flags), store(storeIn)
    {
    }

    bool IsCached(const std::vector<uint8_t> &vchSig, const CPubKey &vchPubKey, const uint256 &sighash) const;

    bool VerifySignature(const std::vector<uint8_t> &vchSig,
        const CPubKey &pubkey,
        const uint256 &sighash) const override;
};

void InitSignatureCache();

#endif // BITCOIN_SCRIPT_SIGCACHE_H
