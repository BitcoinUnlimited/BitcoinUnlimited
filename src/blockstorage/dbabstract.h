// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DBABSTRACT_H
#define BITCOIN_DBABSTRACT_H

#include "chain.h"
#include "undo.h"

enum BlockDBMode
{
    SEQUENTIAL_BLOCK_FILES, // 0
    LEVELDB_BLOCK_STORAGE, // 1

    END_STORAGE_OPTIONS // should always be the last option in the list
};

/**
 * Abstract database class that must be used as the base class for all supported databases
 * This allows us to use one "polymorphic pointer" for all database suport without editing the
 * code to check for BLOCK_DB_MODE and change function calls accordingly
 *
 * NOTE: not databases will use CondenseBlockData or CondenseUndoData because the database either
 * does not need or does not support data compaction.In this case the function should just return
 * immidiately
 */
class CDatabaseAbstract
{
public:
    //! Write a block to the database
    virtual bool WriteBlock(const CBlock &block) = 0;

    //! Read a block from the database
    virtual bool ReadBlock(const CBlockIndex *pindex, CBlock &block) = 0;

    //! Remove a block from the database
    virtual bool EraseBlock(CBlock &block) = 0;

    //! remove a block from the database using the blockindex
    virtual bool EraseBlock(const CBlockIndex *pindex) = 0;

    // clean up the block data if supported by the db
    virtual void CondenseBlockData(const std::string &start, const std::string &end) = 0;

    //! Write undo data to the database
    virtual bool WriteUndo(const CBlockUndo &blockundo, const CBlockIndex *pindex) = 0;

    //! Read  undo data from the database
    virtual bool ReadUndo(CBlockUndo &blockundo, const CBlockIndex *pindex) = 0;

    //! Remove undo data from the database
    virtual bool EraseUndo(const CBlockIndex *pindex) = 0;

    // clean up the undo data if supported by the db
    virtual void CondenseUndoData(const std::string &start, const std::string &end) = 0;

    // prune the database
    virtual uint64_t PruneDB(uint64_t nLastBlockWeCanPrune) = 0;

    virtual ~CDatabaseAbstract() {}
};

#endif // BITCOIN_DBABSTRACT_H
