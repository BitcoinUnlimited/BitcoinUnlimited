// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockstorage/blockstorage.h"

#include "blockleveldb.h"
#include "chainparams.h"
#include "dbwrapper.h"
#include "fs.h"
#include "main.h"
#include "sequential_files.h"
#include "ui_interface.h"
#include "undo.h"
#include "validation/validation.h"

extern bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage = "");
extern bool fCheckForPruning;
extern CCriticalSection cs_LastBlockFile;
extern std::set<int> setDirtyFileInfo;
extern std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;
extern CTweak<uint64_t> pruneIntervalTweak;

CDatabaseAbstract *pblockdb = nullptr;

/**
  * Config param to determine what DB type we are using
  */
BlockDBMode BLOCK_DB_MODE = DEFAULT_BLOCK_DB_MODE;

void InitializeBlockStorage(const int64_t &_nBlockTreeDBCache,
    const int64_t &_nBlockDBCache,
    const int64_t &_nBlockUndoDBCache)
{
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES) // BLOCK_DB_MODE 0
    {
        pblocktree = new CBlockTreeDB(_nBlockTreeDBCache, "blocks", false, fReindex);
        delete pblockdb;
        pblockdb = nullptr;
    }
    else if (BLOCK_DB_MODE == LEVELDB_BLOCK_STORAGE) // BLOCK_DB_MODE 1
    {
        pblocktree = new CBlockTreeDB(_nBlockTreeDBCache, "blockdb", false, fReindex);
        if (boost::filesystem::exists(GetDataDir() / "blockdb" / "blocks"))
        {
            for (fs::recursive_directory_iterator it(GetDataDir() / "blockdb" / "blocks");
                 it != fs::recursive_directory_iterator(); ++it)
            {
                if (!fs::is_directory(*it))
                {
                    nDBUsedSpace += fs::file_size(*it);
                }
            }
        }
        pblockdb = new CBlockLevelDB(_nBlockDBCache, _nBlockUndoDBCache, false, false, false);
    }
}

// grab the block tree for mode and put it at pblocktreeother
void GetBlockTreeOther(BlockDBMode mode)
{
    // hardcode 2MiB here, it is a negligable amount and is only used temporarily
    int64_t _nBlockTreeDBCache = (1 << 21);
    if (mode == SEQUENTIAL_BLOCK_FILES)
    {
        pblocktreeother = new CBlockTreeDB(_nBlockTreeDBCache, "blocks", false, fReindex);
    }
    else if (mode == LEVELDB_BLOCK_STORAGE)
    {
        pblocktreeother = new CBlockTreeDB(_nBlockTreeDBCache, "blockdb", false, fReindex);
    }
}

void GetTempBlockDB(CDatabaseAbstract *&_pblockdbsync, BlockDBMode &_otherMode)
{
    _pblockdbsync = nullptr;
    if (_otherMode == SEQUENTIAL_BLOCK_FILES)
    {
        return;
    }
    else if (_otherMode == LEVELDB_BLOCK_STORAGE)
    {
        int64_t _nBlockDBCache = 64 << 20;
        int64_t _nBlockUndoDBCache = 64 << 20;
        _pblockdbsync = new CBlockLevelDB(_nBlockDBCache, _nBlockUndoDBCache, false, false, false);
    }
}

bool DetermineStorageSync(BlockDBMode &_otherMode)
{
    uint256 bestHashMode = pcoinsdbview->GetBestBlock();
    uint256 bestHashOther = uint256();
    int32_t curmodenum = static_cast<int32_t>(BLOCK_DB_MODE);
    int32_t maxoptions = static_cast<int32_t>(END_STORAGE_OPTIONS);
    for (int32_t i = 0; i < maxoptions; i++)
    {
        if (i != curmodenum)
        {
            BlockDBMode checkmode = static_cast<BlockDBMode>(i);
            uint256 modehash = pcoinsdbview->GetBestBlock(checkmode);
            if (!modehash.IsNull())
            {
                bestHashOther = modehash;
                _otherMode = checkmode;
                // we break because we can only have, at most, 1 other mode
                // with a value (the last mode we used) so there is no need to keep checking
                break;
            }
        }
    }

    // if we are missing a best hash for every other node we
    // have nothing to sync against so return false
    if (bestHashOther.IsNull())
    {
        return false;
    }
    GetBlockTreeOther(_otherMode);
    CDiskBlockIndex bestIndexMode;
    CDiskBlockIndex bestIndexOther;
    pblocktree->FindBlockIndex(bestHashMode, &bestIndexMode);
    pblocktreeother->FindBlockIndex(bestHashOther, &bestIndexOther);

    // if the best height of the storage type we are using is higher than any other type, return false
    if (bestIndexMode.nHeight >= bestIndexOther.nHeight)
    {
        return false;
    }
    return true;
}

