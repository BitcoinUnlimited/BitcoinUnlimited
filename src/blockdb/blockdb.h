// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKDB_H
#define BLOCKDB_H

#include "uint256.h"
#include "dbwrapper.h"
#include "chain.h"
#include "primitives/block.h"
#include "undo.h"

struct BlockDBValue
{
    int32_t blockVersion;
    uint64_t blockHeight;
    CBlock block;

    BlockDBValue()
    {
        SetNull();
    }

    BlockDBValue(const CBlock &_block)
    {
        assert(_block.IsNull() == false);
        this->block = _block;
        this->blockVersion = this->block.nVersion;
        this->blockHeight = this->block.GetHeight();
    }

    template <typename Stream>
    void Serialize(Stream &s) const
    {
        s << VARINT(blockVersion);
        s << VARINT(blockHeight);
        s << block;
    }

    template <typename Stream>
    void Unserialize(Stream &s)
    {
        s >> VARINT(blockVersion);
        s >> VARINT(blockHeight);
        s >> block;
    }

    void SetNull()
    {
        blockVersion = 0;
        blockHeight = 0;
        block.SetNull();
    }
};

struct UndoDBValue
{
    uint256 hashChecksum;
    uint256 hashBlock;
    CBlockUndo blockundo;

    UndoDBValue()
    {
        SetNull();
    }

    UndoDBValue(const uint256 &_hashChecksum, const uint256 &_hashBlock, const CBlockUndo &_blockundo)
    {
        this->hashChecksum = _hashChecksum;
        this->hashBlock = _hashBlock;
        this->blockundo = _blockundo;
    }

    template <typename Stream>
    void Serialize(Stream &s) const
    {
        s << VARINT(hashChecksum);
        s << VARINT(hashBlock);
        s << blockundo;
    }

    template <typename Stream>
    void Unserialize(Stream &s)
    {
        s >> VARINT(hashChecksum);
        s >> VARINT(hashBlock);
        s >> blockundo;
    }

    void SetNull()
    {
        hashChecksum.SetNull();
        hashBlock.SetNull();
        blockundo.vtxundo.clear();
    }
};

/** Access to the block database (blocks/blockdb/) */
class CBlockDB : public CDBWrapper
{
public:
    CBlockDB(std::string folder, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CBlockDB(const CBlockDB &);
    void operator=(const CBlockDB &);

public:
    bool WriteBatchSync(const std::vector<CBlock> &blocks);
};

extern CBlockDB *pblockdb;
extern CBlockDB *pblockundodb;

bool WriteBlockToDB(const CBlock &block);
bool ReadBlockFromDB(const CBlockIndex *pindex, BlockDBValue &value);

bool UndoWriteToDB(const CBlockUndo &blockundo, const uint256 &hashBlock);
bool UndoReadFromDB(CBlockUndo &blockundo, const uint256 &hashBlock);

uint64_t FindFilesToPruneLevelDB(uint64_t nLastBlockWeCanPrune);

#endif // BLOCKDB_H
