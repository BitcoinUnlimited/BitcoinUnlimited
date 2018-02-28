// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockdb_wrapper.h"

#include "blockdb_leveldb.h"
#include "blockdb_sequential.h"
#include "chainparams.h"
#include "dbwrapper.h"
#include "main.h"

extern bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage = "");
extern bool fCheckForPruning;
extern CCriticalSection cs_LastBlockFile;
extern std::set<int> setDirtyFileInfo;
extern std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;

/**
  * Config param to determine what DB type we are using
  */
BlockDBMode BLOCK_DB_MODE = DEFAULT_BLOCK_DB_MODE;


bool WriteBlockToDisk(const CBlock &block, CDiskBlockPos &pos, const CMessageHeader::MessageStartChars &messageStart)
{
    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
    	return WriteBlockToDiskSequential(block, pos, messageStart);
    }
    else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        // we want to set nFile inside pos here to -1 so we know its in levelDB block storage, dont do this within dual most since it also uses sequential

    	return WriteBlockToDiskLevelDB(block);
    }
    else if(BLOCK_DB_MODE == HYBRID_STORAGE)
    {
    	bool seq = WriteBlockToDiskSequential(block, pos, messageStart);
    	bool lev = WriteBlockToDiskLevelDB(block);
    	return (seq & lev);
    }

    // default return of false
    return false;
}

bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex, const Consensus::Params &consensusParams)
{
    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        if (!ReadBlockFromDiskSequential(block, pindex->GetBlockPos(), consensusParams))
        {
            return false;
        }
        if (block.GetHash() != pindex->GetBlockHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s", pindex->ToString(), pindex->GetBlockPos().ToString());
        }
    }
    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        BlockDBValue value;
        block.SetNull();
        if(!ReadBlockFromDiskLevelDB(pindex, value))
        {
            return false;
        }
        block = value.block;
        if(block.GetHash() != pindex->GetBlockHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s", pindex->ToString(), pindex->GetBlockPos().ToString());
        }
    }
    else if (BLOCK_DB_MODE == HYBRID_STORAGE)
    {
    	CBlock blockSeq;
    	CBlock blockLev;
    	BlockDBValue value;
        /// run both to verify both databases match, we will only return
        if (!ReadBlockFromDiskSequential(blockSeq, pindex->GetBlockPos(), consensusParams))
        {
            return false;
        }
        blockLev.SetNull();
        if(!ReadBlockFromDiskLevelDB(pindex, value))
        {
            return false;
        }
        blockLev = value.block;
        if(blockSeq.GetHash() != pindex->GetBlockHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s", pindex->ToString(), pindex->GetBlockPos().ToString());
        }
        if(blockLev.GetHash() != pindex->GetBlockHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s", pindex->ToString(), pindex->GetBlockPos().ToString());
        }
        if(blockSeq.GetHash() != blockLev.GetHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match for both database types. THERE IS A CRITICAL ERROR SOMEWHERE \n");
        }
        block = blockLev;
    }
    return true;
}


/* Calculate the block/rev files that should be deleted to remain under target*/
void FindFilesToPrune(std::set<int> &setFilesToPrune, uint64_t nPruneAfterHeight)
{
    LOCK2(cs_main, cs_LastBlockFile);

    if (chainActive.Tip() == NULL || nPruneTarget == 0)
    {
        return;
    }
    if ((uint64_t)chainActive.Tip()->nHeight <= nPruneAfterHeight)
    {
        return;
    }
    uint64_t nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;

    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES || BLOCK_DB_MODE == HYBRID_STORAGE)
    {
    	FindFilesToPruneSequential(setFilesToPrune, nLastBlockWeCanPrune);
    }
    else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE || BLOCK_DB_MODE == HYBRID_STORAGE)
    {
    	uint64_t amntPruned = FindFilesToPruneLevelDB(nLastBlockWeCanPrune);
        // because we just prune the DB here and dont have a file set to return, we need to set prune triggers here
        // otherwise they will check for the fileset and incorrectly never be set

        // we do not need to set fFlushForPrune since we have "already flushed"

        fCheckForPruning = false;
        // if this is the first time we attempt to prune, dont set pruned = true if we didnt prune anything so we must check amntPruned here
        if (!fHavePruned && amntPruned != 0)
        {
            pblocktree->WriteFlag("prunedblockfiles", true);
            fHavePruned = true;
        }

    }
}


