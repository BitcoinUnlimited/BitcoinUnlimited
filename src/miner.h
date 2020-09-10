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

#include "boost/multi_index/ordered_index.hpp"
#include "boost/multi_index_container.hpp"

class CBlockIndex;
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_coinbaseFlags;

extern std::atomic<int64_t> nTotalPackage;
extern std::atomic<int64_t> nTotalScore;
extern CTweak<bool> miningCPFP;

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


/** Comparator for CTxMemPool::txiter objects.
 *  It simply compares the internal memory address of the CTxMemPoolEntry object
 *  pointed to. This means it has no meaning, and is only useful for using them
 *  as key in other indexes.
 */
struct CompareCTxMemPoolIter
{
    bool operator()(const CTxMemPool::txiter &a, const CTxMemPool::txiter &b) const { return &(*a) < &(*b); }
};

/** A comparator that sorts transactions based on number of ancestors.
 * This is sufficient to sort an ancestor package in an order that is valid
 * to appear in a block.
 */
struct CompareTxIterByAncestorCount
{
    bool operator()(const CTxMemPool::txiter &a, const CTxMemPool::txiter &b)
    {
        if (a->GetCountWithAncestors() != b->GetCountWithAncestors())
            return a->GetCountWithAncestors() < b->GetCountWithAncestors();
        return CTxMemPool::CompareIteratorByHash()(a, b);
    }
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

    bool may2020Enabled = false;
    uint64_t maxSigOpsAllowed = 0;

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

    /** Add transactions based on feerate including unconfirmed ancestors */
    void addPackageTxs(std::vector<const CTxMemPoolEntry *> *vtxe, bool fCanonical);

    // helper function for addScoreTxs and addPriorityTxs
    bool IsIncrementallyGood(uint64_t nExtraSize, unsigned int nExtraSigOps);
    /** Test if tx will still "fit" in the block */
    bool TestForBlock(CTxMemPool::txiter iter);
    /** Test if tx still has unconfirmed parents not yet in block */
    bool isStillDependent(CTxMemPool::txiter iter);

    /** Bytes to reserve for coinbase and block header */
    uint64_t reserveBlockSize(const CScript &scriptPubKeyIn, int64_t coinbaseSize = -1);

    /** Constructs a coinbase transaction */
    CTransactionRef coinbaseTx(const CScript &scriptPubKeyIn, int nHeight, CAmount nValue);

    // helper functions for addPackageTxs()
    /** Test whether a package, if added to the block, would make the block exceed the sigops limits */
    bool TestPackageSigOps(uint64_t packageSize, unsigned int packageSigOps);
    /** Test if a set of transactions are all final */
    bool TestPackageFinality(const CTxMemPool::setEntries &package);
    /** Sort the package in an order that is valid to appear in a block */
    void SortForBlock(const CTxMemPool::setEntries &package, std::vector<CTxMemPool::txiter> &sortedEntries);
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
UniValue mkblocktemplate(const UniValue &params,
    int64_t coinbaseSize = -1,
    CBlock *pblockOut = nullptr,
    const CScript &coinbaseScript = CScript());

// Force block template recalculation the next time a template is requested
void SignalBlockTemplateChange();

#endif // BITCOIN_MINER_H