void SyncStorage(const CChainParams &chainparams)
{
    CDatabaseAbstract *pblockdbsync = nullptr;
    BlockDBMode otherMode = END_STORAGE_OPTIONS;
    if (!DetermineStorageSync(otherMode))
    {
        return;
    }

    LOGA("Upgrading block database...\n");
    uiInterface.InitMessage(_("Upgrading block database...This could take a while."));

    GetTempBlockDB(pblockdbsync, otherMode);
    AssertLockHeld(cs_main);
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        std::vector<std::pair<int, CDiskBlockIndex> > hashesByHeight;
        pblocktreeother->GetSortedHashIndex(hashesByHeight);
        CValidationState state;
        int bestHeight = 0;
        CBlockIndex *pindexBest = new CBlockIndex();
        std::vector<CBlockIndex *> blocksToRemove;
        for (const std::pair<int, CDiskBlockIndex> &item : hashesByHeight)
        {
            CBlockIndex *index;
            if (item.second.GetBlockHash() == chainparams.GetConsensus().hashGenesisBlock)
            {
                CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
                // Start new block file
                unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
                CDiskBlockPos blockPos;
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

            index = LookupBlockIndex(item.second.GetBlockHash());
            if (!index)
            {
                // TODO only one thread should create a new pindex at a time.
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

            // Update the block data
            if (index->nStatus & BLOCK_HAVE_DATA && item.second.nDataPos != 0)
            {
                CBlock block_lev;
                if (pblockdbsync == nullptr)
                {
                    LOGA("blockdbsync is a nullptr \n");
                }
                if (pblockdbsync->ReadBlock(index, block_lev))
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
                if (pblockdbsync->ReadUndo(blockundo, index->pprev))
                {
                    CDiskBlockPos pos;
                    if (!FindUndoPos(
                            state, index->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                    {
                        LOGA("SyncStorage(): FindUndoPos failed");
                        assert(false);
                    }
                    if (!WriteUndoToDisk(blockundo, pos, index->pprev, chainparams.MessageStart()))
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
            if (blocksToRemove.size() % 10000 == 0)
            {
                for (CBlockIndex *removeIndex : blocksToRemove)
                {
                    pblockdbsync->EraseBlock(removeIndex);
                }
                // you must use NULL here, not nullptr
                CBlockIndex *indexfront = blocksToRemove.front();
                std::ostringstream frontkey;
                frontkey << indexfront->GetBlockTime() << ":" << indexfront->GetBlockHash().ToString();
                CBlockIndex *indexback = blocksToRemove.back();
                std::ostringstream backkey;
                backkey << indexback->GetBlockTime() << ":" << indexback->GetBlockHash().ToString();
                pblockdbsync->CondenseBlockData(frontkey.str(), backkey.str());
                blocksToRemove.clear();
            }
        }

        // if bestHeight != 0 then pindexBest has been initialized and we can update the best block.
        if (bestHeight != 0)
        {
            pcoinsdbview->WriteBestBlock(pindexBest->GetBlockHash(), SEQUENTIAL_BLOCK_FILES);
        }
    }

    else if (BLOCK_DB_MODE == LEVELDB_BLOCK_STORAGE)
    {
        std::vector<std::pair<int, CDiskBlockIndex> > indexByHeight;
        pblocktreeother->GetSortedHashIndex(indexByHeight);
        LOGA("indexByHeight size = %u \n", indexByHeight.size());
        int64_t bestHeight = 0;
        int64_t lastFinishedFile = 0;
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

            index = LookupBlockIndex(item.second.GetBlockHash());
            if (!index)
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

            // Update the block data
            if (index->nStatus & BLOCK_HAVE_DATA && !index->GetBlockPos().IsNull())
            {
                CBlock block_seq;
                if (!ReadBlockFromDiskSequential(block_seq, index->GetBlockPos(), chainparams.GetConsensus()))
                {
                    LOGA("SyncStorage(): critical error, failure to read block data from sequential files \n");
                    assert(false);
                }
                unsigned int nBlockSize = ::GetSerializeSize(block_seq, SER_DISK, CLIENT_VERSION);
                index->nDataPos = nBlockSize;
                if (!pblockdb->WriteBlock(block_seq))
                {
                    LOGA("critical error, failed to write block to db, asserting false \n");
                    assert(false);
                }
            }

            // Update the undo data
            if (index->nStatus & BLOCK_HAVE_UNDO && !index->GetUndoPos().IsNull())
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
                if (!ReadUndoFromDiskSequential(blockundo, pos, index->pprev->GetBlockHash()))
                {
                    LOGA("SyncStorage(): critical error, failure to read undo data from sequential files \n");
                    assert(false);
                }
                if (!pblockdb->WriteUndo(blockundo, index->pprev))
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
            if (lastFinishedFile <= loadedblockfile && index->nHeight > (int)blockfiles[lastFinishedFile].nHeightLast)
            {
                fs::remove(GetDataDir() / "blocks" / strprintf("blk%05u.dat", lastFinishedFile));
                fs::remove(GetDataDir() / "blocks" / strprintf("rev%05u.dat", lastFinishedFile));
                lastFinishedFile++;
            }
        }

        // if bestHeight != 0 then pindexBest has been initialized and we can update the best block.
        if (bestHeight != 0)
        {
            pcoinsdbview->WriteBestBlock(pindexBest->GetBlockHash(), LEVELDB_BLOCK_STORAGE);
        }
    }
    // make sure whatever node we did a sync from has no best block anymore
    uint256 emptyHash = uint256();
    pcoinsdbview->WriteBestBlock(emptyHash, otherMode);
    FlushStateToDisk();
    LOGA("Block database upgrade completed.\n");
}

bool WriteBlockToDisk(const CBlock &block, CDiskBlockPos &pos, const CMessageHeader::MessageStartChars &messageStart)
{
    if (!pblockdb)
    {
        return WriteBlockToDiskSequential(block, pos, messageStart);
    }
    return pblockdb->WriteBlock(block);
}

bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex, const Consensus::Params &consensusParams)
{
    if (!pblockdb)
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
    block.SetNull();
    if (!pblockdb->ReadBlock(pindex, block))
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

bool WriteUndoToDisk(const CBlockUndo &blockundo,
    CDiskBlockPos &pos,
    const CBlockIndex *pindex,
    const CMessageHeader::MessageStartChars &messageStart)
{
    if (!pblockdb)
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
        return WriteUndoToDiskSequenatial(blockundo, pos, hashBlock, messageStart);
    }
    return pblockdb->WriteUndo(blockundo, pindex);
}

/**
 * ReadUndoFromDisk only uses CDiskBlockPos for sequential files, not for blockdb
 */
bool ReadUndoFromDisk(CBlockUndo &blockundo, const CDiskBlockPos &pos, const CBlockIndex *pindex)
{
    if (!pblockdb)
    {
        return ReadUndoFromDiskSequential(blockundo, pos, pindex->GetBlockHash());
    }
    return pblockdb->ReadUndo(blockundo, pindex);
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

    if (!pblockdb)
    {
        FindFilesToPruneSequential(setFilesToPrune, nLastBlockWeCanPrune);
    }
    else // if (pblockdb)
    {
        if (nDBUsedSpace < nPruneTarget + (pruneIntervalTweak.Value() * 1024 * 1024))
        {
            return;
        }
        uint64_t amntPruned = pblockdb->PruneDB(nLastBlockWeCanPrune);
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

bool FlushStateToDiskInternal(CValidationState &state,
    FlushStateMode mode,
    bool fFlushForPrune,
    std::set<int> setFilesToPrune)
{
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
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
                          (cacheSize - nSizeAfterLastFlush > (int64_t)nMaxCacheIncreaseSinceLastFlush);
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
            if (!pblockdb) // sequential files
            {
                if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks))
                {
                    return AbortNode(state, "Files to write to block index database");
                }
            }
            else // if (pblockdb) //we are using a db, not sequential files
            {
                // vFiles should be empty for a DB call so insert a blank vector instead
                std::vector<std::pair<int, const CBlockFileInfo *> > vFilesEmpty;
                if (!pblocktree->WriteBatchSync(vFilesEmpty, 0, vBlocks))
                {
                    return AbortNode(state, "Files to write to block index database");
                }
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
    return true;
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
        return FlushStateToDiskInternal(state, mode, fFlushForPrune, setFilesToPrune);
    }
    catch (const std::runtime_error &e)
    {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
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

bool FindBlockPos(CValidationState &state,
    CDiskBlockPos &pos,
    unsigned int nAddSize,
    unsigned int nHeight,
    uint64_t nTime,
    bool fKnown)
{
    // nDataPos for blockdb is a flag, just set to 1 to indicate we have that data. nFile is unused.
    if (pblockdb)
    {
        pos.nFile = 1;
        pos.nPos = nAddSize;
        if (CheckDiskSpace(nAddSize))
        {
            nDBUsedSpace += nAddSize;
            if (fPruneMode && nDBUsedSpace >= nPruneTarget)
            {
                fCheckForPruning = true;
            }
        }
        else
        {
            return state.Error("out of disk space");
        }
        return true;
    }

    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile)
    {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown)
    {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE)
        {
            nFile++;
            if (vinfoBlockFile.size() <= nFile)
            {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile)
    {
        if (!fKnown)
        {
            LOGA("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile[nLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown)
    {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks)
        {
            if (fPruneMode)
            {
                fCheckForPruning = true;
            }
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos))
            {
                FILE *file = OpenBlockFile(pos);
                if (file)
                {
                    LOGA("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE,
                        pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    // nUndoPos for blockdb is a flag, set it to 1 to inidicate we have the data
    if (pblockdb)
    {
        pos.nPos = 1;
        if (!CheckDiskSpace(nAddSize))
        {
            return state.Error("out of disk space");
        }
        return true;
    }

    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks)
    {
        if (fPruneMode)
        {
            fCheckForPruning = true;
        }
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos))
        {
            FILE *file = OpenUndoFile(pos);
            if (file)
            {
                LOGA(
                    "Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
        {
            return state.Error("out of disk space");
        }
    }

    return true;
}
