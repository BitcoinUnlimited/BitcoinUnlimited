// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
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
extern CCriticalSection cs_coinbaseFlags;

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
    CTxMemPool::setEntries inBlock;

    // Chain context for the block
    int nHeight;
    int64_t nLockTimeCutoff;

    // Variables used for addScoreTxs and addPriorityTxs
    int lastFewTxs;
    bool blockFinished;

public:
    BlockAssembler(const CChainParams &chainparams);
    /** Construct a new block template with coinbase to scriptPubKeyIn */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript &scriptPubKeyIn, int64_t coinbaseSize = -1);

private:
    // utility functions
    /** Clear the block's state and prepare for assembling a new block */
    void resetBlock(const CScript &scriptPubKeyIn, int64_t coinbaseSize = -1);
    /** Add a tx to the block */
    void AddToBlock(std::vector<const CTxMemPoolEntry *> *vtxe, CTxMemPool::txiter iter);

    // Methods for how to add transactions to a block.
    /** Add transactions based on modified feerate */
    void addScoreTxs(std::vector<const CTxMemPoolEntry *> *vtxe);
    /** Add transactions based on tx "priority" */
    void addPriorityTxs(std::vector<const CTxMemPoolEntry *> *vtxe);

    // helper function for addScoreTxs and addPriorityTxs
    bool IsIncrementallyGood(uint64_t nExtraSize, unsigned int nExtraSigOps);
    /** Test if tx will still "fit" in the block */
    bool TestForBlock(CTxMemPool::txiter iter);
    /** Test if tx still has unconfirmed parents not yet in block */
    bool isStillDependent(CTxMemPool::txiter iter);
    /** Bytes to reserve for coinbase and block header */
    uint64_t reserveBlockSize(const CScript &scriptPubKeyIn, int64_t coinbaseSize = -1);
    /** Internal method to construct a new block template */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript &scriptPubKeyIn,
        bool blockstreamCoreCompatible,
        int64_t coinbaseSize = -1);
    /** Constructs a coinbase transaction */
    CTransactionRef coinbaseTx(const CScript &scriptPubKeyIn, int nHeight, CAmount nValue);
};

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock *pblock, unsigned int &nExtraNonce);
int64_t UpdateTime(CBlockHeader *pblock, const Consensus::Params &consensusParams, const CBlockIndex *pindexPrev);

// TODO: There is no mining.h
// Create mining.h (The next two functions are in mining.cpp) or leave them here ?

/** Submit a mined block */
UniValue SubmitBlock(CBlock &block);
/** Make a block template to send to miners. */
// implemented in mining.cpp
UniValue mkblocktemplate(const UniValue &params, int64_t coinbaseSize = -1, CBlock *pblockOut = nullptr);

// Force block template recalculation the next time a template is requested
void SignalBlockTemplateChange();

#endif // BITCOIN_MINER_H
