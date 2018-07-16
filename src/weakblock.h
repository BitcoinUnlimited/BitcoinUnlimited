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

//! protects all of the above data structures
extern CCriticalSection cs_weakblocks;

/** Returns true iff weak blocks are enabled. */
bool weakblocksEnabled();

    /** For weak blocks to be considered by this node, the weak block
        must meet a target at least this times smaller than the current
        strong block target.
        This is a value that can be configured live.
    */
uint32_t weakblocksConsiderPOWRatio();

    /** For a weak block to be considered a weak block and not just garbage
        (and reception thus a bannable offense) by this node, the WB
        must meet a target that is at least this times smaller than
        the current strong block target.
    */
uint32_t weakblocksMinPOWRatio();

/** Extract commitment hash for any block in the coinbase
    transaction. Returns a hash value of zero if none is found. */
uint256 weakblocksExtractCommitment(const CBlock* block);

class CWeakblock : public CBlock {
    friend class CWeakStore;
public:
    CWeakblock(const CBlock* other);

    /** Returns true iff other is underlying this weak block - meaning
     * that the underlying one contains the same transactions in same
     * order, except for CB. */
    bool extends(const CBlock* underlying);
    bool extends(const CBlock& underlying);
    bool extends(const ConstCBlockRef& underlying);
    /** Returns the weak block's weak height. It's height is the
     number of weak blocks that come before this one. It is minus one if the
     block is not known as a weak block or when it is marked for removal. */
    int GetWeakHeight() const;
private:
    mutable uint32_t weak_height_cache;
    mutable bool weak_height_cache_valid;
};

typedef std::shared_ptr<CWeakblock> CWeakblockRef;
typedef std::shared_ptr<const CWeakblock> ConstCWeakblockRef;

class CWeakStore {
    friend class CWeakblock;
public:
    /** Register the given block as a weak block. Returns the weak block or nullptr when it failed, respectively. */
    CWeakblockRef store(const CBlock* block);

    /** Purge old weak blocks (but leave enough of them around to help
     * with transmission of the current chain tip!)
     * If the thorough flag is set to true,
     * all weak blocks are expired. */
    void expireOld(const bool fThorough=false);

    /** Look up weak block by its hash. */
    CWeakblockRef byHash(const uint256& hash) const;

    /** Look up underlying weak block. */
    CWeakblockRef parent(const uint256& hash) const;

    /** Return weak blocks chain tip (or nullptr if there's none)
        This is the longest and earliest received (in terms of order of store() calls)
        weak block chain's tip. */
    CWeakblockRef Tip();

    //! Number of known weak blocks
    size_t size() const;

    // returns true if no weak blocks are stored and is equivalent to (::size() == 0)
    bool empty() const;

    /** Runs an internal consistency check that will fail with assert when something
        is broken. FIXME: Not to be used in production, update asserts to exception or DbgAssert. */
    void consistencyCheck() const;

    const std::vector<CWeakblockRef>& chainTips() const;
private:
    //! Map block hash to weak block
    std::map<uint256, CWeakblockRef> hash2wb;

    //! Store DAG edges, so that extends[a] = b means b is the next underlying block for a
    std::map<uint256, uint256> extends_map;

    /*! Store all weak block chain tips, ordered chronologically; meaning that a later received chain tip is further down in this vector. Therefore the weak block chain tip is the one with the largest weak height that comes earliest in this vector. */
    std::vector<CWeakblockRef> chain_tips;

    /** Chain tips pre-marked for removal for the next expireOld(..) call */
    std::set<uint256> to_remove;
};

extern CWeakStore weakstore;

#endif
