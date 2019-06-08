// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "prune.h"

#include "blockstorage.h"
#include "sequential_files.h"
#include "txdb.h"
#include "ui_interface.h"
#include "util.h"
#include "utiltime.h"
#include "xversionkeys.h"

#include <random>

extern CBlockTreeDB *pblocktree;
extern CDatabaseAbstract *pblockdb;
extern CChain chainActive;
extern CTweak<uint64_t> pruneIntervalTweak;
extern CCriticalSection cs_LastBlockFile;
extern std::set<int> setDirtyFileInfo;
extern std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;

bool fPruneWithMask = DEFAULT_PRUNE_WITH_MASK;
bool fHavePruned = false;
uint64_t nPruneTarget = 0;
uint64_t nDBUsedSpace = 0;
arith_uint256 pruneHashMask = 0;
const arith_uint256 LSB64_MASK = std::numeric_limits<uint64_t>::max();
const uint64_t ONE_THRESHOLD_PERCENT = std::numeric_limits<uint64_t>::max() / 100;
uint8_t hashMaskThreshold = DEFAULT_THRESHOLD_PERCENT;
uint64_t normalized_threshold = hashMaskThreshold * ONE_THRESHOLD_PERCENT;

/** Global flag to indicate we should check to see if there are
 *  block/undo files that should be deleted.  Set on startup
 *  or if we allocate more file space when we're in prune mode
 */
bool fCheckForPruning = false;
bool fPruneMode = false;

extern void RelayNewXUpdate(const uint64_t key, const uint64_t val);
extern bool AbortNode(const std::string &strMessage, const std::string &userMessage = "");

std::string hashMaskThresholdValidator(const uint8_t &value, uint8_t *item, bool validate)
{
    if (validate)
    {
        if (value > hashMaskThreshold)
        {
            std::ostringstream ret;
            ret << "Sorry, your hashMaskThreshold (" << hashMaskThreshold
                << ") is smaller than your proposed new threshold (" << value
                << ").  You can only lower this number, not raise it.";
            return ret.str();
        }
        else if (value == hashMaskThreshold)
        {
            // just return in this case, nothing has changed
            return std::string();
        }
        hashMaskThreshold = value;
        pblocktree->WriteHashMaskThreshold(hashMaskThreshold);
        normalized_threshold = hashMaskThreshold * ONE_THRESHOLD_PERCENT;
        RelayNewXUpdate(XVer::BU_PRUNE_THRESHOLD, normalized_threshold);
    }
    else
    {
        return "Validate was false, no changes were made";
    }
    return std::string();
}

/** Generate a random list of 32 ints between 8 and 255 that will be used as important pruning bits*/
void GenerateRandomPruningHashMask()
{
    // only generate new bits if we dont already have some
    uint256 read_hashMask;
    if (pblocktree->ReadHashMask(read_hashMask))
    {
        return;
    }
    pruneHashMask = UintToArith256(read_hashMask);
    uint64_t seed = GetTime();
    std::mt19937_64 rand(seed); // Standard mersenne_twister_engine seeded with rd()
    arith_uint256 hashMask64(rand());
    pruneHashMask = 0;
    pruneHashMask = pruneHashMask | hashMask64;
    uint256 write_hashMask = ArithToUint256(pruneHashMask);
    pblocktree->WriteHashMask(write_hashMask);
    pblocktree->WriteFlag("hashmaskexists", true);
}

/** Get important pruning bits from a block hash and compare their value to our pruning threshold*/
bool hashMaskCompare(uint256 _blockHash)
{
    arith_uint256 value = UintToArith256(_blockHash) & LSB64_MASK;
    if ((value ^ pruneHashMask) < normalized_threshold) // we keep all blocks below the threshold
    {
        return true;
    }
    return false;
}

