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

// current version of the blockdb data structure
const int32_t CURRENT_VERSION = 1;

/**
 * A note on BlockDBValue and UndoDBValue
 *
 * we use a pointer for serialization and a special method for deserialization
 * in order to prevent extra needless copies of large chunks of blockdata or
 * undo data which hinders performance
 */
struct BlockDBValue
{
    int32_t nVersion;
    uint64_t blockHeight;
    const CBlock* block;
    // only used for pruning
    CBlock blocktemp;

    BlockDBValue()
    {
        nVersion = 0;
        blockHeight = 0;
        block = nullptr;
    }

    BlockDBValue(const CBlock* _block)
    {
        assert(_block->IsNull() == false);
        this->block = _block;
        this->nVersion = CURRENT_VERSION;
        this->blockHeight = this->block->GetHeight();
    }

    template <typename Stream>
    void Serialize(Stream &s) const
    {
        // CSerActionSerialize() ser_action();
        ::SerReadWriteMany(s, CSerActionSerialize(), nVersion);
        ::SerReadWriteMany(s, CSerActionSerialize(), blockHeight);
        s << *block;
    }

    template <typename Stream>
    void Unserialize(Stream &s, CBlock& _block) const
    {
        s >> _block;
    }

};

struct UndoDBValue
{
    uint256 hashChecksum;
    uint256 hashBlock;
    const CBlockUndo* blockundo;

    UndoDBValue()
    {
        hashChecksum.SetNull();
        hashBlock.SetNull();
        blockundo = nullptr;
    }

    UndoDBValue(const uint256 &_hashChecksum, const uint256 &_hashBlock, const CBlockUndo* _blockundo)
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
    void Unserialize(Stream &s, CBlockUndo& _block) const
    {
        s >> FLATDATA(hashChecksum);
        s >> FLATDATA(hashBlock);
        s >> _block;
    }
};

/** Access to the block database (blocks/ * /) */
class CBlockDB : public CDBWrapper
{
public:
    CBlockDB(std::string folder, size_t nCacheSize, bool fMemory = false, bool fWipe = false, bool obfuscate = false, COverrideOptions *override = nullptr);

private:
    CBlockDB(const CBlockDB &);
    void operator=(const CBlockDB &);

public:
    bool WriteBatchSync(const std::vector<CBlock> &blocks);

    // we need a custom read functions to account for the way we want to deserialize blockdbvalue and undodbvalue
    template <typename K>
    bool ReadBlock(const K &key, BlockDBValue &value, CBlock& block) const
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        std::string strValue;
        leveldb::Status status = pdb->Get(readoptions, slKey, &strValue);
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
            ssValue.Xor(obfuscate_key);
            ssValue.read((char *)&value.nVersion, sizeof(uint32_t));
            ssValue.read((char *)&value.blockHeight, sizeof(uint64_t));
            value.Unserialize(ssValue, block);
        }
        catch (const std::exception &)
        {
            return false;
        }
        return true;
    }

    template <typename K>
    bool ReadUndo(const K &key, UndoDBValue &value, CBlockUndo& blockundo) const
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        std::string strValue;
        leveldb::Status status = pdb->Get(readoptions, slKey, &strValue);
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
            ssValue.Xor(obfuscate_key);
            value.Unserialize(ssValue, blockundo);
        }
        catch (const std::exception &)
        {
            return false;
        }
        return true;
    }
};

extern CBlockDB *pblockdb;
extern CBlockDB *pblockundodb;

bool WriteBlockToDB(const CBlock &block);
bool ReadBlockFromDB(const CBlockIndex *pindex, BlockDBValue &value, CBlock &block);

bool UndoWriteToDB(const CBlockUndo &blockundo, const CBlockIndex* pindex);
bool UndoReadFromDB(CBlockUndo &blockundo, const CBlockIndex* pindex);

uint64_t FindFilesToPruneLevelDB(uint64_t nLastBlockWeCanPrune);

#endif // BLOCKDB_H
