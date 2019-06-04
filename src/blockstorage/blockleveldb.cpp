// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "blockleveldb.h"
#include "blockstorage.h"
#include "hashwrapper.h"
#include "main.h"

CBlockLevelDB::CBlockLevelDB(size_t nCacheSizeBlock, size_t nCacheSizeUndo, bool fMemory, bool fWipe, bool obfuscate)
{
    COverrideOptions overrideblock;
    // we want to have much larger file sizes for the blocks db so override the default.
    overrideblock.max_file_size = nCacheSizeBlock / 2;
    pwrapperblock =
        new CDBWrapper(GetDataDir() / "blockdb" / "blocks", nCacheSizeBlock, fMemory, fWipe, obfuscate, &overrideblock);

    COverrideOptions overrideundo;
    // Make the undo file max size larger than the default and also configure the write buffer
    // to be a larger proportion of the overall cache since we don't really need a big read buffer
    // for undo files.
    overrideundo.max_file_size = nCacheSizeUndo;
    overrideundo.write_buffer_size = nCacheSizeUndo / 1.8;
    pwrapperundo =
        new CDBWrapper(GetDataDir() / "blockdb" / "undo", nCacheSizeUndo, fMemory, fWipe, obfuscate, &overrideundo);
}

bool CBlockLevelDB::WriteBlock(const CBlock &block)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    key << block.GetBlockTime() << ":" << block.GetHash().ToString();

    if (IsChainNearlySyncd())
    {
        return pwrapperblock->Write(key.str(), block, true);
    }
    else
    {
        return pwrapperblock->Write(key.str(), block, false);
    }
}

bool CBlockLevelDB::ReadBlock(const CBlockIndex *pindex, CBlock &block)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    key << pindex->GetBlockTime() << ":" << pindex->GetBlockHash().ToString();
    return pwrapperblock->Read(key.str(), block);
}

bool CBlockLevelDB::EraseBlock(CBlock &block)
{
    std::ostringstream key;
    key << block.GetBlockTime() << ":" << block.GetHash().ToString();
    return pwrapperblock->Erase(key.str(), true);
}

bool CBlockLevelDB::EraseBlock(const CBlockIndex *pindex)
{
    std::ostringstream key;
    key << pindex->GetBlockTime() << ":" << pindex->GetBlockHash().ToString();
    return pwrapperblock->Erase(key.str(), true);
}

bool CBlockLevelDB::EraseBlock(const std::string &key) { return pwrapperblock->Erase(key, true); }
bool CBlockLevelDB::WriteUndo(const CBlockUndo &blockundo, const CBlockIndex *pindex)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    uint256 hashBlock;
    int64_t nBlockTime = 0;
    if (pindex)
    {
        hashBlock = pindex->GetBlockHash();
        nBlockTime = pindex->GetBlockTime();
    }
    else
    {
        hashBlock.SetNull();
    }
    key << nBlockTime << ":" << hashBlock.ToString();

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    UndoDBValue value(hasher.GetHash(), hashBlock, &blockundo);

    if (IsChainNearlySyncd())
    {
        return pwrapperundo->Write(key.str(), value, true);
    }
    else
    {
        return pwrapperundo->Write(key.str(), value, false);
    }
}

bool CBlockLevelDB::ReadUndo(CBlockUndo &blockundo, const CBlockIndex *pindex)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    uint256 hashBlock;
    int64_t nBlockTime = 0;
    if (pindex)
    {
        hashBlock = pindex->GetBlockHash();
        nBlockTime = pindex->GetBlockTime();
    }
    else
    {
        hashBlock.SetNull();
    }
    key << nBlockTime << ":" << hashBlock.ToString();

    // Read block
    UndoDBValue value;
    if (!ReadUndoInternal(key.str(), value, blockundo))
    {
        return error("%s: failure to read undoblock from db", __func__);
    }
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << value.hashBlock;
    hasher << blockundo;

    // Verify checksum
    if (value.hashChecksum != hasher.GetHash())
    {
        return error("%s: Checksum mismatch", __func__);
    }
    return true;
}

bool CBlockLevelDB::EraseUndo(const CBlockIndex *pindex)
{
    std::ostringstream key;
    uint256 hashBlock;
    int64_t nBlockTime = 0;
    if (pindex)
    {
        hashBlock = pindex->GetBlockHash();
        nBlockTime = pindex->GetBlockTime();
    }
    else
    {
        return false;
    }
    key << nBlockTime << ":" << hashBlock.ToString();
    return pwrapperundo->Erase(key.str(), true);
}
bool CBlockLevelDB::EraseUndo(const std::string &key) { return pwrapperundo->Erase(key, true); }
