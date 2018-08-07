// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wrapper.h"

#include "blockdb.h"
#include "chainparams.h"
#include "dbwrapper.h"
#include "fs.h"
#include "main.h"
#include "sequential_files.h"
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
    uint256 bestHashLev = pcoinsdbview->GetBestBlockDb();

    // if we are using method X and method Y doesnt have any sync progress, assume nothing to sync
    if (bestHashSeq.IsNull() && BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        return false;
    }
    if (bestHashLev.IsNull() && BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        return false;
    }

    CDiskBlockIndex bestIndexSeq;
    CDiskBlockIndex bestIndexLev;
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        pblocktree->FindBlockIndex(bestHashSeq, &bestIndexSeq);
        pblocktreeother->FindBlockIndex(bestHashLev, &bestIndexLev);
    }
    else
    {
        pblocktreeother->FindBlockIndex(bestHashSeq, &bestIndexSeq);
        pblocktree->FindBlockIndex(bestHashLev, &bestIndexLev);
    }

    // if the best height of the storage type we are using is higher than any other type, return false
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES && bestIndexSeq.nHeight >= bestIndexLev.nHeight)
    {
        return false;
    }
    if (BLOCK_DB_MODE == DB_BLOCK_STORAGE && bestIndexLev.nHeight >= bestIndexSeq.nHeight)
    {
        return false;
    }
    return true;
}

