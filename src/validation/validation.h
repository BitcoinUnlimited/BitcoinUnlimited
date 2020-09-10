
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_H
#define BITCOIN_VALIDATION_H

#include "chainparams.h"
#include "consensus/validation.h"
#include "forks.h"
#include "parallel.h"
#include "txdebugger.h"
#include "txmempool.h"
#include "versionbits.h"

extern std::atomic<uint64_t> nBlockSizeAtChainTip;

enum DisconnectResult
{
    DISCONNECT_OK, // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED // Something else went wrong.
};

/** Context-independent validity checks */
bool CheckBlockHeader(const CBlockHeader &block, CValidationState &state, bool fCheckPOW = true);

/** Context-dependent validity header checks */
bool ContextualCheckBlockHeader(const CBlockHeader &block, CValidationState &state, CBlockIndex *pindexPrev);

bool AcceptBlockHeader(const CBlockHeader &block,
    CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex **ppindex = nullptr);

CBlockIndex *AddToBlockIndex(const CBlockHeader &block);

/** Create a new block index entry for a given block hash */
CBlockIndex *InsertBlockIndex(const uint256 &hash);

/** Look up the block index entry for a given block hash. returns nullptr if it does not exist */
CBlockIndex *LookupBlockIndex(const uint256 &hash);


/** Unload database information */
void UnloadBlockIndex();

/** Load the block tree and coins database from disk */
bool LoadBlockIndex();

/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex(const CChainParams &chainparams);

void CheckBlockIndex(const Consensus::Params &consensusParams);

/**
 * Check whether all inputs of this transaction are valid (no double spends, scripts & sigs, amounts)
 * This does not modify the UTXO set. If pvChecks is not nullptr, script checks are pushed onto it
 * instead of being performed inline.
 */
bool CheckInputs(const CTransactionRef &tx,
    CValidationState &state,
    const CCoinsViewCache &view,
    bool fScriptChecks,
    unsigned int flags,
    unsigned int maxOps,
    bool cacheStore,
    ValidationResourceTracker *resourceTracker,
    std::vector<CScriptCheck> *pvChecks = nullptr,
    unsigned char *sighashType = nullptr,
    CValidationDebugger *debugger = nullptr);

/** Remove invalidity status from a block and its descendants. */
bool ReconsiderBlock(CValidationState &state, CBlockIndex *pindex);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main
 * held) */
bool TestBlockValidity(CValidationState &state,
    const CChainParams &chainparams,
    const CBlock &block,
    CBlockIndex *pindexPrev,
    bool fCheckPOW = true,
    bool fCheckMerkleRoot = true);

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params &consensusParams);

/**
 * Determine what nVersion a new block should use.
 */
int32_t ComputeBlockVersion(const CBlockIndex *pindexPrev, const Consensus::Params &params);

CBlockIndex *FindMostWorkChain();

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState &state, const Consensus::Params &consensusParams, CBlockIndex *pindex);

void InvalidChainFound(CBlockIndex *pindexNew);

/** Context-dependent validity block checks */
bool ContextualCheckBlock(const CBlock &block, CValidationState &state, CBlockIndex *pindexPrev);

// BU: returns the blocksize if block is valid.  Otherwise 0
bool CheckBlock(const CBlock &block, CValidationState &state, bool fCheckPOW = true, bool fCheckMerkleRoot = true);

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindexNew,
    const CDiskBlockPos &pos);

uint32_t GetBlockScriptFlags(const CBlockIndex *pindex, const Consensus::Params &consensusparams);

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  In case pfClean is provided, operation will try to be tolerant about errors, and *pfClean
 *  will be true if no problems were found. Otherwise, the return value will be false in case
 *  of problems. Note that in any case, coins may be modified. */
DisconnectResult DisconnectBlock(const CBlock &block, const CBlockIndex *pindex, CCoinsViewCache &view);

/** Apply the effects of this block (with given index) on the UTXO set represented by coins */
bool ConnectBlock(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const CChainParams &chainparams,
    bool fJustCheck = false,
    bool fParallel = false);

/** Disconnect the current chainActive.Tip() */
bool DisconnectTip(CValidationState &state, const Consensus::Params &consensusParams, const bool fRollBack = false);

/** Find the best known block, and make it the tip of the block chain */
bool ActivateBestChain(CValidationState &state,
    const CChainParams &chainparams,
    const CBlock *pblock = nullptr,
    bool fParallel = false,
    CNode *pfrom = nullptr);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during
 * validation/connection/etc of otherwise unrelated blocks during reorganisation; or it may be set to an Invalid state
 * if pblock is itself invalid (but this is not guaranteed even when the block is checked). If you want to *possibly*
 * get feedback on whether pblock is valid, you must also install a CValidationInterface (see validationinterface.h) -
 * this will have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be
 * penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used for non-network block sources and
 * whitelisted peers.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(CValidationState &state,
    const CChainParams &chainparams,
    CNode *pfrom,
    const CBlock *pblock,
    bool fForceProcessing,
    CDiskBlockPos *dbp,
    bool fParallel);

//! Check whether the block associated with this index entry is pruned or not.
bool IsBlockPruned(const CBlockIndex *pblockindex);

#endif
