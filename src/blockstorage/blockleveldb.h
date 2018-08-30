// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKDB_H
#define BLOCKDB_H

#include "chain.h"
#include "dbabstract.h"
#include "dbwrapper.h"
#include "primitives/block.h"
#include "uint256.h"
#include "undo.h"

/**
 * A note on UndoDBValue
 *
 * we use a pointer for serialization and a special method for deserialization
 * in order to prevent extra needless copies of large chunks of blockdata or
 * undo data which hinders performance
 */
struct UndoDBValue
{
    uint256 hashChecksum;
    uint256 hashBlock;
    const CBlockUndo *blockundo;

    UndoDBValue()
    {
        hashChecksum.SetNull();
        hashBlock.SetNull();
        blockundo = nullptr;
    }

    UndoDBValue(const uint256 &_hashChecksum, const uint256 &_hashBlock, const CBlockUndo *_blockundo)
    {
        this->hashChecksum = _hashChecksum;
        this->hashBlock = _hashBlock;
        this->blockundo = _blockundo;
    }

    template <typename Stream>
    void Serialize(Stream &s) const
    {
        s << FLATDATA(hashChecksum);
        s << FLATDATA(hashBlock);
        s << *blockundo;
    }

    template <typename Stream>
    void Unserialize(Stream &s, CBlockUndo &_block) const
    {
        s >> FLATDATA(hashChecksum);
        s >> FLATDATA(hashBlock);
        s >> _block;
    }
};

/** Access to the block database (blocks/ * /) */
class CBlockLevelDB : public CDatabaseAbstract
{
public:
    CBlockLevelDB(size_t nCacheSizeBlock,
        size_t nCacheSizeUndo,
        bool fMemory = false,
        bool fWipe = false,
        bool obfuscate = false);

private:
    CBlockLevelDB(const CBlockLevelDB &);
    void operator=(const CBlockLevelDB &);

    CDBWrapper *pwrapperblock;
    CDBWrapper *pwrapperundo;

public:
    // clean up our internal pointers
    ~CBlockLevelDB()
    {
        delete pwrapperblock;
        pwrapperblock = nullptr;
        delete pwrapperundo;
        pwrapperundo = nullptr;
    }

    // we need a custom read function to account for the way we want to deserialize undodbvalue
    template <typename K>
    bool ReadUndoInternal(const K &key, UndoDBValue &value, CBlockUndo &blockundo) const
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        std::string strValue;
        leveldb::Status status = pwrapperundo->getpdb()->Get(pwrapperundo->getreadoptions(), slKey, &strValue);
        if (!status.ok())
        {
            if (status.IsNotFound())
                return false;
            LOGA("LevelDB read failure: %s\n", status.ToString());
            dbwrapper_private::HandleError(status);
        }
        try
        {
            CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
            ssValue.Xor(pwrapperundo->getobfuscate_key());
            value.Unserialize(ssValue, blockundo);
        }
        catch (const std::exception &)
        {
            return false;
        }
        return true;
    }

    bool WriteBlock(const CBlock &block);
    bool ReadBlock(const CBlockIndex *pindex, CBlock &block);
    bool EraseBlock(CBlock &block);
    bool EraseBlock(const CBlockIndex *pindex);

    void CondenseBlockData(const std::string &key_begin, const std::string &key_end)
    {
        CDataStream ssKey1(SER_DISK, CLIENT_VERSION), ssKey2(SER_DISK, CLIENT_VERSION);
        ssKey1.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey2.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey1 << key_begin;
        ssKey2 << key_end;
        leveldb::Slice slKey1(ssKey1.data(), ssKey1.size());
        leveldb::Slice slKey2(ssKey2.data(), ssKey2.size());
        pwrapperblock->getpdb()->CompactRange(&slKey1, &slKey2);
    }

    bool WriteUndo(const CBlockUndo &blockundo, const CBlockIndex *pindex);
    bool ReadUndo(CBlockUndo &blockundo, const CBlockIndex *pindex);
    bool EraseUndo(const CBlockIndex *pindex);

    void CondenseUndoData(const std::string &key_begin, const std::string &key_end)
    {
        CDataStream ssKey1(SER_DISK, CLIENT_VERSION), ssKey2(SER_DISK, CLIENT_VERSION);
        ssKey1.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey2.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey1 << key_begin;
        ssKey2 << key_end;
        leveldb::Slice slKey1(ssKey1.data(), ssKey1.size());
        leveldb::Slice slKey2(ssKey2.data(), ssKey2.size());
        pwrapperundo->getpdb()->CompactRange(&slKey1, &slKey2);
    }

    uint64_t PruneDB(uint64_t nLastBlockWeCanPrune);
};

#endif // BLOCKDB_H