void SyncStorage(const CChainParams &chainparams)
{
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        std::vector<std::pair<int, CDiskBlockIndex> > hashesByHeight;
        pblocktreeother->GetSortedHashIndex(hashesByHeight);
        CValidationState state;
        int bestHeight = 0;
        CBlockIndex *pindexBest = new CBlockIndex();
        std::vector<CBlockIndex*> blocksToRemove;
        for (const std::pair<int, CDiskBlockIndex> &item : hashesByHeight)
        {
            CBlockIndex *index;
            if (item.second.GetBlockHash() == chainparams.GetConsensus().hashGenesisBlock)
            {
                CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
                // Start new block file
                unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
                CDiskBlockPos blockPos;
                CValidationState state;
                if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime(), false))
                {
                    LOGA("SyncStorage(): FindBlockPos failed");
                    assert(false);
                }
                if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                {
                    LOGA("SyncStorage(): writing genesis block to disk failed");
                    assert(false);
                }
                CBlockIndex *pindex = AddToBlockIndex(block);
                if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                {
                    LOGA("SyncStorage(): genesis block not accepted");
                    assert(false);
                }
                continue;
            }
            BlockMap::iterator it;
            it = mapBlockIndex.find(item.second.GetBlockHash());
            if (it == mapBlockIndex.end())
            {
                CBlockIndex *pindexNew = InsertBlockIndex(item.second.GetBlockHash());
                pindexNew->pprev = InsertBlockIndex(item.second.hashPrev);
                pindexNew->nHeight = item.second.nHeight;
                pindexNew->nFile = 0;
                pindexNew->nDataPos = 0;
                pindexNew->nUndoPos = 0;
                pindexNew->nVersion = item.second.nVersion;
                pindexNew->hashMerkleRoot = item.second.hashMerkleRoot;
                pindexNew->nTime = item.second.nTime;
                pindexNew->nBits = item.second.nBits;
                pindexNew->nNonce = item.second.nNonce;
                pindexNew->nStatus = item.second.nStatus;
                pindexNew->nTx = item.second.nTx;
                index = pindexNew;
            }
            else
            {
                index = it->second;
            }

            // Update the block data
            if (index->nStatus & BLOCK_HAVE_DATA && item.second.nDataPos != 0)
            {
                CBlock block_lev;
                std::ostringstream key;
                key << index->GetBlockTime() << ":" << index->GetBlockHash().ToString();
                if (pblockdb->Read(key.str(), block_lev))
                {
                    unsigned int nBlockSize = ::GetSerializeSize(block_lev, SER_DISK, CLIENT_VERSION);
                    CDiskBlockPos blockPos;
                    if (!FindBlockPos(state, blockPos, nBlockSize + 8, index->nHeight, block_lev.GetBlockTime(), false))
                    {
                        LOGA("SyncStorage(): couldnt find block pos when syncing sequential with info stored in db, "
                             "asserting false \n");
                        assert(false);
                    }
                    if (!WriteBlockToDiskSequential(block_lev, blockPos, chainparams.MessageStart()))
                    {
                        LOGA("Failed to write block read from db in a sequential files");
                        assert(false);
                    }
                    // set this blocks file and data pos
                    index->nFile = blockPos.nFile;
                    index->nDataPos = blockPos.nPos;
                }
                else
                {
                    index->nStatus &= ~BLOCK_HAVE_DATA;
                }
            }
            else
            {
                index->nStatus &= ~BLOCK_HAVE_DATA;
            }

            // Update the undo data
            if (index->nStatus & BLOCK_HAVE_UNDO && item.second.nUndoPos != 0)
            {
                CBlockUndo blockundo;
                if (UndoReadFromDB(blockundo, index->pprev))
                {
                    CDiskBlockPos pos;
                    if (!FindUndoPos(
                            state, index->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                    {
                        LOGA("SyncStorage(): FindUndoPos failed");
                        assert(false);
                    }
                    if (!UndoWriteToDisk(blockundo, pos, index->pprev, chainparams.MessageStart()))
                    {
                        LOGA("SyncStorage(): Failed to write undo data");
                        assert(false);
                    }

                    // update nUndoPos in block index
                    index->nUndoPos = pos.nPos;
                }
                else
                {
                    index->nStatus &= ~BLOCK_HAVE_UNDO;
                }
            }
            else
            {
                index->nStatus &= ~BLOCK_HAVE_UNDO;
            }

            if (!index->GetBlockPos().IsNull() && !index->GetUndoPos().IsNull())
            {
                if (index->nHeight > bestHeight)
                {
                    bestHeight = index->nHeight;
                    pindexBest = index;
                }
            }
            setDirtyBlockIndex.insert(index);
            blocksToRemove.push_back(index);
            if(blocksToRemove.size() % 10000 == 0)
            {
                CDBBatch batch(*pblockdb);
                for(auto removeIndex : blocksToRemove)
                {
                  std::ostringstream key;
                  key << removeIndex->GetBlockTime() << ":" << removeIndex->GetBlockHash().ToString();
                  batch.Erase(key.str());
                }
                pblockdb->WriteBatch(batch);
                // you must use NULL here, not nullptr
                CBlockIndex* indexfront = blocksToRemove.front();
                std::ostringstream frontkey;
                frontkey << indexfront->GetBlockTime() << ":" << indexfront->GetBlockHash().ToString();
                CBlockIndex* indexback = blocksToRemove.back();
                std::ostringstream backkey;
                backkey << indexback->GetBlockTime() << ":" << indexback->GetBlockHash().ToString();
                pblockdb->CompactRange(frontkey.str(),backkey.str());
                blocksToRemove.clear();
            }
        }

        // if bestHeight != 0 then pindexBest has been initialized and we can update the best block.
        if (bestHeight != 0)
        {
            pcoinsdbview->WriteBestBlockSeq(pindexBest->GetBlockHash());
        }
    }

    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        std::vector<std::pair<int, CDiskBlockIndex> > indexByHeight;
        pblocktreeother->GetSortedHashIndex(indexByHeight);
        LOGA("indexByHeight size = %u \n", indexByHeight.size());
        int64_t bestHeight = 0;
        uint64_t lastFinishedFile = 0;
        CBlockIndex *pindexBest = new CBlockIndex();
        // Load block file info
        int loadedblockfile = 0;
        pblocktreeother->ReadLastBlockFile(loadedblockfile);
        LOGA("loadedblockfile = %i \n", loadedblockfile);
        std::vector<CBlockFileInfo> blockfiles;
        blockfiles.resize(loadedblockfile + 1);
        LOGA("blockfiles.size() = %u \n", blockfiles.size());
        for (int nFile = 0; nFile <= loadedblockfile; nFile++)
        {
            pblocktreeother->ReadBlockFileInfo(nFile, blockfiles[nFile]);
        }

        for (const std::pair<int, CDiskBlockIndex> &item : indexByHeight)
        {
            bool needData = true;
            bool needUndo = true;
            CBlockIndex *index;
            if (item.second.GetBlockHash() == chainparams.GetConsensus().hashGenesisBlock)
            {
                CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
                // Start new block file
                unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
                CDiskBlockPos blockPos;
                CValidationState state;
                if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime(), false))
                {
                    LOGA("SyncStorage(): FindBlockPos failed");
                    assert(false);
                }
                if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                {
                    LOGA("SyncStorage(): writing genesis block to disk failed");
                    assert(false);
                }
                CBlockIndex *pindex = AddToBlockIndex(block);
                if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                {
                    LOGA("SyncStorage(): genesis block not accepted");
                    assert(false);
                }
                continue;
            }
            BlockMap::iterator iter;
            iter = mapBlockIndex.find(item.second.GetBlockHash());
            if (iter == mapBlockIndex.end())
            {
                CBlockIndex *pindexNew = InsertBlockIndex(item.second.GetBlockHash());
                pindexNew->pprev = InsertBlockIndex(item.second.hashPrev);
                pindexNew->nHeight = item.second.nHeight;
                // for blockdb nFile, nDataPos, and nUndoPos are switches, 0 is dont have. !0 is have. actual value
                // irrelevant
                pindexNew->nFile = item.second.nFile;
                pindexNew->nDataPos = item.second.nDataPos;
                pindexNew->nUndoPos = item.second.nUndoPos;
                pindexNew->nVersion = item.second.nVersion;
                pindexNew->hashMerkleRoot = item.second.hashMerkleRoot;
                pindexNew->nTime = item.second.nTime;
                pindexNew->nBits = item.second.nBits;
                pindexNew->nNonce = item.second.nNonce;
                pindexNew->nStatus = item.second.nStatus;
                pindexNew->nTx = item.second.nTx;
                index = pindexNew;
            }
            else
            {
                index = iter->second;
            }

            // Update the block data
            if (needData && index->nStatus & BLOCK_HAVE_DATA && !index->GetBlockPos().IsNull())
            {
                CBlock block_seq;
                if (!ReadBlockFromDiskSequential(block_seq, index->GetBlockPos(), chainparams.GetConsensus()))
                {
                    LOGA("SyncStorage(): critical error, failure to read block data from sequential files \n");
                    assert(false);
                }
                if (!WriteBlockToDB(block_seq))
                {
                    LOGA("critical error, failed to write block to db, asserting false \n");
                    assert(false);
                }
            }

            // Update the undo data
            if (needUndo && index->nStatus & BLOCK_HAVE_UNDO && !index->GetUndoPos().IsNull())
            {
                CBlockUndo blockundo;

                // get the undo data from the sequential undo file
                CDiskBlockPos pos = index->GetUndoPos();
                if (pos.IsNull())
                {
                    LOGA("SyncStorage(): critical error, no undo data available for hash %s \n",
                        index->GetBlockHash().GetHex().c_str());
                    assert(false);
                }
                if (!UndoReadFromDiskSequential(blockundo, pos, index->pprev->GetBlockHash()))
                {
                    LOGA("SyncStorage(): critical error, failure to read undo data from sequential files \n");
                    assert(false);
                }
                if (!UndoWriteToDB(blockundo, index->pprev))
                {
                    LOGA("critical error, failed to write undo to db, asserting false \n");
                    assert(false);
                }
            }

            if (!index->GetUndoPos().IsNull() && !index->GetBlockPos().IsNull())
            {
                if (index->nHeight > bestHeight)
                {
                    bestHeight = index->nHeight;
                    // set pindex to the better height so we start from there when syncing
                    pindexBest = index;
                }
            }
            setDirtyBlockIndex.insert(index);
            if(lastFinishedFile <= loadedblockfile && index->nHeight > blockfiles[lastFinishedFile].nHeightLast)
            {
                fs::remove(GetDataDir() / "blocks" / strprintf("blk%05u.dat", lastFinishedFile));
                fs::remove(GetDataDir() / "blocks" / strprintf("rev%05u.dat", lastFinishedFile));
                lastFinishedFile++;
            }
        }

        // if bestHeight != 0 then pindexBest has been initialized and we can update the best block.
        if (bestHeight != 0)
        {
            pcoinsdbview->WriteBestBlockDb(pindexBest->GetBlockHash());
        }
    }
    FlushStateToDisk();
    LOGA("Block database upgrade completed.\n");
}

