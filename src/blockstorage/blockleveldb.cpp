// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "blockleveldb.h"
#include "hash.h"
#include "main.h"

CBlockDB *pblockdb = nullptr;
CBlockDB *pblockundodb = nullptr;

CBlockDB::CBlockDB(std::string folder,
    size_t nCacheSize,
    bool fMemory,
    bool fWipe,
    bool obfuscate,
    COverrideOptions *pOverride)
    : CDBWrapper(GetDataDir() / "blockdb" / folder.c_str(), nCacheSize, fMemory, fWipe, obfuscate, pOverride)
{
}

bool WriteBlockToDB(const CBlock &block)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    key << block.GetBlockTime() << ":" << block.GetHash().ToString();

    return pblockdb->Write(key.str(), block);
}

bool ReadBlockFromDB(const CBlockIndex *pindex, CBlock &block)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    key << pindex->GetBlockTime() << ":" << pindex->GetBlockHash().ToString();
    return pblockdb->Read(key.str(), block);
}

bool WriteUndoToDB(const CBlockUndo &blockundo, const CBlockIndex *pindex)
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
    return pblockundodb->Write(key.str(), value);
}

bool ReadUndoFromDB(CBlockUndo &blockundo, const CBlockIndex *pindex)
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
    if (!pblockundodb->ReadUndo(key.str(), value, blockundo))
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

uint64_t FindFilesToPruneLevelDB(uint64_t nLastBlockWeCanPrune)
{
    uint64_t prunedCount = 0;
    CDBBatch blockBatch(*pblockdb);
    CDBBatch undoBatch(*pblockundodb);
    while (!vDbBlockSizes.empty() && nDBUsedSpace >= nPruneTarget)
    {
        uint256 frontHash = vDbBlockSizes.front().first;
        uint64_t frontSize = vDbBlockSizes.front().second;
        BlockMap::iterator iter = mapBlockIndex.find(frontHash);
        std::ostringstream key;
        key << iter->second->GetBlockTime() << ":" << iter->second->GetBlockHash().ToString();
        blockBatch.Erase(key.str());
        undoBatch.Erase(key.str());
        iter->second->nStatus &= ~BLOCK_HAVE_DATA;
        iter->second->nStatus &= ~BLOCK_HAVE_UNDO;
        iter->second->nFile = 0;
        iter->second->nDataPos = 0;
        iter->second->nUndoPos = 0;
        setDirtyBlockIndex.insert(iter->second);
        vDbBlockSizes.erase(vDbBlockSizes.begin());
        prunedCount = prunedCount + 1;
        nDBUsedSpace = nDBUsedSpace - frontSize;
    }
    pblockdb->WriteBatch(blockBatch, true);
    pblockundodb->WriteBatch(undoBatch, true);
    pblockdb->Compact();
    pblockundodb->Compact();
    LOG(PRUNE, "Pruned %u blocks, size on disk %u, data for %u indexes\n", prunedCount, nDBUsedSpace,
        vDbBlockSizes.size());
    return prunedCount;
}
