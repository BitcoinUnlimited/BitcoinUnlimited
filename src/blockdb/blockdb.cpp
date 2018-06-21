// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "blockdb.h"
#include "hash.h"

CBlockDB *pblockdb = nullptr;
CBlockDB *pblockundodb = nullptr;

CBlockDB::CBlockDB(std::string folder, size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blockdb" / folder.c_str(), nCacheSize, fMemory, fWipe)
{
}

// Writes a whole array of blocks, at some point a rename of this method should be considered
bool CBlockDB::WriteBatchSync(const std::vector<CBlock> &blocks)
{
    CDBBatch batch(*this);
    for (const CBlock &it : blocks)
    {
        batch.Write(it.GetHash(), BlockDBValue(it));
    }
    return WriteBatch(batch, true);
}

bool WriteBlockToDB(const CBlock &block)
{
    BlockDBValue value(block);
    return pblockdb->Write(block.GetHash(), value);
}

bool ReadBlockFromDB(const CBlockIndex *pindex, BlockDBValue &value)
{
    return pblockdb->Read(pindex->GetBlockHash(), value);
}

bool UndoWriteToDB(const CBlockUndo &blockundo, const uint256 &hashBlock)
{
    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    UndoDBValue value(hasher.GetHash(), hashBlock, blockundo);
    return pblockundodb->Write(hashBlock, value);
}

bool UndoReadFromDB(CBlockUndo &blockundo, const uint256 &hashBlock)
{
    // Read block
    UndoDBValue value;
    if(!pblockundodb->Read(hashBlock, value))
    {
        return error("%s: failure to read undoblock from db", __func__);
    }
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << value.hashBlock;
    hasher << value.blockundo;
    // Verify checksum
    if (value.hashChecksum != hasher.GetHash())
    {
        return error("%s: Checksum mismatch", __func__);
    }
    blockundo = value.blockundo;
    return true;
}

uint64_t FindFilesToPruneLevelDB(uint64_t nLastBlockWeCanPrune)
{
    std::vector<uint256> hashesToPrune;
    /// just remove the to be pruned blocks here in the case of leveldb storage
    boost::scoped_ptr<CDBIterator> pcursor(pblockdb->NewIterator());
    pcursor->Seek(uint256());
    // Load mapBlockIndex
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key))
        {
            BlockDBValue diskblock;
            if (pcursor->GetValue(diskblock))
            {
                if(diskblock.blockHeight <= nLastBlockWeCanPrune)
                {
                    /// unsafe to alter a set of data as we iterate through it so store hashes to be deleted in a
                    hashesToPrune.push_back(diskblock.block.GetHash());
                }
                pcursor->Next();
            }
            else
            {
                return 0; // error("FindFilesToPrune() : failed to read value");
            }
        }
        else
        {
            break;
        }
    }
    /// this should prune all blocks from the DB that are old enough to prune
    for(std::vector<uint256>::iterator iter = hashesToPrune.begin(); iter != hashesToPrune.end(); ++iter)
    {
        pblockdb->Erase(*iter);
    }
    return hashesToPrune.size();
}
