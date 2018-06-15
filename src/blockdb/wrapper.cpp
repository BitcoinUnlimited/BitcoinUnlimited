// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wrapper.h"

#include "blockdb.h"
#include "sequential_files.h"
#include "chainparams.h"
#include "dbwrapper.h"
#include "main.h"
#include "undo.h"

extern bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage = "");
extern bool fCheckForPruning;
extern CCriticalSection cs_LastBlockFile;
extern std::set<int> setDirtyFileInfo;
extern std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;

/**
  * Config param to determine what DB type we are using
  */
BlockDBMode BLOCK_DB_MODE = DEFAULT_BLOCK_DB_MODE;

bool DetermineStorageSync()
{
    uint256 bestHashSeq = pcoinsdbview->GetBestBlockSeq();
    LOGA("bestHashSeq = %s \n", bestHashSeq.GetHex().c_str());
    uint256 bestHashLev = pcoinsdbview->GetBestBlockDb();
    LOGA("bestHashLev = %s \n", bestHashLev.GetHex().c_str());
    // if we are using method X and method Y doesnt have any sync progress, assume nothing to sync

    LOGA("check1\n");
    if(bestHashSeq.IsNull() && BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        LOGA("false1\n");
        return false;
    }
    LOGA("check2\n");
    if(bestHashLev.IsNull() && BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        LOGA("false2\n");
        return false;
    }
    CBlockIndex bestIndexSeq;
    LOGA("check3\n");
    pblocktree->FindBlockIndex(bestHashSeq, bestIndexSeq);
    LOGA("check4\n");
    CBlockIndex bestIndexLev;
    pblocktree->FindBlockIndex(bestHashLev, bestIndexLev);

    LOGA("bestIndexSeq info = %i %i %u %u %i %s %u %u %u %u %u \n",
         bestIndexSeq.nHeight,
         bestIndexSeq.nFile,
         bestIndexSeq.nDataPos,
         bestIndexSeq.nUndoPos,
         bestIndexSeq.nVersion,
         bestIndexSeq.hashMerkleRoot.GetHex().c_str(),
         bestIndexSeq.nTime,
         bestIndexSeq.nBits,
         bestIndexSeq.nNonce,
         bestIndexSeq.nStatus,
         bestIndexSeq.nTx
         );

    LOGA("bestIndexLev info = %i %i %u %u %i %s %u %u %u %u %u \n",
         bestIndexLev.nHeight,
         bestIndexLev.nFile,
         bestIndexLev.nDataPos,
         bestIndexLev.nUndoPos,
         bestIndexLev.nVersion,
         bestIndexLev.hashMerkleRoot.GetHex().c_str(),
         bestIndexLev.nTime,
         bestIndexLev.nBits,
         bestIndexLev.nNonce,
         bestIndexLev.nStatus,
         bestIndexLev.nTx
         );

    LOGA("check5\n");
    // if the best height of the storage type we are using is higher than any other type, return false
    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES && bestIndexSeq.nHeight >= bestIndexLev.nHeight)
    {
        LOGA("false3\n");
        return false;
    }
    LOGA("check6\n");
    if(BLOCK_DB_MODE == DB_BLOCK_STORAGE && bestIndexLev.nHeight >= bestIndexSeq.nHeight)
    {
        LOGA("false4\n");
        return false;
    }
    LOGA("check7\n");
    return true;
}

