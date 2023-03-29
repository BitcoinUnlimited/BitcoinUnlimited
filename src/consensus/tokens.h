// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_TOKENS_H
#define BITCOIN_CONSENSUS_TOKENS_H


#include <cstdint>
#include <tuple>

class CCoinsViewCache;
class CTransaction;
class CValidationState;
class CTxOut;
class COutPoint;

/// The only purpose of this interface is to avoid pulling coins.cpp into libbitcoincash.
/// Pulling in coins.cpp would cause a lot of additional files to be pulled in.
class TokenCoinAccessor {
    public:
    virtual std::tuple<bool, CTxOut, uint32_t> AccessCoin(const COutPoint&) const = 0;
    virtual ~TokenCoinAccessor() { };
};

/**
 * Check all consensus rules for token spends. Note that this must be called regardless of whether SCRIPT_ENABLE_TOKENS
 * is set in `scriptFlags` because even pre-activation we must preserve "unupgraded" behavior of node (reject all
 * inputs that had PREFIX_BYTE at wrappedScriptPubKey[0]).
 *
 * If SCRIPT_ENABLE_TOKENS is set, then this function will validate that the txn spends and/or mints tokens properly.
 * All txns in the block (including the coinbase tx) and/or all txns coming into the mempool should be checked against
 * this function.
 *
 * This function does *not* do dupe input checks, or non-token amount checks. As such, it is prudent to call this
 * function before script checks are done (e.g. before CheckInputs()) but do call this function *after*
 * CheckRegularTransaction() and Consensus::CheckTxInputs() are called on a txn.
 *
 * @pre `tx` has already had CheckRegularTransaction() and Consensus::CheckTxInputs() called on it. `tx` may be any
 *      coinbase or non-coinbase txn.
 * @param firstTokenEnabledBlockHeight - The height of the first block that can contain legitimate tokens.  Pass
 *        std::numeric_limits<int64_t>::max() if upgrade9 is not activated yet.
 * @return true if the tx passed all token-related consensus checks, false otherwise.
 */
bool CheckTxTokens(const CTransaction &tx, CValidationState &state,
                    const TokenCoinAccessor &view, uint32_t scriptFlags,
                   int64_t firstTokenEnabledBlockHeight);

#endif
