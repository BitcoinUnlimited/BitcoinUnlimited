// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockdb_leveldb.h"

#include "blockdb_sequential.h"
#include "chain.h"
#include "chainparams.h"
#include "hash.h"
#include "main.h"
#include "pow.h"
#include "ui_interface.h"
#include "uint256.h"
#include "validationinterface.h"

#include <stdint.h>

CFullBlockDB *pblockfull = NULL;

CFullBlockDB::CFullBlockDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blocks" / "blockdb", nCacheSize, fMemory, fWipe)
{
}

// Writes a whole array of blocks, at some point a rename of this method should be considered
bool CFullBlockDB::WriteBatchSync(const std::vector<CBlock> &blocks)
{
    CDBBatch batch(*this);
    for (std::vector<CBlock>::const_iterator it = blocks.begin(); it != blocks.end(); it++)
    {
        batch.Write(it->GetHash(), BlockDBValue(*it));
    }
    return WriteBatch(batch, true);
}

// hash is key, value is {version, height, block}
bool CFullBlockDB::ReadBlock(const uint256 &hash, BlockDBValue &value)
{
    return Read(hash, value);
}

bool CFullBlockDB::WriteBlock(const uint256 &hash, const BlockDBValue &value)
{
    return Write(hash, value);
}

bool CFullBlockDB::EraseBlock(const uint256 &hash)
{
    return Erase(hash);
}



bool WriteBlockToDiskLevelDB(const CBlock &block)
{
    BlockDBValue value(block);
    return pblockfull->Write(block.GetHash(), value);
}

bool ReadBlockFromDiskLevelDB(const CBlockIndex *pindex, BlockDBValue &value)
{
    return pblockfull->ReadBlock(pindex->GetBlockHash(), value);
}

uint64_t FindFilesToPruneLevelDB(uint64_t nLastBlockWeCanPrune)
{
    std::vector<uint256> hashesToPrune;
    /// just remove the to be pruned blocks here in the case of leveldb storage
    boost::scoped_ptr<CDBIterator> pcursor(pblockfull->NewIterator());
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
        pblockfull->EraseBlock(*iter);
    }
    return hashesToPrune.size();
}