bool SetupPruning()
{
    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t nSignedPruneTarget = GetArg("-prune", 0) * 1024 * 1024;
    bool useMask = GetBoolArg("-prunewithmask", DEFAULT_PRUNE_WITH_MASK);
    if (nSignedPruneTarget < 0)
    {
        return InitError(_("Prune cannot be configured with a negative value."));
    }
    nPruneTarget = (uint64_t)nSignedPruneTarget;
    if (nPruneTarget && useMask)
    {
        return InitError(_("Prune and prunewithmask are incompatible, please choose only one"));
    }
    // standard pruning
    if (nPruneTarget)
    {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES)
        {
            return InitError(strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number."),
                MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
        }
        LOGA("Prune configured to target %uMiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
        return true;
    }
    // pruning using a hash mask
    bool haveUsedMask = false;
    pblocktree->ReadFlag("hashmaskexists", haveUsedMask);
    if (haveUsedMask || useMask)
    {
        fPruneWithMask = true;
        GenerateRandomPruningHashMask();
        hashMaskThreshold = 100;
        pblocktree->ReadHashMaskThreshold(hashMaskThreshold);
        uint8_t potentialThreshold = GetArg("-prunethreshold", 100);
        if (potentialThreshold < hashMaskThreshold)
        {
            hashMaskThreshold = potentialThreshold;
            pblocktree->WriteHashMaskThreshold(hashMaskThreshold);
        }
        else if (potentialThreshold > hashMaskThreshold)
        {
            LOGA("cannot raise prunethreshold above %u, keeping it at %u \n", hashMaskThreshold, hashMaskThreshold);
        }
        pruneHashMask = pruneHashMask / hashMaskThreshold;
        fPruneMode = true;
    }
    return true;
}

void UnlinkPrunedFiles(std::set<int> &setFilesToPrune)
{
    for (std::set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it)
    {
        CDiskBlockPos pos(*it, 0);
        fs::remove(GetBlockPosFilename(pos, "blk"));
        fs::remove(GetBlockPosFilename(pos, "rev"));
        LOG(PRUNE, "Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    READLOCK(cs_mapBlockIndex);
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it)
    {
        CBlockIndex *pindex = it->second;
        if (pindex->nFile == fileNumber)
        {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
                range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second)
            {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator itr = range.first;
                range.first++;
                if (itr->second == pindex)
                {
                    mapBlocksUnlinked.erase(itr);
                }
            }
        }
    }
    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    uint64_t retval = 0;
    for (const CBlockFileInfo &file : vinfoBlockFile)
    {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

void PruneFiles(std::set<int> &setFilesToPrune, uint64_t nLastBlockWeCanPrune)
{
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count = 0;

    if (nCurrentUsage + nBuffer >= nPruneTarget)
    {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++)
        {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
            {
                continue;
            }

            if (nCurrentUsage + nBuffer < nPruneTarget) // are we below our target?
            {
                break;
            }

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep
            // scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
            {
                continue;
            }

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LOG(PRUNE, "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
        nPruneTarget / 1024 / 1024, nCurrentUsage / 1024 / 1024,
        ((int64_t)nPruneTarget - (int64_t)nCurrentUsage) / 1024 / 1024, nLastBlockWeCanPrune, count);
}

uint64_t PruneDB(uint64_t nLastBlockWeCanPrune)
{
    CBlockIndex *pindexOldest = chainActive.Tip();
    while (pindexOldest->pprev)
    {
        pindexOldest = pindexOldest->pprev;
    }
    uint64_t prunedCount = 0;
    std::vector<std::string> blockBatch;
    std::vector<std::string> undoBatch;
    while (pindexOldest != nullptr)
    {
        if (pindexOldest->GetBlockHash() == Params().GetConsensus().hashGenesisBlock)
        {
            pindexOldest = chainActive.Next(pindexOldest);
            continue;
        }
        if (pindexOldest->nFile == 0)
        {
            // already pruned block
            pindexOldest = chainActive.Next(pindexOldest);
            continue;
        }
        if (!fPruneWithMask && nDBUsedSpace < nPruneTarget)
        {
            break;
        }
        if (pindexOldest->nHeight >= (int)nLastBlockWeCanPrune)
        {
            break;
        }
        if (fPruneWithMask == true && hashMaskCompare(pindexOldest->GetBlockHash()))
        {
            pindexOldest = chainActive.Next(pindexOldest);
            continue;
        }
        unsigned int blockSize = pindexOldest->nDataPos;
        std::ostringstream key;
        key << pindexOldest->GetBlockTime() << ":" << pindexOldest->GetBlockHash().ToString();
        blockBatch.push_back(key.str());
        undoBatch.push_back(key.str());
        nDBUsedSpace = nDBUsedSpace - blockSize;
        pindexOldest->nStatus &= ~BLOCK_HAVE_DATA;
        pindexOldest->nStatus &= ~BLOCK_HAVE_UNDO;
        pindexOldest->nFile = 0;
        pindexOldest->nDataPos = 0;
        pindexOldest->nUndoPos = 0;
        setDirtyBlockIndex.insert(pindexOldest);
        prunedCount = prunedCount + 1;
        pindexOldest = chainActive.Next(pindexOldest);
    }
    CValidationState state;
    FlushStateToDiskInternal(state);
    for (const auto &key : blockBatch)
    {
        pblockdb->EraseBlock(key);
    }
    for (const auto &key : undoBatch)
    {
        pblockdb->EraseUndo(key);
    }
    LOG(PRUNE, "Pruned %u blocks, size on disk %u\n", prunedCount, nDBUsedSpace);
    return prunedCount;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
    {
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));
    }

    // we only use this metric with pblockdb
    if (pblockdb && fPruneMode)
    {
        nDBUsedSpace += nAdditionalBytes;
        if (nDBUsedSpace >= nPruneTarget)
        {
            fCheckForPruning = true;
        }
    }

    return true;
}

/* Calculate the files that should be deleted to remain*/
void FindFilesToPrune(std::set<int> &setFilesToPrune, uint64_t nPruneAfterHeight, bool &fFlushForPrune)
{
    LOCK2(cs_main, cs_LastBlockFile);

    if (chainActive.Tip() == NULL || (nPruneTarget == 0 && !fPruneWithMask))
    {
        return;
    }
    if ((uint64_t)chainActive.Tip()->nHeight <= nPruneAfterHeight)
    {
        return;
    }
    uint64_t nLastBlockWeCanPrune = chainActive.Tip()->nHeight - Params().MinBlocksToKeep();

    if (!pblockdb)
    {
        PruneFiles(setFilesToPrune, nLastBlockWeCanPrune);
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
    else // if (pblockdb)
    {
        if (!fPruneWithMask)
        {
            if (nDBUsedSpace < nPruneTarget + (pruneIntervalTweak.Value() * 1024 * 1024))
            {
                return;
            }
        }
        uint64_t amntPruned = PruneDB(nLastBlockWeCanPrune);
        // because we just prune the DB here and dont have a file set to return, we need to set prune triggers here
        // otherwise they will check for the fileset and incorrectly never be set

        // if this is the first time we attempt to prune, dont set pruned = true if we didnt prune anything so we must
        // check amntPruned here
        if (!fHavePruned && amntPruned != 0)
        {
            pblocktree->WriteFlag("prunedblockfiles", true);
            fHavePruned = true;
        }
    }
}