// use this sparingly, this function will be very disk intensive
void SyncStorage(const CChainParams &chainparams)
{
    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        CValidationState state;
        std::vector<std::pair<int, CBlockIndex *> > vSortedByHeight;
        vSortedByHeight.reserve(mapBlockIndex.size());
        int bestHeight = 0;
        CBlockIndex* pindexBest;

        for (const std::pair<uint256, CBlockIndex *> &item : mapBlockIndex)
        {
            CBlockIndex *pindex = item.second;
            vSortedByHeight.push_back(std::make_pair(pindex->nHeight, pindex));
        }

        // Calculate nChainWork
        std::sort(vSortedByHeight.begin(), vSortedByHeight.end());
        for (const std::pair<int, CBlockIndex *> &item : vSortedByHeight)
        {
            CBlockIndex *pindex = item.second;
            // we search for the index in mapblockindex again so we can update nFile, nDataPos, and nUndoPos
            BlockMap::iterator it;
            it = mapBlockIndex.find(pindex->GetBlockHash());
            if(it == mapBlockIndex.end())
            {
                /** should never happen */
                LOGA("something is very wrong somewhere \n");
                assert(false);
            }
            //write the block to the disk if we dont have its data
            if(it->second->nStatus & BLOCK_HAVE_DATA && it->second->GetBlockPos().IsNull())
            {
                BlockDBValue blockValue;
                pblockdb->Read(it->second->GetBlockHash(), blockValue);
                CBlock block_lev = blockValue.block;
                unsigned int nBlockSize = ::GetSerializeSize(block_lev, SER_DISK, CLIENT_VERSION);
                CDiskBlockPos blockPos;
                if (!FindBlockPos(state, blockPos, nBlockSize + 8, blockValue.blockHeight, block_lev.GetBlockTime(), false))
                {
                    LOGA("couldnt find block pos when syncing sequential with info stored in db, asserting false \n");
                    assert(false);
                }
                if (!WriteBlockToDiskSequential(block_lev, blockPos, chainparams.MessageStart()))
                {
                    AbortNode(state, "Failed to sync block from db to sequential files");
                }
                // set this blocks file and data pos
                it->second->nFile = blockPos.nFile;
                it->second->nDataPos = blockPos.nPos;

                // we preform this check inside block data because all undo data requires nfile be set first which is selected when
                // we write block data.
                if(it->second->nStatus & BLOCK_HAVE_UNDO && !it->second->GetBlockPos().IsNull())
                {
                    CBlockUndo blockundo;
                    if(!UndoReadFromDB(blockundo, it->second->GetBlockHash()))
                    {
                        LOGA("failed to read undo data for block with hash %s \n", it->second->GetBlockHash().GetHex().c_str());
                        continue;
                    }
                    CDiskBlockPos pos;
                    if (!FindUndoPos(state, it->second->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                    {
                        LOGA("ConnectBlock(): FindUndoPos failed");
                        assert(false);
                    }
                    uint256 prevHash;
                    if (it->second->pprev) // genesis block prev hash is 0
                    {
                        prevHash = it->second->pprev->GetBlockHash();
                    }
                    else
                    {
                        prevHash.SetNull();
                    }
                    if (!UndoWriteToDisk(blockundo, pos, prevHash, chainparams.MessageStart()))
                    {
                        LOGA("Failed to write undo data");
                        assert(false);
                    }
                    // update nUndoPos in block index
                    it->second->nUndoPos = pos.nPos;
                }
            }
            if(!it->second->GetBlockPos().IsNull() && !it->second->GetUndoPos().IsNull())
            {
                if(it->second->nHeight > bestHeight)
                {
                    bestHeight = it->second->nHeight;
                    // set pindex to the better height so we start from there when syncing
                    pindexBest = it->second;
                }
            }
        }
        // if bestHeight != 0 then pindexBest has been initialized, otherwise no promises
        if(bestHeight != 0)
        {
            pcoinsdbview->WriteBestBlockSeq(pindexBest->GetBlockHash());
        }
    }
    if(BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        // multiple testing trials with adjustments have proven that using bestblock from the coinsviewdb is very unreliable/unstable due to the way
        // PV cleans up its block validation thread when given the shutdown signal.
        // we just iterate through the entire mapblockindex instead.
        BlockMap::iterator iter = mapBlockIndex.begin();
        int64_t bestHeight = 0;
        CBlockIndex* pindexBest;
        for(iter = mapBlockIndex.begin(); iter != mapBlockIndex.end(); ++iter)
        {
            if(iter->second->nStatus & BLOCK_HAVE_DATA)
            {
                CBlock block_seq;
                if(!ReadBlockFromDiskSequential(block_seq, iter->second->GetBlockPos(), chainparams.GetConsensus()))
                {
                    LOGA("SyncStorage(): critical error, failure to read block data from sequential files \n");
                    assert(false);
                }
                if(!WriteBlockToDB(block_seq))
                {
                    LOGA("critical error, failed to write block to db, asserting false \n");
                    assert(false);
                }
            }
            if(iter->second->nStatus & BLOCK_HAVE_UNDO)
            {
                CBlockUndo blockundo;
                // get the undo data from the sequential undo file
                CDiskBlockPos pos = iter->second->GetUndoPos();
                if (pos.IsNull())
                {
                    LOGA("SyncStorage(): critical error, no undo data available for hash %s \n", iter->second->GetBlockHash().GetHex().c_str());
                    assert(false);
                }
                if (!UndoReadFromDiskSequential(blockundo, pos, iter->second->pprev->GetBlockHash()))
                {
                    LOGA("SyncStorage(): critical error, failure to read undo data from sequential files \n");
                    assert(false);
                }
                if(!UndoWriteToDB(blockundo, iter->second->pprev->GetBlockHash()))
                {
                    LOGA("critical error, failed to write undo to db, asserting false \n");
                    assert(false);
                }
            }
            if(iter->second->nStatus & BLOCK_HAVE_UNDO && iter->second->nStatus & BLOCK_HAVE_DATA)
            {
                if(iter->second->nHeight > bestHeight)
                {
                    bestHeight = iter->second->nHeight;
                    // set pindex to the better height so we start from there when syncing
                    pindexBest = iter->second;
                }
            }
        }
        // if bestHeight != 0 then pindexBest has been initialized, otherwise no promises
        if(bestHeight != 0)
        {
            pcoinsdbview->WriteBestBlockDb(pindexBest->GetBlockHash());
        }
        LOGA("we have synced all missing blocks \n");
    }
}

bool WriteBlockToDisk(const CBlock &block, CDiskBlockPos &pos, const CMessageHeader::MessageStartChars &messageStart)
{
    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
    	return WriteBlockToDiskSequential(block, pos, messageStart);
    }
    else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        // we want to set nFile inside pos here to -1 so we know its in levelDB block storage, dont do this within dual most since it also uses sequential

        return WriteBlockToDB(block);
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
        return true;
    }
    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        BlockDBValue value;
        block.SetNull();
        if(!ReadBlockFromDB(pindex, value))
        {
            LOGA("failed to read block with hash %s from leveldb \n", pindex->GetBlockHash().GetHex().c_str());
            return false;
        }
        block = value.block;
        if(block.GetHash() != pindex->GetBlockHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s", pindex->ToString(), pindex->GetBlockPos().ToString());
        }
        return true;
    }
    return false;
}

bool UndoWriteToDisk(const CBlockUndo &blockundo, CDiskBlockPos &pos, const uint256 &hashBlock, const CMessageHeader::MessageStartChars &messageStart)
{
    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        return UndoWriteToDiskSequenatial(blockundo, pos, hashBlock, messageStart);
    }
    else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        return UndoWriteToDB(blockundo, hashBlock);
    }
    // default return of false
    return false;
}

bool UndoReadFromDisk(CBlockUndo &blockundo, const CDiskBlockPos &pos, const uint256 &hashBlock)
{
    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        return UndoReadFromDiskSequential(blockundo, pos, hashBlock);
    }
    else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        return UndoReadFromDB(blockundo, hashBlock);
    }
    // default return of false
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

    if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
    	FindFilesToPruneSequential(setFilesToPrune, nLastBlockWeCanPrune);
    }
    else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE)
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
                if(BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
                {
                    if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks))
                    {
                        return AbortNode(state, "Files to write to block index database");
                    }
                }
                else if(BLOCK_DB_MODE == DB_BLOCK_STORAGE)
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
