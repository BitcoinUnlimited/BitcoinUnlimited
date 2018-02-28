// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKDB_H
#define BLOCKDB_H

#include "chain.h"
#include "coins.h"
#include "dbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockFileInfo;
class CBlockIndex;
class uint256;

struct BlockDBValue
{
    int32_t blockVersion;
    uint64_t blockHeight;
    CBlock block;

    BlockDBValue()
    {
        SetNull();
    }

    BlockDBValue(const CBlock &block)
    {
        assert(block.IsNull() == false);
        this->block = block;
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

bool WriteBlockToDiskLevelDB(const CBlock &block);
bool ReadBlockFromDiskLevelDB(const CBlockIndex *pindex, BlockDBValue &value);
uint64_t FindFilesToPruneLevelDB(uint64_t nLastBlockWeCanPrune);

/** Access to the block database (blocks/blockdb/) */
class CFullBlockDB : public CDBWrapper
{
public:
    CFullBlockDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CFullBlockDB(const CFullBlockDB &);
    void operator=(const CFullBlockDB &);

public:
    bool WriteBatchSync(const std::vector<CBlock> &blocks);
    bool ReadBlock(const uint256 &hash, BlockDBValue &value);
    bool WriteBlock(const uint256 &hash, const BlockDBValue &value);
    bool EraseBlock(const uint256 &hash);
};

extern CFullBlockDB *pblockfull;

#endif // BLOCKDB_H
