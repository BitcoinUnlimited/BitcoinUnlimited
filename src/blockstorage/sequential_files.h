// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKDB_SEQUENTIAL_H
#define BLOCKDB_SEQUENTIAL_H

#include "net.h"
#include "txdb.h"
#include "undo.h"
#include "validationinterface.h"

#include <set>
#include <stdint.h>


/** Open a block file (blk?????.dat) */
FILE *OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly = false);
/** Open an undo file (rev?????.dat) */
FILE *OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);
/** Translation to a filesystem path */
fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix);

void FlushBlockFile(bool fFinalize = false);

/**
 *  Actually unlink the specified files
 */
void UnlinkPrunedFiles(std::set<int> &setFilesToPrune);

bool WriteBlockToDiskSequential(const CBlock &block,
    CDiskBlockPos &pos,
    const CMessageHeader::MessageStartChars &messageStart);
bool ReadBlockFromDiskSequential(CBlock &block, const CDiskBlockPos &pos, const Consensus::Params &consensusParams);
void FindFilesToPruneSequential(std::set<int> &setFilesToPrune, uint64_t nPruneAfterHeight);
bool WriteUndoToDiskSequenatial(const CBlockUndo &blockundo,
    CDiskBlockPos &pos,
    const uint256 &hashBlock,
    const CMessageHeader::MessageStartChars &messageStart);
bool ReadUndoFromDiskSequential(CBlockUndo &blockundo, const CDiskBlockPos &pos, const uint256 &hashBlock);


#endif // BLOCKDB_SEQUENTIAL_H
