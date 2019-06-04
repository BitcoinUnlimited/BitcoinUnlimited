// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockleveldb.h"
#include "main.h"
#include "undo.h"

enum FlushStateMode
{
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};


static const BlockDBMode DEFAULT_BLOCK_DB_MODE = SEQUENTIAL_BLOCK_FILES;
extern BlockDBMode BLOCK_DB_MODE;
extern CDatabaseAbstract *pblockdb;
extern arith_uint256 pruneHashMask;

/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int DEFAULT_BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int DEFAULT_UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB
extern unsigned int blockfile_chunk_size;
extern unsigned int undofile_chunk_size;

void InitializeBlockStorage(const int64_t &_nBlockTreeDBCache,
    const int64_t &_nBlockDBCache,
    const int64_t &_nBlockUndoDBCache);

/** Catch leveldb up with sequential block files */
void SyncStorage(const CChainParams &chainparams);

/** Functions for disk access for blocks */
bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex, const Consensus::Params &consensusParams);
bool WriteBlockToDisk(const CBlock &block, CDiskBlockPos &pos, const CMessageHeader::MessageStartChars &messageStart);

bool WriteUndoToDisk(const CBlockUndo &blockundo,
    CDiskBlockPos &pos,
    const CBlockIndex *pindex,
    const CMessageHeader::MessageStartChars &messageStart);
bool ReadUndoFromDisk(CBlockUndo &blockundo, const CDiskBlockPos &pos, const CBlockIndex *pindex);

/** Flush all state, indexes and buffers to disk. */
bool FlushStateToDiskInternal(CValidationState &state,
    FlushStateMode mode = FLUSH_STATE_ALWAYS,
    bool fFlushForPrune = false,
    std::set<int> setFilesToPrune = {});
bool FlushStateToDisk(CValidationState &state, FlushStateMode mode);
void FlushStateToDisk();
/** Prune block files and flush state to disk. */
void PruneAndFlush();

bool FindBlockPos(CValidationState &state,
    CDiskBlockPos &pos,
    unsigned int nAddSize,
    unsigned int nHeight,
    uint64_t nTime,
    bool fKnown = false);

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);


extern BlockDBMode BLOCK_DB_MODE;
