// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include "consensus/params.h"

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

arith_uint256 CalculateASERT(const arith_uint256 &refTarget,
    const int64_t nPowTargetSpacing,
    const int64_t nTimeDiff,
    const int64_t nHeightDiff,
    const arith_uint256 &powLimit,
    const int64_t nHalfLife) noexcept;

uint32_t GetNextASERTWorkRequired(const CBlockIndex *pindexPrev,
    const CBlockHeader *pblock,
    const Consensus::Params &params,
    const CBlockIndex *pindexReferenceBlock) noexcept;

/**
 * ASERT caches a special block index for efficiency. If block indices are
 * freed then this needs to be called to ensure no dangling pointer when a new
 * block tree is created.
 * (this is temporary and will be removed after the ASERT constants are fixed)
 */
void ResetASERTAnchorBlockCache() noexcept;

/**
 * For testing purposes - get the current ASERT cache block.
 */
const CBlockIndex *GetASERTAnchorBlockCache() noexcept;

unsigned int GetNextWorkRequired(const CBlockIndex *pindexLast, const CBlockHeader *pblock, const Consensus::Params &);
unsigned int CalculateNextWorkRequired(const CBlockIndex *pindexLast,
    int64_t nFirstBlockTime,
    const Consensus::Params &);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params &);
/** Get block's work: that is the work equivalent for the nBits of difficulty specified in this block */
arith_uint256 GetBlockProof(const CBlockIndex &block);

/** Return the time it would take to redo the work difference between from and to, assuming the current hashrate
 * corresponds to the difficulty at tip, in seconds. */
int64_t GetBlockProofEquivalentTime(const CBlockIndex &to,
    const CBlockIndex &from,
    const CBlockIndex &tip,
    const Consensus::Params &);

/**
 * Bitcoin cash's difficulty adjustment mechanism.
 */
uint32_t GetNextCashWorkRequired(const CBlockIndex *pindexPrev,
    const CBlockHeader *pblock,
    const Consensus::Params &params);

#endif // BITCOIN_POW_H
