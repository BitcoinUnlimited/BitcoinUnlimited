// Copyright (c) 2017-2017 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_TX_VERIFY_H
#define BITCOIN_CONSENSUS_TX_VERIFY_H

#include "primitives/transaction.h"

#include <stdint.h>
#include <vector>

class CBlockIndex;
class CCoinsViewCache;
class CValidationState;
class CChainParams;
/** Transaction validation functions */

/** Context-independent validity checks */
bool CheckTransaction(const CTransactionRef tx, CValidationState &state);
/** Context-dependent transaction structure validity checks.  Does not include input checking or script execution. */
bool ContextualCheckTransaction(const CTransactionRef tx,
    CValidationState &state,
    CBlockIndex *const pindexPrev,
    const CChainParams &params);

namespace Consensus
{
/**
 * Check whether all inputs of this transaction are valid (no double spends and amounts)
 * This does not modify the UTXO set. This does not check scripts and sigs.
 * Preconditions: tx.IsCoinBase() is false.
 */
bool CheckTxInputs(const CTransactionRef tx, CValidationState &state, const CCoinsViewCache &inputs);
} // namespace Consensus

/** Auxiliary functions for transaction validation (ideally should not be exposed) */

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
unsigned int GetLegacySigOpCount(const CTransactionRef tx, const uint32_t flags);

/**
 * Count ECDSA signature operations in pay-to-script-hash inputs.
 *
 * @param[in] mapInputs Map of previous transactions that have outputs we're spending
 * @return maximum number of sigops required to validate this transaction's inputs
 * @see CTransaction::FetchInputs
 */
unsigned int GetP2SHSigOpCount(const CTransactionRef tx, const CCoinsViewCache &mapInputs, const uint32_t flags);

/**
 * Check if transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 */
bool IsFinalTx(const CTransactionRef tx, int nBlockHeight, int64_t nBlockTime);

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation.
 */
std::pair<int, int64_t> CalculateSequenceLocks(const CTransactionRef tx,
    int flags,
    std::vector<int> *prevHeights,
    const CBlockIndex &block);

bool EvaluateSequenceLocks(const CBlockIndex &block, std::pair<int, int64_t> lockPair);
/**
 * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
 * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
 */
bool SequenceLocks(const CTransactionRef tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block);

uint64_t GetTransactionSigOpCount(const CTransactionRef ptx, const CCoinsViewCache &coins, const uint32_t flags);

#endif // BITCOIN_CONSENSUS_TX_VERIFY_H
