// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "blockdb.h"
#include "main.h"
#include "hash.h"

CBlockDB *pblockdb = nullptr;
CBlockDB *pblockundodb = nullptr;

CBlockDB::CBlockDB(std::string folder, size_t nCacheSize, bool fMemory, bool fWipe, bool obfuscate, COverrideOptions *pOverride) : CDBWrapper(GetDataDir() / "blockdb" / folder.c_str(), nCacheSize, fMemory, fWipe, obfuscate, pOverride)
{
}

// Writes a whole array of blocks, at some point a rename of this method should be considered
bool CBlockDB::WriteBatchSync(const std::vector<CBlock> &blocks)
{
    CDBBatch batch(*this);
    for (const CBlock &it : blocks)
    {
        batch.Write(it.GetHash(), BlockDBValue(&it));
    }
    return WriteBatch(batch, true);
}

bool WriteBlockToDB(const CBlock &block)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    key << block.GetBlockTime() << ":" << block.GetHash().ToString();

    BlockDBValue value(&block);
    return pblockdb->Write(key.str(), value);
}

bool ReadBlockFromDB(const CBlockIndex *pindex, BlockDBValue &value, CBlock &block)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    key << pindex->GetBlockTime() << ":" << pindex->GetBlockHash().ToString();

    return pblockdb->ReadBlock(key.str(), value, block);
}

bool UndoWriteToDB(const CBlockUndo &blockundo, const CBlockIndex* pindex)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    uint256 hashBlock;
    int64_t nBlockTime = 0;
    if(pindex)
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

bool UndoReadFromDB(CBlockUndo &blockundo, const CBlockIndex* pindex)
{
    // Create a key which will sort the database by the blocktime.  This is needed to prevent unnecessary
    // compactions which hamper performance. Will a key sorted by time the only files that need to undergo
    // compaction are the most recent files only.
    std::ostringstream key;
    uint256 hashBlock;
    int64_t nBlockTime = 0;
    if(pindex)
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
    if(!pblockundodb->ReadUndo(key.str(), value, blockundo))
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
    std::vector<uint256> hashesToPrune;
    BlockMap::iterator iter;
    iter = mapBlockIndex.find(chainActive.Tip()->GetBlockHash());
    if(iter == mapBlockIndex.end())
    {
        return 0;
    }
    CBlockIndex* pindex = iter->second;
    while(pindex->pprev)
    {
        if(pindex->nHeight < nLastBlockWeCanPrune)
        {
            hashesToPrune.push_back(pindex->GetBlockHash());
        }
        pindex = pindex->pprev;
    }
    /// this should prune all blocks from the DB that are old enough to prune
    for(std::vector<uint256>::iterator iter = hashesToPrune.begin(); iter != hashesToPrune.end(); ++iter)
    {
        pblockdb->Erase(*iter);
    }
    return hashesToPrune.size();
}
