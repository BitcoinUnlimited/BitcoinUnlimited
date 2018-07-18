// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include "primitives/block.h"
#include "txmempool.h"

#include <memory>
#include <stdint.h>

class CBlockIndex;
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;

extern CScript COINBASE_FLAGS;

namespace Consensus
{
struct Params;
};

static const bool DEFAULT_PRINTPRIORITY = false;

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};

/** Generate a new block, without valid proof-of-work */
class BlockAssembler
{
private:
    const CChainParams &chainparams;

    // Configuration parameters for the block size
    uint64_t nBlockMaxSize, nBlockMinSize;

    // Information on the current status of the block
    uint64_t nBlockSize;
    uint64_t nBlockTx;
    unsigned int nBlockSigOps;
    CAmount nFees;
    std::set<uint256> inBlock;

    // Chain context for the block
    int nHeight;
    int64_t nLockTimeCutoff;

    // Variables used for addScoreTxs and addPriorityTxs
    int lastFewTxs;
    bool blockFinished;

    // will be initialized by IsUAHFforkActiveOnNextBlock
    bool uahfChainBlock;

public:
    BlockAssembler(const CChainParams &chainparams);
    /** Construct a new block template with coinbase to scriptPubKeyIn */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript &scriptPubKeyIn);

private:
    // utility functions
    /** Clear the block's state and prepare for assembling a new block */
    void resetBlock(const CScript &scriptPubKeyIn);
    /** Add a tx to the block */
    void AddToBlock(CBlockTemplate *, const CTxMemPoolEntry *iter);

    // Methods for how to add transactions to a block.
    /** Add transactions from latest weak block. Returns the hash
     (or zero if no weak block has been found to base of off)*/
    uint256 addFromLatestWeakBlock(CBlockTemplate *);
    /** Add transactions based on modified feerate */
    void addScoreTxs(CBlockTemplate *);
    /** Add transactions based on tx "priority" */
    void addPriorityTxs(CBlockTemplate *);

    // helper function for addScoreTxs and addPriorityTxs
    bool IsIncrementallyGood(uint64_t nExtraSize, unsigned int nExtraSigOps);
    /** Test if tx will still "fit" in the block */
    bool TestForBlock(const CTxMemPoolEntry *iter);
    /** Test if tx still has unconfirmed parents not yet in block */
    bool isStillDependent(CTxMemPool::txiter iter);
    /** Bytes to reserve for coinbase and block header */
    uint64_t reserveBlockSize(const CScript &scriptPubKeyIn);
    /** Internal method to construct a new block template */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript &scriptPubKeyIn, bool blockstreamCoreCompatible);
    /** Constructs a coinbase transaction */
    CTransactionRef coinbaseTx(const CScript &scriptPubKeyIn, int nHeight, CAmount nValue, const uint256 &weakhash);
};

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock *pblock, unsigned int &nExtraNonce);
int64_t UpdateTime(CBlockHeader *pblock, const Consensus::Params &consensusParams, const CBlockIndex *pindexPrev);

// TODO: There is no mining.h
// Create mining.h (The next two functions are in mining.cpp) or leave them here ?

/** Submit a mined block */
UniValue SubmitBlock(CBlock &block);
/** Make a block template to send to miners. */
UniValue mkblocktemplate(const UniValue &params, CBlock *pblockOut);

#endif // BITCOIN_MINER_H