bool WriteBlockToDisk(const CBlock &block, CDiskBlockPos &pos, const CMessageHeader::MessageStartChars &messageStart)
{
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        return WriteBlockToDiskSequential(block, pos, messageStart);
    }
    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        return WriteBlockToDB(block);
    }
    return false;
}

bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex, const Consensus::Params &consensusParams)
{
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        if (!ReadBlockFromDiskSequential(block, pindex->GetBlockPos(), consensusParams))
        {
            return false;
        }
        if (block.GetHash() != pindex->GetBlockHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
        }
        return true;
    }
    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        block.SetNull();
        if (!ReadBlockFromDB(pindex, block))
        {
            LOGA("failed to read block with hash %s from leveldb \n", pindex->GetBlockHash().GetHex().c_str());
            return false;
        }
        if (block.GetHash() != pindex->GetBlockHash())
        {
            return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
        }
        return true;
    }
    return false;
}

bool UndoWriteToDisk(const CBlockUndo &blockundo,
    CDiskBlockPos &pos,
    const CBlockIndex *pindex,
    const CMessageHeader::MessageStartChars &messageStart)
{
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        uint256 hashBlock;
        if (pindex)
        {
            hashBlock = pindex->GetBlockHash();
        }
        else
        {
            hashBlock.SetNull();
        }
        return UndoWriteToDiskSequenatial(blockundo, pos, hashBlock, messageStart);
    }
    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        return UndoWriteToDB(blockundo, pindex);
    }
    return false;
}

