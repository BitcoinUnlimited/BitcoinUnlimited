// Copyright (c) 2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEAKBLOCK_H
#define BITCOIN_WEAKBLOCK_H

#include "uint256.h"
#include "consensus/params.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "uint256.h"

const uint32_t DEFAULT_WEAKBLOCKS_CONSIDER_POW_RATIO=4;
const bool DEFAULT_WEAKBLOCKS_ENABLE=true;

bool weakblocksEnabled();
uint32_t weakblocksConsiderPOWRatio();

// absolute minimum POW target multiplicator - below this, also weak blocks are considered invalid and nodes sending those are penalized and banned
uint32_t weakblocksMinPOWRatio();

//! protects all of the above data structures
extern CCriticalSection cs_weakblocks;

// Internally, a weak block is just a bunch of pointers, to save
// memory compared to storing all transactions as duplicates for
// each weak block
typedef std::vector<CTransaction*> Weakblock;

/* From a block's coinbase transaction, extract the potential candidate hash
   that point to the underlying weak block, as a "OP_RETURN WB <uint256>" scriptPubKey pattern. */
uint256 candidateWeakHash(const CBlock &block);

// Check whether a weak block is underlying a strong block by looking at the transaction contents
bool extendsWeak(const CBlock &block, const Weakblock* underlying);

// Check whether a weak block is underlying another weak block by looking at the transaction contents
bool extendsWeak(const Weakblock *wb, const Weakblock* underlying);

// store CBlock as a weak block return  iff the block was stored, false if it already exists
bool storeWeakblock(const CBlock &block);

// return pointer to a CBlock if a given hash is a stored, or NULL
// returned block needs to be handled with cs_weakblocks locked
// responsibility of memory management is internal to weakblocks module
// the returned block is valid until the next call to purgeOldWeakblocks
const CBlock* blockForWeak(const Weakblock *wb);

// return a weak block. Caller needs to care for cs_weakblocks
const Weakblock* getWeakblock(const uint256& hash);

// give hash of a weak block. Needs to be cs_weakblocks locked
const uint256 HashForWeak(const Weakblock* wb);

// convenience function around getWeakblock
inline bool isKnownWeakblock(const uint256& hash) {
    AssertLockHeld(cs_weakblocks);
    return getWeakblock(hash) != NULL;
}

/*! Return the weak height of a weakblock
  The height is the number of weak blocks that come before this one.
Needs to be called with cs_weakblocks locked. */
int weakHeight(const uint256 wbhash);
int weakHeight(const Weakblock* wb);

/*! Return block from longest and earliest weak chain
  Can return NULL if there is no weak block chain available. */
const Weakblock* getWeakLongestChainTip();

// Remove old weak blocks
// This removes those chain tip that have been marked for removal in the last round
// and marks the current one for removal in the next round.
void purgeOldWeakblocks();

// return a map of weak block hashes to their weak block height, in chronological order of receival
std::vector<std::pair<uint256, int> > weakChainTips();

// return block underlying the given weak block (or NULL)
// This needs to be handled with cs_weakblocks locked
const Weakblock* underlyingWeak(const Weakblock *block);

// currently known number of weak blocks
int numKnownWeakblocks();

// currently known number of transactions appearing in weak blocks
int numKnownWeakblockTransactions();

//! Internal consistency check
/*! To be used only for testing / debugging.
  For each weak block that is registered, this checks that:
  - hash2weakblock and weakblock2hash are consistent

  It also checks that getWeakLongestChainTip() is indeed pointing to one of
  the longest chains of weakblocks.

  Runtime is O(<number-of-weak-blocks>^2)
*/
void weakblocksConsistencyCheck();

//! Consistency check that all internal data structures are empty
void weakblocksEmptyCheck();

#endif