/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
bool FlushStateToDisk(CValidationState &state, FlushStateMode mode)
{
    const CChainParams &chainparams = Params();
    LOCK2(cs_main, cs_LastBlockFile);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    try
    {
        if (fPruneMode && fCheckForPruning && !fReindex)
        {
            FindFilesToPrune(setFilesToPrune, chainparams.PruneAfterHeight());
            fCheckForPruning = false;
            if (!setFilesToPrune.empty())
            {
                fFlushForPrune = true;
                if (!fHavePruned)
                {
                    pblocktree->WriteFlag("prunedblockfiles", true);
                    fHavePruned = true;
                }
            }
        }
        int64_t nNow = GetTimeMicros();
        // Avoid writing/flushing immediately after startup.
        if (nLastWrite == 0)
        {
            nLastWrite = nNow;
        }
        if (nLastFlush == 0)
        {
            nLastFlush = nNow;
        }
        if (nLastSetChain == 0)
        {
            nLastSetChain = nNow;
        }

        // If possible adjust the max size of the coin cache (nCoinCacheUsage) based on current available memory. Do
        // this before determinining whether to flush the cache or not in the steps that follow.
        AdjustCoinCacheSize();

        size_t cacheSize = pcoinsTip->DynamicMemoryUsage();
        static int64_t nSizeAfterLastFlush = 0;
        // The cache is close to the limit. Try to flush and trim.
        bool fCacheCritical = ((mode == FLUSH_STATE_IF_NEEDED) && (cacheSize > nCoinCacheUsage * 0.995)) ||
                              (cacheSize - nSizeAfterLastFlush > nMaxCacheIncreaseSinceLastFlush);
        // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload
        // after a crash.
        bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
        // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
        bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
        // Combine all conditions that result in a full cache flush.
        bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheCritical || fPeriodicFlush || fFlushForPrune;
        // Write blocks and block index to disk.
        if (fDoFullFlush || fPeriodicWrite)
        {
            // Depend on nMinDiskSpace to ensure we can write block index
            if (!CheckDiskSpace(0))
            {
                return state.Error("out of disk space");
            }
            // First make sure all block and undo data is flushed to disk. This is not used for levelDB block storage
            if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
            {
                FlushBlockFile();
            }
            // Then update all block file information (which may refer to block and undo files).
            {
                std::vector<std::pair<int, const CBlockFileInfo *> > vFiles;
                vFiles.reserve(setDirtyFileInfo.size());
                for (std::set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end();)
                {
                    vFiles.push_back(std::make_pair(*it, &vinfoBlockFile[*it]));
                    setDirtyFileInfo.erase(it++);
                }
                std::vector<const CBlockIndex *> vBlocks;
                vBlocks.reserve(setDirtyBlockIndex.size());
                for (std::set<CBlockIndex *>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end();)
                {
                    vBlocks.push_back(*it);
                    setDirtyBlockIndex.erase(it++);
                }


                // we write different info depending on block storage system
                if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES || BLOCK_DB_MODE == HYBRID_STORAGE)
                {
                    if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks))
                    {
                        return AbortNode(state, "Files to write to block index database");
                    }
                }
                else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE || BLOCK_DB_MODE == HYBRID_STORAGE)
                {
                    // vFiles should be empty for a LEVELDB call so insert a blank vector instead
                    std::vector<std::pair<int, const CBlockFileInfo *> > vFilesEmpty;
                    // pass in -1 for the last block file since we dont use it for level, it will be ignored in the function if it is -1337
                    if (!pblocktree->WriteBatchSync(vFilesEmpty, -1337, vBlocks))
                    {
                        return AbortNode(state, "Files to write to block index database");
                    }
                }
                else
                {
                    // THIS SHOULD NEVER HAPPEN, it means we werent running in any recognizable block DB mode
                    assert(false);
                }


            }
            // Finally remove any pruned files
            if (fFlushForPrune)
            {
                UnlinkPrunedFiles(setFilesToPrune);
            }
            nLastWrite = nNow;
        }
        // Flush best chain related state. This can only be done if the blocks / block index write was also done.
        if (fDoFullFlush)
        {
            // Typical Coin structures on disk are around 48 bytes in size.
            // Pushing a new one to the database can cause it to be written
            // twice (once in the log, and once in the tables). This is already
            // an overestimation, as most will delete an existing entry or
            // overwrite one. Still, use a conservative safety factor of 2.
            if (!CheckDiskSpace(48 * 2 * 2 * pcoinsTip->GetCacheSize()))
            {
                return state.Error("out of disk space");
            }
            // Flush the chainstate (which may refer to block index entries).
            if (!pcoinsTip->Flush())
            {
                return AbortNode(state, "Failed to write to coin database");
            }
            nLastFlush = nNow;
            // Trim any excess entries from the cache if needed.  If chain is not syncd then
            // trim extra so that we don't flush as often during IBD.
            if (IsChainNearlySyncd() && !fReindex && !fImporting)
            {
                pcoinsTip->Trim(nCoinCacheUsage);
            }
            else
            {
                // Trim, but never trim more than nMaxCacheIncreaseSinceLastFlush
                size_t nTrimSize = nCoinCacheUsage * .90;
                if (nCoinCacheUsage - nMaxCacheIncreaseSinceLastFlush > nTrimSize)
                {
                    nTrimSize = nCoinCacheUsage - nMaxCacheIncreaseSinceLastFlush;
                }
                pcoinsTip->Trim(nTrimSize);
            }
            nSizeAfterLastFlush = pcoinsTip->DynamicMemoryUsage();
        }
        if (fDoFullFlush || ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) &&
                                nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000))
        {
            // Update best block in wallet (so we can detect restored wallets).
            GetMainSignals().SetBestChain(chainActive.GetLocator());
            nLastSetChain = nNow;
        }

        // As a safeguard, periodically check and correct any drift in the value of cachedCoinsUsage.  While a
        // correction should never be needed, resetting the value allows the node to continue operating, and only
        // an error is reported if the new and old values do not match.
        if (fPeriodicFlush)
        {
            pcoinsTip->ResetCachedCoinUsage();
        }
    }
    catch (const std::runtime_error &e)
    {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk()
{
    CValidationState state;
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush()
{
    CValidationState state;
    fCheckForPruning = true;
    FlushStateToDisk(state, FLUSH_STATE_NONE);
}