bool UndoReadFromDisk(CBlockUndo &blockundo, const CDiskBlockPos &pos, const CBlockIndex *pindex)
{
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        return UndoReadFromDiskSequential(blockundo, pos, pindex->GetBlockHash());
    }
    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        return UndoReadFromDB(blockundo, pindex);
    }
    return false;
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

    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        FindFilesToPruneSequential(setFilesToPrune, nLastBlockWeCanPrune);
    }
    else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        uint64_t amntPruned = FindFilesToPruneLevelDB(nLastBlockWeCanPrune);
        // because we just prune the DB here and dont have a file set to return, we need to set prune triggers here
        // otherwise they will check for the fileset and incorrectly never be set

        // we do not need to set fFlushForPrune since we have "already flushed"

        fCheckForPruning = false;
        // if this is the first time we attempt to prune, dont set pruned = true if we didnt prune anything so we must
        // check amntPruned here
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

        // If possible adjust the max size of the coin cache (nCoinCacheMaxSize) based on current available memory. Do
        // this before determinining whether to flush the cache or not in the steps that follow.
        AdjustCoinCacheSize();

        size_t cacheSize = pcoinsTip->DynamicMemoryUsage();
        static int64_t nSizeAfterLastFlush = 0;
        // The cache is close to the limit. Try to flush and trim.
        bool fCacheCritical = ((mode == FLUSH_STATE_IF_NEEDED) && (cacheSize > nCoinCacheMaxSize * 0.995)) ||
                              (cacheSize - nSizeAfterLastFlush > nMaxCacheIncreaseSinceLastFlush);
        // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload
        // after a crash.
        bool fPeriodicWrite =
            mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
        // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
        bool fPeriodicFlush =
            mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
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
            if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
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
                if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
                {
                    if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks))
                    {
                        return AbortNode(state, "Files to write to block index database");
                    }
                }
                else if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
                {
                    // vFiles should be empty for a LEVELDB call so insert a blank vector instead
                    std::vector<std::pair<int, const CBlockFileInfo *> > vFilesEmpty;
                    if (!pblocktree->WriteBatchSync(vFilesEmpty, 0, vBlocks))
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
            // Finally remove any pruned files, this will be empty for blockdb mode
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
                pcoinsTip->Trim(nCoinCacheMaxSize);
            }
            else
            {
                // Trim, but never trim more than nMaxCacheIncreaseSinceLastFlush
                size_t nTrimSize = nCoinCacheMaxSize * .90;
                if (nCoinCacheMaxSize - nMaxCacheIncreaseSinceLastFlush > nTrimSize)
                {
                    nTrimSize = nCoinCacheMaxSize - nMaxCacheIncreaseSinceLastFlush;
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
