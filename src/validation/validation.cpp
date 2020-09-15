// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"

#include "blockrelay/blockrelay_common.h"
#include "blockstorage/blockstorage.h"
#include "blockstorage/sequential_files.h"
#include "checkpoints.h"
#include "connmgr.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "dosman.h"
#include "expedited.h"
#include "index/txindex.h"
#include "init.h"
#include "requestManager.h"
#include "sync.h"
#include "timedata.h"
#include "txadmission.h"
#include "txorphanpool.h"
#include "ui_interface.h"
#include "util.h"
#include "validationinterface.h"

#include <boost/scope_exit.hpp>
#include <unordered_set>

extern CTweak<unsigned int> unconfPushAction;
void ProcessOrphans(std::vector<uint256> &vWorkQueue);

class Hasher
{
private:
    /** Salt */
    const uint64_t k0, k1;

public:
    Hasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}
    uint64_t operator()(const uint256 &hash) const { return SipHashUint256(k0, k1, hash); }
};

// Stores hashes of blocks that have already successfully passed CheckBlock().
CCriticalSection cs_BlocksAlreadyChecked;
std::unordered_set<uint256, Hasher> setBlocksAlreadyChecked GUARDED_BY(cs_BlocksAlreadyChecked);
// We don't let this set grow unbounded just in case we forget to erase values later.
const unsigned int MAX_SETBLOCKSALREADYCHECKED_SIZE = 5000;

struct CBlockIndexWorkComparator
{
    bool operator()(CBlockIndex *pa, CBlockIndex *pb) const
    {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork)
            return false;
        if (pa->nChainWork < pb->nChainWork)
            return true;

        // ... then by earliest time received, ...
        if (pa->nSequenceId < pb->nSequenceId)
            return false;
        if (pa->nSequenceId > pb->nSequenceId)
            return true;

        // Use pointer address as tie breaker (should only happen with blocks
        // loaded from disk, as those all have id 0).
        if (pa < pb)
            return false;
        if (pa > pb)
            return true;

        // Identical blocks.
        return false;
    }
};

// bip135 begin
// keep track of count over last 100
struct UnknownForkData
{
    int UnknownForkSignalStrength{0};
    bool UnknownForkSignalFirstDetected{false};
    bool UnknownForkSignalLost{false};
    bool UnknownForkSignalAt25Percent{false};
    bool UnknownForkSignalAt50Percent{false};
    bool UnknownForkSignalAt70Percent{false};
    bool UnknownForkSignalAt90Percent{false};
    bool UnknownForkSignalAt95Percent{false};
};

static UnknownForkData unknownFork[Consensus::MAX_VERSION_BITS_DEPLOYMENTS];
// bip135 end

/**
 * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
 * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
 * missing the data for the block.
 */
std::set<CBlockIndex *, CBlockIndexWorkComparator> setBlockIndexCandidates GUARDED_BY(cs_main);

// Last time the block tip was updated
std::atomic<int64_t> nTimeBestReceived{0};

CBlockIndex *pindexBestForkTip = nullptr;
CBlockIndex *pindexBestForkBase = nullptr;

extern uint64_t nBlockSequenceId;
extern bool fCheckForPruning;
extern std::map<uint256, NodeId> mapBlockSource;
extern std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;
extern bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage = "");
extern void LimitMempoolSize(CTxMemPool &pool, size_t limit, unsigned long age);
extern void AlertNotify(const std::string &strMessage);
extern std::set<int> setDirtyFileInfo;
extern std::map<uint256, std::pair<CBlockHeader, int64_t> > mapUnConnectedHeaders;
extern std::atomic<int> nPreferredDownload;
extern int nSyncStarted;
extern bool fLargeWorkForkFound;
extern bool fLargeWorkInvalidChainFound;

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;
static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

// Protected by cs_main
static ThresholdConditionCache warningcache[Consensus::MAX_VERSION_BITS_DEPLOYMENTS];

//////////////////////////////////////////////////////////////////
//
// Header
//

bool CheckBlockHeader(const CBlockHeader &block, CValidationState &state, bool fCheckPOW)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus()))
        return state.DoS(50, error("CheckBlockHeader(): proof of work failed"), REJECT_INVALID, "high-hash");

    // Check timestamp
    if (block.GetBlockTime() > GetAdjustedTime() + 2 * 60 * 60)
        return state.Invalid(
            error("CheckBlockHeader(): block timestamp too far in the future"), REJECT_INVALID, "time-too-new");

    return true;
}


bool ContextualCheckBlockHeader(const CBlockHeader &block, CValidationState &state, CBlockIndex *const pindexPrev)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;

    // Check proof of work
    uint32_t expectedNbits = GetNextWorkRequired(pindexPrev, &block, consensusParams);
    if (block.nBits != expectedNbits)
    {
        return state.DoS(100, error("%s: incorrect proof of work. Height %d, Block nBits 0x%x, expected 0x%x", __func__,
                                  nHeight, block.nBits, expectedNbits),
            REJECT_INVALID, "bad-diffbits");
    }

    if (fCheckpointsEnabled)
    {
        const CChainParams &chainparams = Params();
        // If the parent block belongs to the set of checkpointed blocks but it has a mismatched hash,
        // then we are on the wrong fork so ignore
        if (!CheckAgainstCheckpoint(pindexPrev->nHeight, *pindexPrev->phashBlock, chainparams))
        {
            return error("%s: CheckAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());
        }
        READLOCK(cs_mapBlockIndex);
        const CBlockIndex *pindexMainChain = chainActive[nHeight];
        if (pindexMainChain && pindexMainChain->GetBlockHeader() == block)
        {
            return true; // if this header is in the main chain it is valid and we are done
        }
        // Don't accept any forks from the main chain prior to the last checkpoint.
        CBlockIndex *pcheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        if (pcheckpoint && nHeight < pcheckpoint->nHeight)
        {
            return state.DoS(100,
                error("%s: forked chain is older than last checkpoint (height %d)", __func__, nHeight),
                REJECT_CHECKPOINT, "bad-fork-prior-to-checkpoint");
        }
    }

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(error("%s: block's timestamp is too early", __func__), REJECT_INVALID, "time-too-old");

    // Reject outdated version blocks when 95% (75% on testnet) of the network has upgraded:
    // check for version 2, 3 and 4 upgrades
    if ((block.nVersion < 2 && nHeight >= consensusParams.BIP34Height) ||
        (block.nVersion < 3 && nHeight >= consensusParams.BIP66Height) ||
        (block.nVersion < 4 && nHeight >= consensusParams.BIP65Height))
    {
        return state.Invalid(
            error("%s: rejected nVersion=0x%08x block", __func__, block.nVersion), REJECT_OBSOLETE, "bad-version");
    }

    return true;
}

static void NotifyHeaderTip()
{
    if (!pindexBestHeader.load())
        return;

    static std::atomic<CBlockIndex *> pindexHeaderOld{pindexBestHeader.load()};
    static std::atomic<int64_t> nLastTime{0};
    if (pindexBestHeader.load()->nChainWork > pindexHeaderOld.load()->nChainWork &&
        (GetTime() - nLastTime > 1 || !IsInitialBlockDownload()))
    {
        uiInterface.NotifyHeaderTip(false, pindexBestHeader.load(), true);
        pindexHeaderOld.store(pindexBestHeader.load());
        nLastTime = GetTime();
    }
}
bool AcceptBlockHeader(const CBlockHeader &block,
    CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex **ppindex)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetHash();
    CBlockIndex *pindex = nullptr;
    if (hash != chainparams.GetConsensus().hashGenesisBlock)
    {
        pindex = LookupBlockIndex(hash);
        if (pindex)
        {
            // Block header is already known.
            if (ppindex)
                *ppindex = pindex;
            {
                READLOCK(cs_mapBlockIndex);
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return state.Invalid(
                        error("%s: block %s height %d is marked invalid", __func__, hash.ToString(), pindex->nHeight),
                        0, "duplicate");
            }
            return true;
        }

        if (!CheckBlockHeader(block, state))
            return false;

        // Get prev block index
        CBlockIndex *pindexPrev = LookupBlockIndex(block.hashPrevBlock);
        if (!pindexPrev)
            return state.DoS(10, error("%s: previous block %s not found while accepting %s", __func__,
                                     block.hashPrevBlock.ToString(), hash.ToString()),
                0, "bad-prevblk");
        {
            READLOCK(cs_mapBlockIndex);
            if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
                return state.DoS(100,
                    error("%s: previous block %s is invalid", __func__, pindexPrev->GetBlockHash().GetHex().c_str()),
                    REJECT_INVALID, "bad-prevblk");
        }

        if (!ContextualCheckBlockHeader(block, state, pindexPrev))
            return false;
    }
    if (pindex == nullptr)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    return true;
}

//////////////////////////////////////////////////////////////////
//
// Blockindex
//

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
void PruneBlockIndexCandidates()
{
    AssertLockHeld(cs_main);
    if (setBlockIndexCandidates.empty())
        return; // nothing to prune

    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex *, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip()))
    {
        setBlockIndexCandidates.erase(it++);
    }
}

CBlockIndex *AddToBlockIndex(const CBlockHeader &block)
{
    WRITELOCK(cs_mapBlockIndex);
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex *pindexNew = new CBlockIndex(block);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
        // If the prior block or an ancestor has failed, mark this one failed
        if (pindexNew->pprev && pindexNew->pprev->nStatus & BLOCK_FAILED_MASK)
            pindexNew->nStatus |= BLOCK_FAILED_CHILD;
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);

    // If the block belongs to the set of check-pointed blocks but it has a mismatched hash,
    // then we are on the wrong fork so ignore.
    if (fCheckpointsEnabled && !CheckAgainstCheckpoint(pindexNew->nHeight, *pindexNew->phashBlock, Params()))
    {
        pindexNew->nStatus |= BLOCK_FAILED_VALID; // block doesn't match checkpoints so invalid
        pindexNew->nStatus &= ~BLOCK_VALID_CHAIN;
    }

    // Lastly, set the best header if this is a valid header on a valid chain and if the chain work
    // is higher than the previous best header.
    CBlockIndex *pBestHeader = pindexBestHeader.load();
    if ((!(pindexNew->nStatus & BLOCK_FAILED_MASK)) &&
        (pBestHeader == nullptr || pBestHeader->nChainWork < pindexNew->nChainWork))
    {
        pindexBestHeader.store(pindexNew);
    }

    // Update the ui if the best header has changed.
    NotifyHeaderTip();

    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

CBlockIndex *LookupBlockIndex(const uint256 &hash)
{
    READLOCK(cs_mapBlockIndex);
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi == mapBlockIndex.end())
        return nullptr;
    return mi->second; // I can return this CBlockIndex because header pointers are never deleted
}

CBlockIndex *InsertBlockIndex(const uint256 &hash)
{
    if (hash.IsNull())
        return nullptr;
    WRITELOCK(cs_mapBlockIndex);

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex *pindexNew = new CBlockIndex();
    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool LoadBlockIndexDB()
{
    // Open and read all ldb index files so that they are in the Operating System file cache before we iterate
    // through the index files and load the block index. While this is a bit of a hack, it has an enormous
    // effect on speeding up the reading in of the block index data particularly when the index data is stored
    // on spinning disk. The problem appears to lie in some issue with leveldb cache innefficiency.
    fs::path path_index;
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        path_index = GetDataDir() / "blocks" / "index";
    }
    else
    {
        path_index = GetDataDir() / "blockdb" / "index";
    }
    std::vector<fs::path> vIndexFiles;
    std::copy(fs::directory_iterator(path_index), fs::directory_iterator(), std::back_inserter(vIndexFiles));
    for (const auto path_file : vIndexFiles)
    {
        if (fs::extension(path_file) == ".ldb")
        {
            FILE *file = fsbridge::fopen(path_file, "rb");
            const unsigned int nBuffSize = 65536;
            char buff[nBuffSize];
            if (file)
            {
                // Just read (but do nothing with) the contents of the
                // file so that it ends up in the OS cache
                while (fgets(buff, nBuffSize, file))
                {
                }
                fclose(file);
            }
        }
    }

    // Load the block index data
    const CChainParams &chainparams = Params();
    if (!pblocktree->LoadBlockIndexGuts())
    {
        return false;
    }

    LOCK(cs_main);
    /** This sync method will break on pruned nodes so we cant use if pruned*/
    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (!fHavePruned)
    {
        // by default we want to sync from disk instead of network if possible
        // run a db sync here to sync storage methods
        // may increase startup time significantly but is faster than network sync
        SyncStorage(chainparams);
    }

    delete pblocktreeother;
    pblocktreeother = nullptr;
    // lock cs_mapBlockIndex after running SyncStorage to avoid an issue with
    // deadlocks, this is only safe to do because only the programs main thread
    // is running at the time LocdBlockIndexDB is called.
    WRITELOCK(cs_mapBlockIndex);
    try
    {
        if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
        {
            fs::remove_all(GetDataDir() / "blockdb");
        }
        else
        {
            fs::remove_all(GetDataDir() / "blocks");
        }
    }
    catch (boost::filesystem::filesystem_error const &e)
    {
        LOG(PRUNE, "%s \n", e.code().message());
    }

    if (shutdown_threads.load() == true)
    {
        return false;
    }

    // Gather data necessary to perform the following checks
    std::vector<std::pair<int, CBlockIndex *> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    std::set<int> setBlkDataFiles;
    for (const std::pair<uint256, CBlockIndex *> &item : mapBlockIndex)
    {
        CBlockIndex *pindex = item.second;
        vSortedByHeight.push_back(std::make_pair(pindex->nHeight, pindex));

        if (pindex->nStatus & BLOCK_HAVE_DATA)
        {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }

    // Calculate nChainWork
    std::sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for (const std::pair<int, CBlockIndex *> &item : vSortedByHeight)
    {
        CBlockIndex *pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0)
        {
            if (pindex->pprev)
            {
                if (pindex->pprev->nChainTx)
                {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                }
                else
                {
                    pindex->nChainTx = 0;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            }
            else
            {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (fCheckpointsEnabled && !CheckAgainstCheckpoint(pindex->nHeight, *pindex->phashBlock, chainparams))
        {
            pindex->nStatus |= BLOCK_FAILED_VALID; // block doesn't match checkpoints so invalid
            pindex->nStatus &= ~BLOCK_VALID_CHAIN;
        }
        if ((pindex->pprev) && (pindex->pprev->nStatus & BLOCK_FAILED_MASK))
        {
            // if the parent is invalid I am too
            pindex->nStatus |= BLOCK_FAILED_CHILD;
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == nullptr))
            setBlockIndexCandidates.insert(pindex);
        CBlockIndex *pBestInvalid = pindexBestInvalid.load();
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pBestInvalid || pindex->nChainWork > pBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        CBlockIndex *pBestHeader = pindexBestHeader.load();
        if (pindex->IsValid(BLOCK_VALID_TREE) &&
            (pBestHeader == nullptr || CBlockIndexWorkComparator()(pBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    if (!pblockdb) // sequential files
    {
        // Check presence of blk files

        LOGA("Checking all blk files are present...\n");
        for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
        {
            CDiskBlockPos pos(*it, 0);
            fs::path path = GetBlockPosFilename(pos, "blk");
            if (!fs::exists(path))
            {
                fs::file_status s = fs::status(path);
                LOGA("missing path = %s which has status of %u \n", path.string().c_str(), s.type());
                return false;
            }
        }
        // Load block file info
        pblocktree->ReadLastBlockFile(nLastBlockFile);
        vinfoBlockFile.resize(nLastBlockFile + 1);
        LOGA("%s: last block file = %i\n", __func__, nLastBlockFile);
        for (int nFile = 0; nFile <= nLastBlockFile; nFile++)
        {
            pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
        }
        LOGA("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
        for (int nFile = nLastBlockFile + 1; true; nFile++)
        {
            CBlockFileInfo info;
            if (pblocktree->ReadBlockFileInfo(nFile, info))
            {
                vinfoBlockFile.push_back(info);
            }
            else
            {
                break;
            }
        }
    }

    if (fHavePruned)
    {
        LOGA("LoadBlockIndexDB(): Block files have previously been pruned\n");
    }

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    if (fReindexing)
        fReindex = fReindexing;

    // Load pointer to end of best chain
    uint256 bestblockhash = pcoinsdbview->GetBestBlock();
    BlockMap::iterator it = mapBlockIndex.find(bestblockhash);
    if (it == mapBlockIndex.end())
    {
        return true;
    }
    chainActive.SetTip(it->second);

    PruneBlockIndexCandidates();

    LOGA("%s: hashBestChain=%s height=%d date=%s progress=%f\n", __func__, chainActive.Tip()->GetBlockHash().ToString(),
        chainActive.Height(), DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), chainActive.Tip()));

    return true;
}

void UnloadBlockIndex()
{
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        orphanpool.mapOrphanTransactions.clear();
        orphanpool.mapOrphanTransactionsByPrev.clear();
        orphanpool.nBytesOrphanPool = 0;
    }

    nPreferredDownload.store(0);
    nodestate.Clear();
    requester.MapBlocksInFlightClear();
    requester.MapNodestateClear();
    mempool.clear();

    {
        LOCK(cs_main);
        nBlockSequenceId = 1;
        nSyncStarted = 0;
        nLastBlockFile = 0;
        mapUnConnectedHeaders.clear();
        setBlockIndexCandidates.clear();
        chainActive.SetTip(nullptr);
        pindexBestInvalid = nullptr;
        pindexBestHeader = nullptr;
        ResetASERTAnchorBlockCache();
        mapBlocksUnlinked.clear();
        vinfoBlockFile.clear();
        mapBlockSource.clear();
        setDirtyBlockIndex.clear();
        setDirtyFileInfo.clear();
        versionbitscache.Clear();
        for (int b = 0; b < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; b++)
        {
            warningcache[b].clear();
        }
    }

    {
        WRITELOCK(cs_mapBlockIndex);

        for (BlockMap::value_type &entry : mapBlockIndex)
        {
            delete entry.second;
        }
        mapBlockIndex.clear();
    }

    fHavePruned = false;
    recentRejects.reset();
}

bool LoadBlockIndex()
{
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexDB())
        return false;
    return true;
}

bool InitBlockIndex(const CChainParams &chainparams)
{
    LOCK(cs_main);

    // Initialize global variables that cannot be constructed at startup.

    // Check whether we're already initialized
    if (chainActive.Genesis() != nullptr)
        return true;

    LOGA("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    if (!fReindexing)
    {
        try
        {
            CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
            {
                return error("LoadBlockIndex(): FindBlockPos failed");
            }
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            {
                return error("LoadBlockIndex(): writing genesis block to disk failed");
            }
            CBlockIndex *pindex = AddToBlockIndex(block);
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
            {
                return error("LoadBlockIndex(): genesis block not accepted");
            }
            if (!ActivateBestChain(state, chainparams, &block, false))
            {
                return error("LoadBlockIndex(): genesis block cannot be activated");
            }
            // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
            return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
        }
        catch (const std::runtime_error &e)
        {
            return error("LoadBlockIndex(): failed to initialize block database: %s", e.what());
        }
    }
    return true;
}

void CheckBlockIndex(const Consensus::Params &consensusParams)
{
    if (!fCheckBlockIndex)
    {
        return;
    }

    LOCK(cs_main);
    READLOCK(cs_mapBlockIndex);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chainActive.Height() < 0)
    {
        assert(mapBlockIndex.size() <= 1);
        return;
    }
    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex *, CBlockIndex *> forward;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++)
    {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mapBlockIndex.size());

    std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
        std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
        rangeGenesis = forward.equal_range(nullptr);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent nullptr.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex *pindexFirstInvalid = nullptr; // Oldest ancestor of pindex which is invalid.
    CBlockIndex *pindexFirstMissing = nullptr; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex *pindexFirstNeverProcessed = nullptr; // Oldest ancestor of pindex for which nTx == 0.
    // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex *pindexFirstNotTreeValid = nullptr;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex *pindexFirstNotTransactionsValid = nullptr;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex *pindexFirstNotChainValid = nullptr;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    CBlockIndex *pindexFirstNotScriptsValid = nullptr;
    while (pindex != nullptr)
    {
        nNodes++;
        if (pindexFirstInvalid == nullptr && pindex->nStatus & BLOCK_FAILED_VALID)
            pindexFirstInvalid = pindex;
        if (pindexFirstMissing == nullptr && !(pindex->nStatus & BLOCK_HAVE_DATA))
            pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == nullptr && pindex->nTx == 0)
            pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE)
            pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTransactionsValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS)
            pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotChainValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN)
            pindexFirstNotChainValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotScriptsValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS)
            pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == nullptr)
        {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        // nSequenceId can't be set for blocks that aren't linked
        if (pindex->nChainTx == 0)
            assert(pindex->nSequenceId == 0);
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned)
        {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        }
        else
        {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA)
                assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO)
            assert(pindex->nStatus & BLOCK_HAVE_DATA);
        // This is pruning-independent.
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0));
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is
        // equivalent to nChainTx being set.
        // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNeverProcessed != nullptr) == (pindex->nChainTx == 0));
        assert((pindexFirstNotTransactionsValid != nullptr) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(pindex->pprev == nullptr || pindex->nChainWork >= pindex->pprev->nChainWork);
        // The pskip pointer must point back for all but the first 2 blocks.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight)));
        assert(pindexFirstNotTreeValid == nullptr); // All mapBlockIndex entries must at least be TREE valid
        // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE)
            assert(pindexFirstNotTreeValid == nullptr);
        // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN)
            assert(pindexFirstNotChainValid == nullptr);
        // SCRIPTS valid implies all parents are SCRIPTS valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS)
            assert(pindexFirstNotScriptsValid == nullptr);
        if (pindexFirstInvalid == nullptr)
        {
            // Checks for not-invalid blocks.
            // The failed mask cannot be set for blocks without invalid parents.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0);
        }

        /*  This section does not apply to PV since blocks can arrive and be processed in potentially any order.
            Leaving the commented section for now for further review.
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstNeverProcessed == nullptr) {
            if (pindexFirstInvalid == nullptr) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.

                // PV:  this is no longer true under certain condition for PV - leaving it in here for further review
                // BU: if the chain is excessive it won't be on the list of active chain candidates
                //if ((!chainContainsExcessive(pindex)) && (pindexFirstMissing == nullptr || pindex ==
        chainActive.Tip()) )
                //    assert(setBlockIndexCandidates.count(pindex));

                    // If some parent is missing, then it could be that this block was in
                    // setBlockIndexCandidates but had to be removed because of the missing data.
                    // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in
        setBlockIndexCandidates.
        } else {
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        */

        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
            std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
            rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second)
        {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex)
            {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != nullptr &&
            pindexFirstInvalid == nullptr)
        {
            // If this block has block data available, some parent was never received, and has no invalid parents, it
            // must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (!(pindex->nStatus & BLOCK_HAVE_DATA))
            assert(!foundInUnlinked);
        // BU: blocks that are excessive are placed in the unlinked map
        if ((pindexFirstMissing == nullptr) && (!chainContainsExcessive(pindex)))
        {
            assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == nullptr &&
            pindexFirstMissing != nullptr)
        {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently
            // missing data for some parent.
            assert(fHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0)
            {
                if (pindexFirstInvalid == nullptr)
                {
                    assert(foundInUnlinked);
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
            std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
            range = forward.equal_range(pindex);
        if (range.first != range.second)
        {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex)
        {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid)
                pindexFirstInvalid = nullptr;
            if (pindex == pindexFirstMissing)
                pindexFirstMissing = nullptr;
            if (pindex == pindexFirstNeverProcessed)
                pindexFirstNeverProcessed = nullptr;
            if (pindex == pindexFirstNotTreeValid)
                pindexFirstNotTreeValid = nullptr;
            if (pindex == pindexFirstNotTransactionsValid)
                pindexFirstNotTransactionsValid = nullptr;
            if (pindex == pindexFirstNotChainValid)
                pindexFirstNotChainValid = nullptr;
            if (pindex == pindexFirstNotScriptsValid)
                pindexFirstNotScriptsValid = nullptr;
            // Find our parent.
            CBlockIndex *pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
                rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex)
            {
                // Our parent must have at least the node we're coming from as child.
                assert(rangePar.first != rangePar.second);
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second)
            {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            }
            else
            {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}


//////////////////////////////////////////////////////////////////
//
// Transactions
//

bool CheckInputs(const CTransactionRef &tx,
    CValidationState &state,
    const CCoinsViewCache &inputs,
    bool fScriptChecks,
    unsigned int flags,
    unsigned int maxOps,
    bool cacheStore,
    ValidationResourceTracker *resourceTracker,
    std::vector<CScriptCheck> *pvChecks,
    unsigned char *sighashType,
    CValidationDebugger *debugger)
{
    bool allPassed = true;
    if (!tx->IsCoinBase())
    {
        if (!Consensus::CheckTxInputs(tx, state, inputs))
        {
            if (debugger)
            {
                debugger->SetInputCheckResult(false);
                debugger->AddInputCheckError(state.GetRejectReason());
                debugger->FinishCheckInputSession();
            }
            return false;
        }
        if (pvChecks)
            pvChecks->reserve(tx->vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks before the
        // last block chain checkpoint. Assuming the checkpoints are valid this
        // is safe because block merkle hashes are still computed and checked,
        // and any change will be caught at the next checkpoint. Of course, if
        // the checkpoint is for a chain that's invalid due to false scriptSigs
        // this optimisation would allow an invalid chain to be accepted.
        if (fScriptChecks)
        {
            for (unsigned int i = 0; i < tx->vin.size(); i++)
            {
                const COutPoint &prevout = tx->vin[i].prevout;
                const CScript &scriptSig = tx->vin[i].scriptSig;
                CoinAccessor coin(inputs, prevout);

                if (debugger)
                {
                    if (coin->IsSpent())
                    {
                        debugger->SetInputCheckResult(false);
                        debugger->AddInputCheckError(strprintf("COutPoint %s is spent", prevout.ToString().c_str()));
                        debugger->FinishCheckInputSession();
                        return false;
                    }
                }
                else
                {
                    assert(!coin->IsSpent());
                }

                // We very carefully only pass in things to CScriptCheck which
                // are clearly committed. This provides
                // a sanity check that our caching is not introducing consensus
                // failures through additional data in, eg, the coins being
                // spent being checked as a part of CScriptCheck.
                const CScript scriptPubKey = coin->out.scriptPubKey;
                const CAmount amount = coin->out.nValue;
                bool inputVerified = true;
                if (debugger)
                {
                    debugger->AddInputCheckMetadata("prevtx", prevout.hash.ToString());
                    debugger->AddInputCheckMetadata("n", std::to_string(prevout.n));
                    debugger->AddInputCheckMetadata("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));
                    debugger->AddInputCheckMetadata("scriptSig", HexStr(scriptSig.begin(), scriptSig.end()));
                    debugger->AddInputCheckMetadata("amount", std::to_string(amount));
                }

                // Verify signature
                if (pvChecks)
                {
                    pvChecks->push_back(
                        CScriptCheck(resourceTracker, scriptPubKey, amount, *tx, i, flags, maxOps, cacheStore));
                }
                else
                {
                    CScriptCheck check(resourceTracker, scriptPubKey, amount, *tx, i, flags, maxOps, cacheStore);
                    if (!check())
                    {
                        ScriptError scriptError = check.GetScriptError();
                        // Compute flags without the optional standardness flags.
                        // This differs from MANDATORY_SCRIPT_VERIFY_FLAGS as it contains
                        // additional upgrade flags (see ParallelAcceptToMemoryPool variable
                        // featureFlags).
                        uint32_t mandatoryFlags =
                            (flags & ~(STANDARD_NOT_MANDATORY_VERIFY_FLAGS | SCRIPT_VERIFY_INPUT_SIGCHECKS));
                        if (flags != mandatoryFlags)
                        {
                            // Check whether the failure was caused by a
                            // non-mandatory script verification check, such as
                            // non-standard DER encodings or non-null dummy
                            // arguments; if so, don't trigger DoS protection to
                            // avoid splitting the network between upgraded and
                            // non-upgraded nodes.
                            CScriptCheck check2(
                                nullptr, scriptPubKey, amount, *tx, i, mandatoryFlags, maxOps, cacheStore);
                            if (check2())
                            {
                                if (debugger)
                                {
                                    debugger->AddInputCheckError(strprintf(
                                        "non-mandatory-script-verify-flag (%s)", ScriptErrorString(scriptError)));
                                    inputVerified = false;
                                    allPassed = false;
                                }
                                else
                                {
                                    return state.Invalid(
                                        false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)",
                                                                       ScriptErrorString(scriptError)));
                                }
                            }
                            // update the error message to reflect the mandatory violation.
                            scriptError = check2.GetScriptError();
                        }

                        // Before banning, we need to check whether the transaction would
                        // be valid on the other side of the upgrade, so as to avoid
                        // splitting the network between upgraded and non-upgraded nodes.
                        // Note that this will create strange error messages like
                        // "upgrade-conditional-script-failure (Opcode missing or not
                        // understood)".
                        CScriptCheck check3(nullptr, scriptPubKey, amount, *tx, i,
                            mandatoryFlags ^ SCRIPT_ENABLE_OP_REVERSEBYTES, maxOps, cacheStore);
                        if (check3())
                        {
                            if (debugger)
                            {
                                debugger->AddInputCheckError(strprintf("upgrade-conditional-script-failure (%s)",
                                    ScriptErrorString(check.GetScriptError())));
                                inputVerified = false;
                                allPassed = false;
                            }
                            else
                            {
                                return state.Invalid(
                                    false, REJECT_INVALID, strprintf("upgrade-conditional-script-failure (%s)",
                                                               ScriptErrorString(check.GetScriptError())));
                            }
                        }

                        // Failures of other flags indicate a transaction that is
                        // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                        // such nodes as they are not following the protocol. That
                        // said during an upgrade careful thought should be taken
                        // as to the correct behavior - we may want to continue
                        // peering with non-upgraded nodes even after a soft-fork
                        // super-majority vote has passed.
                        if (debugger)
                        {
                            inputVerified = false;
                            allPassed = false;
                            debugger->AddInputCheckError(
                                strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(scriptError)));
                        }
                        else
                        {
                            return state.DoS(100, false, REJECT_INVALID,
                                strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(scriptError)));
                        }
                    }
                    if (sighashType)
                        *sighashType = check.sighashType;
                }
                if (debugger)
                {
                    debugger->SetInputCheckValidity(inputVerified);
                    debugger->IncrementCheckIndex();
                }
            }
        }
    }
    if (debugger)
    {
        debugger->SetInputCheckResult(allPassed);
        debugger->FinishCheckInputSession();
    }
    return true;
}


//////////////////////////////////////////////////////////////////
//
// Block/chain
//

bool ReconsiderBlock(CValidationState &state, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    READLOCK(cs_mapBlockIndex);
    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end())
    {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex)
        {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx &&
                setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second))
            {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid.load())
            {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = nullptr;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != nullptr)
    {
        if (pindex->nStatus & BLOCK_FAILED_MASK)
        {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

bool TestBlockValidity(CValidationState &state,
    const CChainParams &chainparams,
    const CBlock &block,
    CBlockIndex *pindexPrev,
    bool fCheckPOW,
    bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == chainActive.Tip());
    // Ensure that if there is a checkpoint on this height, that this block is the one.
    if (fCheckpointsEnabled && !CheckAgainstCheckpoint(pindexPrev->nHeight + 1, block.GetHash(), chainparams))
        return error("%s: CheckAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, state, fCheckPOW, fCheckMerkleRoot))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, chainparams, true))
        return false;
    assert(state.IsValid());

    return true;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params &consensusParams)
{
    int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
}

int32_t ComputeBlockVersion(const CBlockIndex *pindexPrev, const Consensus::Params &params)
{
    LOCK(cs_main);
    int32_t nVersion = VERSIONBITS_TOP_BITS;

    for (int i = 0; i < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++)
    {
        // bip135 begin
        // guard this because not all deployments have window/threshold
        if (IsConfiguredDeployment(params, i))
        {
            const struct ForkDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[i];
            ThresholdState state = VersionBitsState(pindexPrev, params, (Consensus::DeploymentPos)i, versionbitscache);
            // activate the bits that are STARTED or LOCKED_IN according to their deployments
            if (state == THRESHOLD_LOCKED_IN || (state == THRESHOLD_STARTED && vbinfo.myVote == true))
            {
                nVersion |= VersionBitsMask(params, (Consensus::DeploymentPos)i);
            }
        }
        // bip135 end
    }

    return nVersion;
}

int32_t UnlimitedComputeBlockVersion(const CBlockIndex *pindexPrev, const Consensus::Params &params, uint32_t nTime)
{
    if (blockVersion != 0) // BU: allow override of block version
    {
        return blockVersion;
    }

    int32_t nVersion = ComputeBlockVersion(pindexPrev, params);

    return nVersion;
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
CBlockIndex *FindMostWorkChain()
{
    AssertLockHeld(cs_main);
    WRITELOCK(cs_mapBlockIndex);

    do
    {
        CBlockIndex *pindexNew = nullptr;

        // Find the best candidate header.
        {
            std::set<CBlockIndex *, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return nullptr;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        uint64_t depth = 0;
        bool fFailedChain = false;
        bool fMissingData = false;
        bool fRecentExcessive = false; // Has there been a excessive block within our accept depth?
        // Was there an excessive block prior to our accept depth (if so we ignore the accept depth -- this chain has
        // already been accepted as valid)
        bool fOldExcessive = false;
        // follow the chain all the way back to where it joins the current active chain.
        while (pindexTest && !chainActive.Contains(pindexTest))
        {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (depth < excessiveAcceptDepth)
            {
                // Unlimited: deny this candidate chain if there's a recent excessive block
                fRecentExcessive |= ((pindexTest->nStatus & BLOCK_EXCESSIVE) != 0);
            }
            else
            {
                // Unlimited: unless there is an even older excessive block
                fOldExcessive |= ((pindexTest->nStatus & BLOCK_EXCESSIVE) != 0);
            }

            if (fFailedChain | fMissingData | fRecentExcessive)
                break;
            pindexTest = pindexTest->pprev;
            depth++;
        }

        // If there was a recent excessive block, check a certain distance beyond the acceptdepth to see if this chain
        // has already seen an excessive block... if it has then allow the chain.
        // This stops the client from always tracking excessiveDepth blocks behind the chain tip in a situation where
        // lots of excessive blocks are being created.
        // But after a while with no excessive blocks, we reset and our reluctance to accept an excessive block resumes
        // on this chain.
        // An alternate algorithm would be to move the excessive block size up to match the size of the accepted block,
        // but this changes a user-defined field and is awkward to code because
        // block sizes are not saved.
        if ((fRecentExcessive && !fOldExcessive) && (depth < excessiveAcceptDepth + EXCESSIVE_BLOCK_CHAIN_RESET))
        {
            CBlockIndex *chain = pindexTest;
            // skip accept depth blocks, we are looking for an older excessive
            while (chain && (depth < excessiveAcceptDepth))
            {
                chain = chain->pprev;
                depth++;
            }

            while (chain && (depth < excessiveAcceptDepth + EXCESSIVE_BLOCK_CHAIN_RESET))
            {
                fOldExcessive |= ((chain->nStatus & BLOCK_EXCESSIVE) != 0);
                chain = chain->pprev;
                depth++;
            }
        }

        // Conditions where we want to reject the chain
        if (fFailedChain || fMissingData || (fRecentExcessive && !fOldExcessive))
        {
            // Candidate chain is not usable (either invalid or missing data)
            CBlockIndex *pBestInvalid = pindexBestInvalid.load();
            if (fFailedChain && (pBestInvalid == nullptr || pindexNew->nChainWork > pBestInvalid->nChainWork))
                pindexBestInvalid = pindexNew;
            CBlockIndex *pindexFailed = pindexNew;
            // Remove the entire chain from the set.
            while (pindexTest != pindexFailed)
            {
                if (fFailedChain)
                {
                    pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                }
                else if (fMissingData || (fRecentExcessive && !fOldExcessive))
                {
                    // If we're missing data, then add back to mapBlocksUnlinked,
                    // so that if the block arrives in the future we can try adding
                    // to setBlockIndexCandidates again.
                    mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                }
                setBlockIndexCandidates.erase(pindexFailed);
                pindexFailed = pindexFailed->pprev;
            }
            setBlockIndexCandidates.erase(pindexTest);
            fInvalidAncestor = true;
        }

        if (!fInvalidAncestor)
            return pindexNew;
    } while (true);
    DbgAssert(0, return nullptr); // should never get here
}

bool InvalidateBlock(CValidationState &state, const Consensus::Params &consensusParams, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    {
        {
            WRITELOCK(cs_mapBlockIndex);
            pindex->nStatus |= BLOCK_FAILED_VALID;
        }
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);

        // Lock block validation threads to make sure no new inbound block announcements
        // cause any block validation state to change while we're unwinding the chain.
        LOCK(PV->cs_blockvalidationthread);

        while (chainActive.Contains(pindex))
        {
            CBlockIndex *pindexWalk = chainActive.Tip();
            {
                WRITELOCK(cs_mapBlockIndex);
                pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
            }
            setDirtyBlockIndex.insert(pindexWalk);
            setBlockIndexCandidates.erase(pindexWalk);
            // ActivateBestChain considers blocks already in chainActive
            // unconditionally valid already, so force disconnect away from it.
            if (!DisconnectTip(state, consensusParams))
            {
                return false;
            }
        }
    }

    LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
        GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    {
        READLOCK(cs_mapBlockIndex);
        BlockMap::iterator it = mapBlockIndex.begin();
        while (it != mapBlockIndex.end())
        {
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx &&
                !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip()))
            {
                setBlockIndexCandidates.insert(it->second);
            }
            it++;
        }
    }

    InvalidChainFound(pindex);
    // Now mark every block index on every chain that contains pindex as child of invalid
    MarkAllContainingChainsInvalid(pindex);

    // Since this block has been invalidated and removed from setBlockIndexCandidates
    // then reset the best header to the current most work chain.
    CBlockIndex *mostWork = FindMostWorkChain();
    if (mostWork)
    {
        pindexBestHeader = mostWork;
    }

    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev, false);
    return true;
}

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before the last checkpoint)
    if (IsInitialBlockDownload())
        return;

    // If our best fork is no longer within 72 blocks (+/- 12 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = nullptr;

    if (pindexBestForkTip)
    {
        if (!fLargeWorkForkFound && pindexBestForkBase)
        {
            std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                                  pindexBestForkBase->phashBlock->ToString() + std::string("'");
            AlertNotify(warning);
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            LOGA("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height "
                 "%d (%s).\nChain state database corruption likely.\n",
                __func__, pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
            fLargeWorkForkFound = true;
        }
    }
    else
    {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void InvalidChainFound(CBlockIndex *pindexNew)
{
    CBlockIndex *pBestInvalid = pindexBestInvalid.load();
    if (!pBestInvalid || pindexNew->nChainWork > pBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LOGA("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__, pindexNew->GetBlockHash().ToString(),
        pindexNew->nHeight, std::log(pindexNew->nChainWork.getdouble()) / std::log(2.0),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert(tip);
    LOGA("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__, tip->GetBlockHash().ToString(),
        chainActive.Height(), std::log(tip->nChainWork.getdouble()) / std::log(2.0),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions();
}

bool ContextualCheckBlock(const CBlock &block, CValidationState &state, CBlockIndex *const pindexPrev)
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params &consensusParams = Params().GetConsensus();

    // Start enforcing BIP113 (Median Time Past)
    int nLockTimeFlags = 0;
    if (nHeight >= consensusParams.BIP68Height)
    {
        nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    int64_t nLockTimeCutoff;
    if (pindexPrev == nullptr)
        nLockTimeCutoff = block.GetBlockTime();
    else
        nLockTimeCutoff =
            (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST) ? pindexPrev->GetMedianTimePast() : block.GetBlockTime();

    // Check that all transactions are finalized and count the number of
    // transactions to check for excessive transaction limits.
    uint64_t nTx = 0;
    uint64_t nLargestTx = 0;
    for (const auto &tx : block.vtx)
    {
        if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
        {
            return state.DoS(
                10, error("%s: contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
        }
        if (!ContextualCheckTransaction(tx, state, pindexPrev, Params()))
            return false;

        nTx++;
        if (tx->GetTxSize() > nLargestTx)
            nLargestTx = tx->GetTxSize();
    }

    // Enforce block nVersion=2 rule that the coinbase starts with serialized block height
    if (nHeight >= consensusParams.BIP34Height)
    {
        // For legacy reasons keep the original way of checking BIP34 compliance
        CScript expect = CScript() << nHeight;
        if (block.vtx[0]->vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0]->vin[0].scriptSig.begin()))
        {
            // However the original way only checks a specific serialized int encoding, BUT BIP34 does not mandate
            // the most efficient encoding, only that it be a "serialized CScript", and then gives an example with
            // 3 byte encoding.  Therefore we've ended up with miners that only generate 3 byte encodings...
            int blockCoinbaseHeight = block.GetHeight();
            if (blockCoinbaseHeight == nHeight)
            {
                LOG(BLK, "Mined block valid but suboptimal height format, different client interpretions of "
                         "BIP34 may cause fork");
            }
            else
            {
                uint256 hashp = block.hashPrevBlock;
                uint256 hash = block.GetHash();
                return state.DoS(100, error("%s: block height mismatch in coinbase, expected %d, got %d, block is %s, "
                                            "parent block is %s, pprev is %s",
                                          __func__, nHeight, blockCoinbaseHeight, hash.ToString(), hashp.ToString(),
                                          pindexPrev->phashBlock->ToString()),
                    REJECT_INVALID, "bad-cb-height");
            }
        }
    }

    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev == nullptr ? 1 : pindexPrev->nHeight + 1;

    // Check whether this block exceeds what we want to relay.
    block.fExcessive = CheckExcessive(block, block.GetBlockSize(), nTx, nLargestTx);
    return true;
}

bool CheckBlock(const CBlock &block, CValidationState &state, bool fCheckPOW, bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot)
    {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(
                100, error("CheckBlock(): hashMerkleRoot mismatch"), REJECT_INVALID, "bad-txnmrklroot", true);

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(
                100, error("CheckBlock(): duplicate transaction"), REJECT_INVALID, "bad-txns-duplicate", true);
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    if (block.vtx.empty())
        return state.DoS(100, error("CheckBlock(): size limits failed"), REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase())
        return state.DoS(100, error("CheckBlock(): first tx is not coinbase"), REJECT_INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS(100, error("CheckBlock(): more than one coinbase"), REJECT_INVALID, "bad-cb-multiple");

    // Check transactions
    for (const auto &tx : block.vtx)
        if (!CheckTransaction(tx, state))
            return error("CheckBlock(): CheckTransaction of %s failed with %s", tx->GetHash().ToString(),
                FormatStateMessage(state));


    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;
    return true;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindexNew,
    const CDiskBlockPos &pos)
{
    AssertLockHeld(cs_main); // for setBlockIndexCandidates
    WRITELOCK(cs_mapBlockIndex); // for nStatus and nSequenceId

    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;

    if (block.fExcessive)
    {
        pindexNew->nStatus |= BLOCK_EXCESSIVE;
    }

    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == nullptr || pindexNew->pprev->nChainTx)
    {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex *> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty())
        {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            pindex->nSequenceId = ++nBlockSequenceId;
            if (chainActive.Tip() == nullptr || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip()))
            {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
                range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second)
            {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    }
    else
    {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE))
        {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

/** Store block on disk. If dbp is non-nullptr, the file is known to already reside on disk */
bool AcceptBlock(const CBlock &block,
    CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex **ppindex,
    bool fRequested,
    CDiskBlockPos *dbp)
{
    AssertLockHeld(cs_main);

    CBlockIndex *&pindex = *ppindex;

    if (!AcceptBlockHeader(block, state, chainparams, &pindex))
    {
        return false;
    }

    LOG(PARALLEL, "Check Block %s with chain work %s block height %d\n", pindex->phashBlock->ToString(),
        pindex->nChainWork.ToString(), pindex->nHeight);

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = false;
    {
        READLOCK(cs_mapBlockIndex);
        fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    }
    bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave)
    {
        return true;
    }
    // If we didn't ask for it:
    if (!fRequested)
    {
        if (pindex->nTx != 0)
            return true; // This is a previously-processed block that was pruned
        if (!fHasMoreWork)
            return true; // Don't process less-work chains
        if (fTooFarAhead)
            return true; // Block height is too high
    }

    {
        if ((!CheckBlock(block, state)) || !ContextualCheckBlock(block, state, pindex->pprev))
        {
            if (state.IsInvalid() && !state.CorruptionPossible())
            {
                {
                    WRITELOCK(cs_mapBlockIndex);
                    pindex->nStatus |= BLOCK_FAILED_VALID;
                    setDirtyBlockIndex.insert(pindex);
                }
                // Now mark every block index on every chain that contains pindex as child of invalid
                MarkAllContainingChainsInvalid(pindex);
            }
            return false;
        }
    }
    int nHeight = pindex->nHeight;
    // Write block to history file
    try
    {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != nullptr)
        {
            blockPos = *dbp;
        }
        if (!FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != nullptr))
        {
            return error("AcceptBlock(): FindBlockPos failed");
        }
        if (dbp == nullptr)
        {
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            {
                AbortNode(state, "Failed to write block");
            }
        }
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
        {
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
        }
    }
    catch (const std::runtime_error &e)
    {
        return AbortNode(state, std::string("System error: ") + e.what());
    }
    if (fCheckForPruning)
    {
        FlushStateToDisk(state, FLUSH_STATE_NONE); // we just allocated more disk space for block files
    }
    return true;
}

uint32_t GetBlockScriptFlags(const CBlockIndex *pindex, const Consensus::Params &consensusparams)
{
    AssertLockHeld(cs_main);

    uint32_t flags = SCRIPT_VERIFY_NONE;

    // Start enforcing P2SH (Bip16)
    if (pindex->nHeight >= consensusparams.BIP16Height)
    {
        flags |= SCRIPT_VERIFY_P2SH;
    }

    // Start enforcing the DERSIG (BIP66) rule
    if (pindex->nHeight >= consensusparams.BIP66Height)
    {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    if (pindex->nHeight >= consensusparams.BIP65Height)
    {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY).
    if (pindex->nHeight >= consensusparams.BIP68Height)
    {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // Start enforcing the UAHF fork
    if (UAHFforkActivated(pindex->nHeight))
    {
        flags |= SCRIPT_VERIFY_STRICTENC;
        flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
    }

    // If the DAA HF is enabled, we start rejecting transaction that use a high
    // s in their signature. We also make sure that signature that are supposed
    // to fail (for instance in multisig or other forms of smart contracts) are
    // null.
    if (IsDAAEnabled(consensusparams, pindex->pprev))
    {
        flags |= SCRIPT_VERIFY_LOW_S;
        flags |= SCRIPT_VERIFY_NULLFAIL;
    }

    // Since Nov 15, 2018 HF activates sig push only, clean stack rules
    // are enforced and CHECKDATASIG has been introduced on the BCH chain
    // (see  BIP 62 and CHECKDATASIG specification or more details)
    if (IsNov2018Activated(consensusparams, chainActive.Tip()))
    {
        flags |= SCRIPT_VERIFY_SIGPUSHONLY;
        flags |= SCRIPT_VERIFY_CLEANSTACK;
        flags |= SCRIPT_ENABLE_CHECKDATASIG;
    }

    // This will check if current blocki is the first boock of the fork,
    // hence we add SCRIPT_ENABLE_SCHNORR_MULTISIG to the set of supported flags.
    if (IsNov2019Activated(consensusparams, pindex->pprev))
    {
        flags |= SCRIPT_ENABLE_SCHNORR_MULTISIG;
        flags |= SCRIPT_VERIFY_MINIMALDATA;
    }

    if (IsMay2020Activated(consensusparams, pindex->pprev))
    {
        flags |= SCRIPT_ENABLE_OP_REVERSEBYTES;
    }

    return flags;
}

/**
 * Restore the UTXO in a Coin at a given COutPoint
 * @param undo The Coin to be restored.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return A DisconnectResult as an int
 */
int ApplyTxInUndo(Coin &&undo, CCoinsViewCache &view, const COutPoint &out)
{
    bool fClean = true;

    if (view.HaveCoin(out))
    {
        LOG(BLK, "Apply Undo: Unclean disconnect of (%s, %d)\n", out.hash.ToString(), out.n);
        fClean = false; // overwriting transaction output
    }

    if (undo.nHeight == 0)
    {
        // Missing undo metadata (height and coinbase). Older versions included this
        // information only in undo records for the last spend of a transactions'
        // outputs. This implies that it must be present for some other output of the same tx.
        CoinAccessor alternate(view, out.hash);
        if (alternate->IsSpent())
        {
            LOG(BLK, "Apply Undo: Coin (%s, %d) is spent\n", out.hash.ToString(), out.n);
            return DISCONNECT_FAILED; // adding output for transaction without known metadata
        }
        undo.nHeight = alternate->nHeight;
        undo.fCoinBase = alternate->fCoinBase;
    }
    // The potential_overwrite parameter to AddCoin is only allowed to be false if we know for
    // sure that the coin did not already exist in the cache. As we have queried for that above
    // using HaveCoin, we don't need to guess. When fClean is false, a coin already existed and
    // it is an overwrite.
    view.AddCoin(out, std::move(undo), !fClean);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When UNCLEAN or FAILED is returned, view is left in an indeterminate state. */
DisconnectResult DisconnectBlock(const CBlock &block, const CBlockIndex *pindex, CCoinsViewCache &view)
{
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    // blockdb mode does not use the file pos system
    if (pos.IsNull() && BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        error("DisconnectBlock(): no undo data available");
        return DISCONNECT_FAILED;
    }
    if (!ReadUndoFromDisk(blockUndo, pos, pindex->pprev))
    {
        error("DisconnectBlock(): failure reading undo data");
        return DISCONNECT_FAILED;
    }
    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
    {
        error("DisconnectBlock(): block and undo data inconsistent");
        return DISCONNECT_FAILED;
    }
    // undo transactions in reverse of the OTI algorithm order (so add inputs first, then remove outputs)
    // we can use this algorithm for both dtor and ctor because we are undoing a validated block so
    // we already know that the block is valid.

    // restore inputs
    for (unsigned int i = 1; i < block.vtx.size(); i++) // i=1 to skip the coinbase, it has no inputs
    {
        const CTransaction &tx = *(block.vtx[i]);
        CTxUndo &txundo = blockUndo.vtxundo[i - 1];
        if (txundo.vprevout.size() != tx.vin.size())
        {
            error("DisconnectBlock(): transaction and undo data inconsistent");
            return DISCONNECT_FAILED;
        }
        for (unsigned int j = tx.vin.size(); j-- > 0;)
        {
            const COutPoint &out = tx.vin[j].prevout;
            int res = ApplyTxInUndo(std::move(txundo.vprevout[j]), view, out);
            if (res == DISCONNECT_FAILED)
            {
                error("DisconnectBlock(): ApplyTxInUndo failed");
                return DISCONNECT_FAILED;
            }
            fClean = fClean && res != DISCONNECT_UNCLEAN;
        }
        // At this point, all of txundo.vprevout should have been moved out.
    }

    // remove outputs
    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = *(block.vtx[i]);
        uint256 hash = tx.GetHash();

        // Check that all outputs are available and match the outputs in the block itself exactly.
        for (size_t o = 0; o < tx.vout.size(); o++)
        {
            if (!tx.vout[o].scriptPubKey.IsUnspendable())
            {
                COutPoint out(hash, o);
                Coin coin;
                view.SpendCoin(out, &coin);
                if (tx.vout[o] != coin.out)
                {
                    error("DisconnectBlock(): transaction output mismatch");
                    fClean = false; // transaction output mismatch
                }
            }
        }
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}


bool ConnectBlockPrevalidations(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const CChainParams &chainparams,
    bool fJustCheck)
{
    int64_t nTimeStart = GetStopwatchMicros();

    // If we're not in IBD then check it again in case a previous version let a bad block in
    //
    // During IBD setBlocksAlreadyChecked is used rather than the internal block flag block.fChecked because
    // blocks are typcially stored 1000 blocks ahead and get writting to disk but fChecked is not stored
    // to disk, so when the block is finally read again for processing it ends up going through CheckBlock() again
    // unnecessarily. So by storing the hash of the block during IBD just after the first successful CheckBlock()
    // we can avoid doing CheckBlock() again later in IBD and which saves us a significant amount of time.
    //
    // In addition, if we had just restarted the node and continued IBD, started a Reindex, or found a new block
    // then setBlockAlreadyChecked would be empty and we'd have to run CheckBlock() at least one time, which is
    // correct behavior.
    {
        bool fAlreadyChecked = true;
        {
            LOCK(cs_BlocksAlreadyChecked);
            if (!setBlocksAlreadyChecked.count(block.GetHash()))
                fAlreadyChecked = false;
        }
        if (!fAlreadyChecked)
        {
            if (!CheckBlock(block, state, !fJustCheck, !fJustCheck))
            {
                return false;
            }
        }
        else
        {
            LOCK(cs_BlocksAlreadyChecked);
            setBlocksAlreadyChecked.erase(block.GetHash());
        }
    }

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == nullptr ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    int64_t nTime1 = GetStopwatchMicros();
    nTimeCheck += nTime1 - nTimeStart;
    LOG(BENCH, "    - Sanity checks: %.2fms [%.2fs]\n", 0.001 * (nTime1 - nTimeStart), nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely.
    // This rule was originally applied to all blocks with a timestamp after March 15, 2012, 0:00 UTC.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    bool fEnforceBIP30 = (!pindex->phashBlock) || // Enforce on CreateNewBlock invocations which don't have a hash.
                         !((pindex->nHeight == 91842 &&
                               pindex->GetBlockHash() ==
                                   uint256S("0x00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec")) ||
                             (pindex->nHeight == 91880 &&
                                 pindex->GetBlockHash() ==
                                     uint256S("0x00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721")));

    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create
    // further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for
    // the BIP30 check.
    if (pindex->pprev) // If this isn't the genesis block
    {
        CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus().BIP34Height);
        // Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't
        // correspond.
        fEnforceBIP30 =
            fEnforceBIP30 &&
            (!pindexBIP34height || !(pindexBIP34height->GetBlockHash() == chainparams.GetConsensus().BIP34Hash));

        if (fEnforceBIP30)
        {
            for (const auto &tx : block.vtx)
            {
                for (size_t o = 0; o < tx->vout.size(); o++)
                {
                    if (view.HaveCoin(COutPoint(tx->GetHash(), o)))
                    {
                        return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"), REJECT_INVALID,
                            "bad-txns-BIP30");
                    }
                }
            }
        }
    }

    int64_t nTime2 = GetStopwatchMicros();
    nTimeForks += nTime2 - nTime1;
    LOG(BENCH, "    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    return true;
}

/** Ensure that the script validation threads have finished running and locks are set correctly
 *  before exiting ConnectBlock.
 */
static void ConnectBlockScopeExit(bool fParallel,
    CCheckQueueControl<CScriptCheck> &control,
    CCheckQueue<CScriptCheck> *pScriptQueue)
{
    // Typically the script validations would be stopped by issuing a PV->Quit() however under
    // certain conditions block validation may return early from some error or if the chain tip has changed
    // during block validation. So here we make sure to stop the script validation threads. This prevents a
    // long to validate block from continuing to use resources when it is in fact not even a valid block.
    //
    // In the typical case we will end up issuing the Quit() twice. This is fine because all were doing
    // is setting a boolean flag.
    pScriptQueue->Quit();

    // As a final check, make sure all the script validation threads have stopped. Sometimes
    // if a PV thread is terminated early or a block is found to be invalid for some reason
    // then we'll end up returning without getting to the control.Wait() at the end of this scope.
    // While in single threaded operation this is not an issue but when PV is in sue we MUST wait
    // for all threads to terminate before continuing otherwise we can end up trying to access
    // data which has already been destroyed in the main thread.
    control.Wait();

    // Make sure locks have set locks correctly on leaving this scope.
    PV->SetLocks(fParallel);
}

bool ConnectBlockDependencyOrdering(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const CChainParams &chainparams,
    bool fJustCheck,
    bool fParallel,
    bool fScriptChecks,
    CAmount &nFees,
    CBlockUndo &blockundo,
    std::vector<std::pair<uint256, CDiskTxPos> > &vPos)
{
    nFees = 0;
    int64_t nTime2 = GetStopwatchMicros();
    LOG(BLK, "Dependency ordering for %s MTP: %d\n", block.GetHash().ToString(), pindex->GetMedianTimePast());

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY)
    int nLockTimeFlags = 0;
    if (pindex->nHeight >= chainparams.GetConsensus().BIP68Height)
    {
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    // Get the script flags for this block
    uint32_t flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus());

    ValidationResourceTracker resourceTracker;
    std::vector<int> prevheights;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    int nChecked = 0;
    int nUnVerifiedChecked = 0;
    const arith_uint256 nStartingChainWork = chainActive.Tip()->nChainWork;

    // Section for boost scoped lock on the scriptcheck_mutex
    boost::thread::id this_id(boost::this_thread::get_id());

    // Initialize a PV session.
    if (!PV->Initialize(this_id, pindex, fParallel))
        return false;

    /*********************************************************************************************
     If in PV, unlock cs_main here so we have no contention when we're checking inputs and scripts
     *********************************************************************************************/
    if (fParallel)
        LEAVE_CRITICAL_SECTION(cs_main);

    // Get the next available mutex and the associated scriptcheckqueue. Then lock this thread
    // with the mutex so that the checking of inputs can be done with the chosen scriptcheckqueue.
    CCheckQueue<CScriptCheck> *pScriptQueue(PV->GetScriptCheckQueue());

    // Aquire the control that is used to wait for the script threads to finish. Do this after aquiring the
    // scoped lock to ensure the scriptqueue is free and available.
    CCheckQueueControl<CScriptCheck> control(fScriptChecks && PV->ThreadCount() ? pScriptQueue : nullptr);

    // Begin Section for Boost Scope Guard
    {
        // Scope guard to make sure cs_main is set and resources released if we encounter an exception.
        BOOST_SCOPE_EXIT(&fParallel, &control, &pScriptQueue)
        {
            ConnectBlockScopeExit(fParallel, control, pScriptQueue);
        }
        BOOST_SCOPE_EXIT_END


        // Start checking Inputs
        // When in parallel mode then unlock cs_main for this loop to give any other threads
        // a chance to process in parallel. This is crucial for parallel validation to work.
        // NOTE: the only place where cs_main is needed is if we hit PV->ChainWorkHasChanged, which
        //       internally grabs the cs_main lock when needed.
        for (unsigned int i = 0; i < block.vtx.size(); i++)
        {
            const CTransaction &tx = *(block.vtx[i]);
            const CTransactionRef &txref = block.vtx[i];

            nInputs += tx.vin.size();

            // Get total sigop count for both legacy and p2sh sigops
            nSigOps += GetTransactionSigOpCount(txref, view, flags);
            if (nSigOps > GetMaxBlockSigOpsCount(block.GetBlockSize()))
                return state.DoS(100, error("ConnectBlock(): too many sigops"), REJECT_INVALID, "bad-blk-sigops");

            if (!tx.IsCoinBase())
            {
                // Check that transaction is BIP68 final
                // BIP68 lock checks (as opposed to nLockTime checks) must
                // be in ConnectBlock because they require the UTXO set
                prevheights.resize(tx.vin.size());
                {
                    bool abort = false;
                    for (size_t j = 0; j < tx.vin.size(); j++)
                    {
                        CoinAccessor coin(view, tx.vin[j].prevout);
                        // isSpend is true for empty coin object (coinEmpty)
                        if (coin->IsSpent())
                        {
                            abort = true;
                            break;
                        }
                        prevheights[j] = coin->nHeight;
                        nFees = nFees + coin->out.nValue;
                    }
                    if (abort)
                    {
                        // If we were validating at the same time as another block and the other block wins the
                        // validation race and updates the UTXO first, then we may end up here with missing inputs.
                        // Therefore we check to see if the chainwork has advanced or if we recieved a quit and if
                        // so return without DOSing the node.
                        if (PV->ChainWorkHasChanged(nStartingChainWork) || PV->QuitReceived(this_id, fParallel))
                        {
                            return false;
                        }
                        return state.DoS(100, error("%s: block %s inputs missing/spent in tx %d %s", __func__,
                                                  block.GetHash().ToString(), i, tx.GetHash().ToString()),
                            REJECT_INVALID, "bad-txns-inputs-missingorspent");
                    }
                }
                nFees = nFees - tx.GetValueOut();

                if (!SequenceLocks(txref, nLockTimeFlags, &prevheights, *pindex))
                {
                    return state.DoS(100, error("%s: block %s contains a non-BIP68-final transaction", __func__,
                                              block.GetHash().ToString()),
                        REJECT_INVALID, "bad-txns-nonfinal");
                }

                uint256 hash = tx.GetHash();
                {
                    // If XVal is not on then check all inputs, otherwise only check
                    // transactions that were not previously verified in the mempool.
                    bool fUnVerified = block.setUnVerifiedTxns.count(hash);
                    if (fUnVerified || !block.fXVal)
                    {
                        if (fUnVerified)
                            nUnVerifiedChecked++;

                        std::vector<CScriptCheck> vChecks;
                        bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks
                                                            (still consult the cache, though) */
                        if (!CheckInputs(txref, state, view, fScriptChecks, flags, maxScriptOps.Value(), fCacheResults,
                                &resourceTracker, PV->ThreadCount() ? &vChecks : nullptr))
                        {
                            return error("%s: block %s CheckInputs on %s failed with %s", __func__,
                                block.GetHash().ToString(), tx.GetHash().ToString(), FormatStateMessage(state));
                        }
                        control.Add(vChecks);
                        nChecked++;
                    }
                }
            }

            CTxUndo undoDummy;
            if (i > 0)
            {
                blockundo.vtxundo.push_back(CTxUndo());
            }
            UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);
            vPos.push_back(std::make_pair(tx.GetHash(), pos));
            pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);

            if (PV->QuitReceived(this_id, fParallel))
            {
                return false;
            }

            // This is for testing PV and slowing down the validation of inputs. This makes it easier to create
            // and run python regression tests and is an testing feature.
            if (GetArg("-pvtest", false))
                MilliSleep(1000);
        }
        LOG(BENCH, "Number of CheckInputs() performed: %d  Unverified count: %d\n", nChecked, nUnVerifiedChecked);

        // Wait for all sig check threads to finish before updating utxo
        LOG(PARALLEL, "Waiting for script threads to finish\n");
        if (!control.Wait())
        {
            // if we end up here then the signature verification failed and we must re-lock cs_main before returning.
            return state.DoS(100, false, REJECT_INVALID, "bad-blk-signatures", false, "parallel script check failed");
        }

        if (PV->QuitReceived(this_id, fParallel))
        {
            return false;
        }
    }

    int64_t nTime3 = GetStopwatchMicros();
    nTimeConnect += nTime3 - nTime2;
    LOG(BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(),
        0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(),
        nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs - 1), nTimeConnect * 0.000001);

    int64_t nTime4 = GetStopwatchMicros();
    nTimeVerify += nTime4 - nTime2;
    LOG(BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime4 - nTime2),
        nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs - 1), nTimeVerify * 0.000001);

    return true;
}


bool ConnectBlockCanonicalOrdering(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const CChainParams &chainparams,
    bool fJustCheck,
    bool fParallel,
    bool fScriptChecks,
    CAmount &nFees,
    CBlockUndo &blockundo,
    std::vector<std::pair<uint256, CDiskTxPos> > &vPos)
{
    // Enabled returns true if the fork is enabled on the NEXT block, so we pass this block's parent
    bool may2020Active = (pindex) ? IsMay2020Activated(chainparams.GetConsensus(), pindex) : false;
    nFees = 0;
    int64_t nTime2 = GetStopwatchMicros();
    LOG(BLK, "Canonical ordering for %s MTP: %d\n", block.GetHash().ToString(), pindex->GetMedianTimePast());

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY)
    int nLockTimeFlags = 0;
    if (pindex->nHeight >= chainparams.GetConsensus().BIP68Height)
    {
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    // Get the script flags for this block
    uint32_t flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus());

    std::vector<ValidationResourceTracker> txResourceTracker;
    std::vector<int> prevheights;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    int nChecked = 0;
    int nUnVerifiedChecked = 0;
    const arith_uint256 nStartingChainWork = chainActive.Tip()->nChainWork;

    // Section for boost scoped lock on the scriptcheck_mutex
    boost::thread::id this_id(boost::this_thread::get_id());

    // Get the next available mutex and the associated scriptcheckqueue. Then lock this thread
    // with the mutex so that the checking of inputs can be done with the chosen scriptcheckqueue.
    CCheckQueue<CScriptCheck> *pScriptQueue(PV->GetScriptCheckQueue());

    // Aquire the control that is used to wait for the script threads to finish. Do this after aquiring the
    // scoped lock to ensure the scriptqueue is free and available.
    CCheckQueueControl<CScriptCheck> control(fScriptChecks && PV->ThreadCount() ? pScriptQueue : nullptr);

    // Initialize a PV session.
    if (!PV->Initialize(this_id, pindex, fParallel))
        return false;

    /*********************************************************************************************
     If in PV, unlock cs_main here so we have no contention when we're checking inputs and scripts
     *********************************************************************************************/
    if (fParallel)
        LEAVE_CRITICAL_SECTION(cs_main);

    // Begin Section for Boost Scope Guard
    {
        // Scope guard to make sure cs_main is set and resources released if we encounter an exception.
        BOOST_SCOPE_EXIT(&fParallel, &control, &pScriptQueue)
        {
            ConnectBlockScopeExit(fParallel, control, pScriptQueue);
        }
        BOOST_SCOPE_EXIT_END

        txResourceTracker.resize(block.vtx.size());

        // Outputs then Inputs algorithm: add outputs to the coin cache
        // and validate lexical ordering
        uint256 prevTxHash;
        for (unsigned int i = 0; i < block.vtx.size(); i++)
        {
            const CTransaction &tx = *(block.vtx[i]);
            try
            {
                AddCoins(view, tx, pindex->nHeight);
            }
            catch (std::logic_error &e)
            {
                return state.DoS(100,
                    error("%s: block %s repeated-tx %s", __func__, block.GetHash().ToString(), tx.GetHash().ToString()),
                    REJECT_INVALID, "repeated-txn");
            }

            if (i == 1)
            {
                prevTxHash = tx.GetHash();
            }
            else if (i != 0)
            {
                uint256 curTxHash = tx.GetHash();
                if (curTxHash < prevTxHash)
                {
                    return state.DoS(100,
                        error("%s: block %s lexical misordering tx %d (%s < %s)", __func__, block.GetHash().ToString(),
                                         i, curTxHash.ToString(), prevTxHash.ToString()),
                        REJECT_INVALID, "bad-txn-order");
                }
                prevTxHash = curTxHash;
            }
        }

        // Start checking Inputs
        // When in parallel mode then unlock cs_main for this loop to give any other threads
        // a chance to process in parallel. This is crucial for parallel validation to work.
        // NOTE: the only place where cs_main is needed is if we hit PV->ChainWorkHasChanged, which
        //       internally grabs the cs_main lock when needed.
        for (unsigned int i = 0; i < block.vtx.size(); i++)
        {
            const CTransaction &tx = *(block.vtx[i]);
            const CTransactionRef &txref = block.vtx[i];

            nInputs += tx.vin.size();

            if (!may2020Active)
            {
                // Get total sigop count for both legacy and p2sh sigops
                nSigOps += GetTransactionSigOpCount(txref, view, flags);
                if (nSigOps > GetMaxBlockSigOpsCount(block.GetBlockSize()))
                    return state.DoS(100, error("ConnectBlock(): too many sigops"), REJECT_INVALID, "bad-blk-sigops");
            }

            if (!tx.IsCoinBase())
            {
                // Check that transaction is BIP68 final
                // BIP68 lock checks (as opposed to nLockTime checks) must
                // be in ConnectBlock because they require the UTXO set
                prevheights.resize(tx.vin.size());
                {
                    bool abort = false;
                    for (size_t j = 0; j < tx.vin.size(); j++)
                    {
                        CoinAccessor coin(view, tx.vin[j].prevout);
                        // isSpend is true for empty coin object (coinEmpty)
                        if (coin->IsSpent())
                        {
                            abort = true;
                            break;
                        }
                        prevheights[j] = coin->nHeight;
                        nFees = nFees + coin->out.nValue;
                    }
                    if (abort)
                    {
                        // If we were validating at the same time as another block and the other block wins the
                        // validation race and updates the UTXO first, then we may end up here with missing inputs.
                        // Therefore we check to see if the chainwork has advanced or if we recieved a quit and if
                        // so return without DOSing the node.
                        if (PV->ChainWorkHasChanged(nStartingChainWork) || PV->QuitReceived(this_id, fParallel))
                        {
                            return false;
                        }
                        return state.DoS(100, error("%s: block %s inputs missing/spent in tx %d %s", __func__,
                                                  block.GetHash().ToString(), i, tx.GetHash().ToString()),
                            REJECT_INVALID, "bad-txns-inputs-missingorspent");
                    }
                }
                nFees = nFees - tx.GetValueOut();

                if (!SequenceLocks(txref, nLockTimeFlags, &prevheights, *pindex))
                {
                    return state.DoS(100, error("%s: block %s contains a non-BIP68-final transaction", __func__,
                                              block.GetHash().ToString()),
                        REJECT_INVALID, "bad-txns-nonfinal");
                }

                uint256 hash = tx.GetHash();
                {
                    // If XVal is not on then check all inputs, otherwise only check
                    // transactions that were not previously verified in the mempool.
                    bool fUnVerified = block.setUnVerifiedTxns.count(hash);
                    if (fUnVerified || !block.fXVal)
                    {
                        if (fUnVerified)
                            nUnVerifiedChecked++;

                        std::vector<CScriptCheck> vChecks;
                        bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks
                                                            (still consult the cache, though) */
                        if (!CheckInputs(txref, state, view, fScriptChecks, flags, maxScriptOps.Value(), fCacheResults,
                                &txResourceTracker[i], PV->ThreadCount() ? &vChecks : nullptr))
                        {
                            return error("%s: block %s CheckInputs on %s failed with %s", __func__,
                                block.GetHash().ToString(), tx.GetHash().ToString(), FormatStateMessage(state));
                        }
                        control.Add(vChecks);
                        nChecked++;
                    }
                }
            }

            CTxUndo undoDummy;
            if (i > 0)
            {
                blockundo.vtxundo.push_back(CTxUndo());
            }

            SpendCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

            vPos.push_back(std::make_pair(tx.GetHash(), pos));
            pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);

            if (PV->QuitReceived(this_id, fParallel))
            {
                return false;
            }

            // This is for testing PV and slowing down the validation of inputs. This makes it easier to create
            // and run python regression tests and is an testing feature.
            if (GetArg("-pvtest", false))
                MilliSleep(1000);
        }
        LOG(BENCH, "Number of CheckInputs() performed: %d  Unverified count: %d\n", nChecked, nUnVerifiedChecked);

        // Wait for all sig check threads to finish before updating utxo
        LOG(PARALLEL, "Waiting for script threads to finish\n");
        if (!control.Wait())
        {
            // if we end up here then the signature verification failed and we must re-lock cs_main before returning.
            return state.DoS(100, false, REJECT_INVALID, "bad-blk-signatures", false, "parallel script check failed");
        }

        if (may2020Active)
        {
            uint64_t blockSigChecks = 0;
            for (const auto &t : txResourceTracker) // its ok to add the coinbase sigchecks because they must be 0
            {
                auto txSigChecks = t.GetConsensusSigChecks();
                blockSigChecks += txSigChecks;
                LOG(BENCH, "Tx SigChecks performed: %d\n", txSigChecks);
                // May2020 transaction consensus rule
                if (txSigChecks > MAY2020_MAX_TX_SIGCHECK_COUNT)
                {
                    return state.DoS(100, false, REJECT_INVALID, "bad-tx-sigchecks", false,
                        "per transaction sigcheck limit exceeded");
                }
            }

            LOG(BENCH, "Number of SigChecks performed: %d\n", blockSigChecks);

            // May 2020 block consensus rule
            uint64_t maxSigChecksAllowed = maxSigChecks.Value();
            if (blockSigChecks > maxSigChecksAllowed)
            {
                return state.DoS(
                    100, false, REJECT_INVALID, "bad-blk-sigchecks", false, "block sigcheck limit exceeded");
            }
        }

        if (PV->QuitReceived(this_id, fParallel))
        {
            return false;
        }
    }

    int64_t nTime3 = GetStopwatchMicros();
    nTimeConnect += nTime3 - nTime2;
    LOG(BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(),
        0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(),
        nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs - 1), nTimeConnect * 0.000001);

    int64_t nTime4 = GetStopwatchMicros();
    nTimeVerify += nTime4 - nTime2;
    LOG(BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime4 - nTime2),
        nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs - 1), nTimeVerify * 0.000001);

    return true;
}


bool ConnectBlock(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const CChainParams &chainparams,
    bool fJustCheck,
    bool fParallel)
{
    // pindex should be the header structure for this new block.  Check this by making sure that the nonces are the
    // same.
    assert(pindex->nNonce == block.nNonce);

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == chainparams.GetConsensus().hashGenesisBlock)
    {
        if (!fJustCheck)
        {
            view.SetBestBlock(pindex->GetBlockHash());
        }
        return true;
    }

    /** BU: Start Section to validate inputs - if there are parallel blocks being checked
     *      then the winner of this race will get to update the UTXO.
     */
    AssertLockHeld(cs_main);
    // Section for boost scoped lock on the scriptcheck_mutex
    boost::thread::id this_id(boost::this_thread::get_id());

    if (!ConnectBlockPrevalidations(block, state, pindex, view, chainparams, fJustCheck))
        return false;

    const arith_uint256 nStartingChainWork = chainActive.Tip()->nChainWork;

    const int64_t timeBarrier = GetTime() - (24 * 3600 * checkScriptDays.Value());
    // Blocks that have various days of POW behind them makes them secure in that
    // real online nodes have checked the scripts.  Therefore, during initial block
    // download we don't need to check most of those scripts except for the most
    // recent ones.
    bool fScriptChecks = true;
    CBlockIndex *pBestHeader = pindexBestHeader.load();
    if (pBestHeader)
    {
        if (fReindex || fImporting)
            fScriptChecks = !fCheckpointsEnabled || block.nTime > timeBarrier;
        else
            fScriptChecks = !fCheckpointsEnabled || block.nTime > timeBarrier ||
                            (uint32_t)pindex->nHeight > pBestHeader->nHeight - (144 * checkScriptDays.Value());
    }

    CAmount nFees = 0;
    CBlockUndo blockundo;
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());

    // Discover how to handle this block
    bool canonical = fCanonicalTxsOrder;
    // Always allow overwite of fCanonicalTxsOrder but for regtest on BCH
    if (IsNov2018Activated(chainparams.GetConsensus(), chainActive.Tip()))
    {
        if (!(chainparams.NetworkIDString() == "regtest"))
        {
            canonical = true;
        }
    }
    else
    {
        if (!(chainparams.NetworkIDString() == "regtest"))
        {
            canonical = false;
        }
    }

    if (canonical)
    {
        if (!ConnectBlockCanonicalOrdering(
                block, state, pindex, view, chainparams, fJustCheck, fParallel, fScriptChecks, nFees, blockundo, vPos))
            return false;
    }
    else
    {
        if (!ConnectBlockDependencyOrdering(
                block, state, pindex, view, chainparams, fJustCheck, fParallel, fScriptChecks, nFees, blockundo, vPos))
            return false;
    }

    CAmount blockReward = nFees + GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    if (block.vtx[0]->GetValueOut() > blockReward)
        return state.DoS(100, error("ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",
                                  block.vtx[0]->GetValueOut(), blockReward),
            REJECT_INVALID, "bad-cb-amount");

    if (fJustCheck)
        return true;

    int64_t nTime4 = GetStopwatchMicros();

    /*****************************************************************************************************************
     *                         Start update of UTXO, if this block wins the validation race *
     *****************************************************************************************************************/
    // If in PV mode and we win the race then we lock everyone out by taking cs_main but before updating the UTXO
    // and
    // terminating any competing threads.

    // Last check for chain work just in case the thread manages to get here before being terminated.
    if (PV->ChainWorkHasChanged(nStartingChainWork) || PV->QuitReceived(this_id, fParallel))
    {
        return false; // no need to lock cs_main before returning as it should already be locked.
    }

    // Quit any competing threads may be validating which have the same previous block before updating the UTXO.
    PV->QuitCompetingThreads(block.GetBlockHeader().hashPrevBlock);

    // Write undo information to disk
    {
        if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
        {
            if (pindex->GetUndoPos().IsNull())
            {
                CDiskBlockPos _pos;
                if (!FindUndoPos(
                        state, pindex->nFile, _pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                    return error("ConnectBlock(): FindUndoPos failed");

                if (!WriteUndoToDisk(blockundo, _pos, pindex->pprev, chainparams.MessageStart()))
                    return AbortNode(state, "Failed to write undo data");

                // update nUndoPos in block index
                //
                // We must take the cs_mapBlockIndex after FindUndoPos() in order to maintain
                // the proper locking order of cs_main -> cs_LastBlockFile -> cs_mapBlockIndex
                WRITELOCK(cs_mapBlockIndex);
                pindex->nUndoPos = _pos.nPos;
                pindex->nStatus |= BLOCK_HAVE_UNDO;
            }

            WRITELOCK(cs_mapBlockIndex);
            pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
            setDirtyBlockIndex.insert(pindex);
        }
    }

    // Write transaction data to the txindex
    if (IsTxIndexReady())
    {
        g_txindex->BlockConnected(block, pindex);
    }

    // add this block to the view's block chain (the main UTXO in memory cache)
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetStopwatchMicros();
    nTimeIndex += nTime5 - nTime4;
    LOG(BENCH, "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0]->GetHash();

    int64_t nTime6 = GetStopwatchMicros();
    nTimeCallbacks += nTime6 - nTime5;
    LOG(BENCH, "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);

    PV->Cleanup(block, pindex); // NOTE: this must be run whether in fParallel or not!

    // Track all recent txns in a block so we don't re-request them again. This can happen a txn announcement
    // arrives just after the block is received.
    for (const CTransactionRef &ptx : block.vtx)
    {
        txRecentlyInBlock.insert(ptx->GetHash());
    }

    return true;
}

void InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state)
{
    AssertLockHeld(cs_main);
    int nDoS = 0;
    if (state.IsInvalid(nDoS))
    {
        assert(state.GetRejectCode() < REJECT_INTERNAL); // Blocks are never rejected with internal reject codes

        std::map<uint256, NodeId>::iterator it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end())
        {
            CNodeRef node(connmgr->FindNodeFromId(it->second));

            if (node)
            {
                node->PushMessage(NetMsgType::REJECT, (std::string)NetMsgType::BLOCK,
                    (unsigned char)state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH),
                    pindex->GetBlockHash());
                if (nDoS > 0)
                    dosMan.Misbehaving(node.get(), nDoS);
            }
        }
    }
    if (!state.CorruptionPossible())
    {
        {
            WRITELOCK(cs_mapBlockIndex);
            pindex->nStatus |= BLOCK_FAILED_VALID;
        }
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);

        // Now mark every block index on every chain that contains pindex as child of invalid
        MarkAllContainingChainsInvalid(pindex);

        // Since this block has been invalidated and removed from setBlockIndexCandidates
        // then reset the best header to the current most work chain.
        CBlockIndex *mostWork = FindMostWorkChain();
        if (mostWork)
        {
            pindexBestHeader = mostWork;
        }
    }
}

// bip135 begin
/** Check for conspicuous versionbit signal events in last 100 blocks and alert. */
void CheckAndAlertUnknownVersionbits(const CChainParams &chainParams, const CBlockIndex *chainTip)
{
    static bool fWarned = false;
    int nUpgraded = 0;
    bool upgradedEval = false;
    const CBlockIndex *pindex = chainTip;
    int32_t anUnexpectedVersion = 0;

    // start unexpected version / new fork signal checks only after BIT_WARNING_WINDOW block height
    if (pindex->nHeight >= BIT_WARNING_WINDOW)
    {
        for (int bit = 0; bit < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; bit++)
        {
            if (!IsConfiguredDeployment(chainParams.GetConsensus(), bit))
            {
                const CBlockIndex *iindex = pindex; // iterating index, reset to chain tip
                // set count for this bit to 0
                unknownFork[bit].UnknownForkSignalStrength = 0;
                for (int i = 0; i < BIT_WARNING_WINDOW && iindex != nullptr; i++)
                {
                    unknownFork[bit].UnknownForkSignalStrength += ((iindex->nVersion >> bit) & 0x1);
                    if (!upgradedEval)
                    {
                        // do the old "unexpected block version" counting only during first bit walk
                        int32_t nExpectedVersion =
                            UnlimitedComputeBlockVersion(pindex->pprev, chainParams.GetConsensus(), pindex->nTime);

                        if (iindex->nVersion > VERSIONBITS_LAST_OLD_BLOCK_VERSION &&
                            (iindex->nVersion & ~nExpectedVersion) != 0)
                        {
                            anUnexpectedVersion = iindex->nVersion;
                            ++nUpgraded;
                        }
                    }
                    iindex = iindex->pprev;
                }
                upgradedEval = true; // only do the unexpected version checks once during bit loop
                if (unknownFork[bit].UnknownForkSignalFirstDetected && !unknownFork[bit].UnknownForkSignalLost &&
                    unknownFork[bit].UnknownForkSignalStrength == 0)
                {
                    // report a lost signal
                    LOGA("%s: signal lost for unknown fork (versionbit %i)\n", __func__, bit);
                    unknownFork[bit].UnknownForkSignalFirstDetected = true;
                    unknownFork[bit].UnknownForkSignalLost = true; // set it so that we don't report on it again
                }
                // report newly gained / regained signal
                else if ((!unknownFork[bit].UnknownForkSignalFirstDetected || unknownFork[bit].UnknownForkSignalLost) &&
                         unknownFork[bit].UnknownForkSignalStrength > 0)
                {
                    // report a newly detected signal
                    LOGA("%s: new signal detected for unknown fork (versionbit %i) - strength %d/%d\n", __func__, bit,
                        unknownFork[bit].UnknownForkSignalStrength, BIT_WARNING_WINDOW);
                    // set it so that we don't report on it again
                    unknownFork[bit].UnknownForkSignalFirstDetected = true;
                    unknownFork[bit].UnknownForkSignalLost = false;
                }
                else if (unknownFork[bit].UnknownForkSignalStrength >= 95 &&
                         !unknownFork[bit].UnknownForkSignalAt95Percent)
                {
                    LOGA("%s: signal for unknown fork (versionbit %i) >= 95%% - strength %d/%d\n", __func__, bit,
                        unknownFork[bit].UnknownForkSignalStrength, BIT_WARNING_WINDOW);
                    unknownFork[bit].UnknownForkSignalAt95Percent = true;
                }
                else if (unknownFork[bit].UnknownForkSignalStrength >= 90 &&
                         !unknownFork[bit].UnknownForkSignalAt90Percent)
                {
                    LOGA("%s: signal for unknown fork (versionbit %i) >= 90%% - strength %d/%d\n", __func__, bit,
                        unknownFork[bit].UnknownForkSignalStrength, BIT_WARNING_WINDOW);
                    unknownFork[bit].UnknownForkSignalAt90Percent = true;
                    unknownFork[bit].UnknownForkSignalAt95Percent = false;
                }
                else if (unknownFork[bit].UnknownForkSignalStrength >= 70 &&
                         !unknownFork[bit].UnknownForkSignalAt70Percent)
                {
                    LOGA("%s: signal for unknown fork (versionbit %i) >= 70%% - strength %d/%d\n", __func__, bit,
                        unknownFork[bit].UnknownForkSignalStrength, BIT_WARNING_WINDOW);
                    unknownFork[bit].UnknownForkSignalAt70Percent = true;
                    unknownFork[bit].UnknownForkSignalAt90Percent = false;
                    unknownFork[bit].UnknownForkSignalAt95Percent = false;
                }
                else if (unknownFork[bit].UnknownForkSignalStrength >= 50 &&
                         !unknownFork[bit].UnknownForkSignalAt50Percent)
                {
                    LOGA("%s: signal for unknown fork (versionbit %i) >= 50%% - strength %d/%d\n", __func__, bit,
                        unknownFork[bit].UnknownForkSignalStrength, BIT_WARNING_WINDOW);
                    unknownFork[bit].UnknownForkSignalAt50Percent = true;
                    unknownFork[bit].UnknownForkSignalAt70Percent = false;
                    unknownFork[bit].UnknownForkSignalAt90Percent = false;
                    unknownFork[bit].UnknownForkSignalAt95Percent = false;
                }
                else if (unknownFork[bit].UnknownForkSignalStrength >= 25 &&
                         !unknownFork[bit].UnknownForkSignalAt25Percent)
                {
                    LOGA("%s: signal for unknown fork (versionbit %i) >= 25%% - strength %d/%d\n", __func__, bit,
                        unknownFork[bit].UnknownForkSignalStrength, BIT_WARNING_WINDOW);
                    unknownFork[bit].UnknownForkSignalAt25Percent = true;
                    unknownFork[bit].UnknownForkSignalAt50Percent = false;
                    unknownFork[bit].UnknownForkSignalAt70Percent = false;
                    unknownFork[bit].UnknownForkSignalAt90Percent = false;
                    unknownFork[bit].UnknownForkSignalAt95Percent = false;
                    fWarned = false; // turn off to repeat the warning when > 50% again
                }
            }
        }
    }

    if (nUpgraded > 0)
        LOGA("%s: %d of last 100 blocks have unexpected version. One example: 0x%x\n", __func__, nUpgraded,
            anUnexpectedVersion);
    if (nUpgraded > BIT_WARNING_WINDOW / 2)
    {
        // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
        strMiscWarning = _("Warning: Unknown block versions being mined! It's possible unknown rules are in effect");

        // Temporararily disable the warning message. It is not very useful and frequently gives false positive
        // warnings causing end users to think there is a serious problem when there is none.
        // TODO: find a mor permanent solution or workround.
        strMiscWarning = _("");

        if (!fWarned)
        {
            AlertNotify(strMiscWarning);
            fWarned = true;
        }
    }
    else
    {
        strMiscWarning = _("");
        // Clear message when conditions have returned to normal
        if (fWarned)
        {
            AlertNotify(strMiscWarning);
            fWarned = false;
        }
    }
}
// bip135 end

/** Update chainActive and related internal data structures. */
void UpdateTip(CBlockIndex *pindexNew)
{
    DbgAssert(txProcessingCorral.region() == CORRAL_TX_PAUSE, LOGA("Updating tip during tx processing"));
    const CChainParams &chainParams = Params();
    chainActive.SetTip(pindexNew);

    // If the chain tip has changed previously rejected transactions
    // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
    // or a double-spend. Reset the rejects filter and give those
    // txs a second chance.
    recentRejects.reset();

    // New best block
    nTimeBestReceived.store(GetTime());
    mempool.AddTransactionsUpdated(1);

    cvBlockChange.notify_all();

    LOGA("%s: new best=%s  height=%d bits=%d log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%utxo)\n",
        __func__, chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), chainActive.Tip()->nBits,
        log(chainActive.Tip()->nChainWork.getdouble()) / log(2.0), (unsigned long)chainActive.Tip()->nChainTx,
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip()),
        pcoinsTip->DynamicMemoryUsage() * (1.0 / (1 << 20)), pcoinsTip->GetCacheSize());

    if (!IsInitialBlockDownload())
    {
        // Check the version of the last 100 blocks,
        // alert if significant signaling changes.
        CheckAndAlertUnknownVersionbits(chainParams, chainActive.Tip());
    }

    // Set the global variables based on the fork state of the NEXT block
    // Always allow overwite of fCanonicalTxsOrder but for regtest)
    if (IsNov2018Activated(chainParams.GetConsensus(), chainActive.Tip()))
    {
        if (chainParams.NetworkIDString() != "regtest")
        {
            fCanonicalTxsOrder = true;
        }
    }
}

static void ResubmitTransactions(CBlock *block = nullptr)
{
    // To be very safe let's force everything in the mempool to be re-admitted.  This reduces this rare case
    // quickly to a very common operation mode.  If we do not do this, we must guarantee that all tx coming from
    // the block get injected into the mempool to ensure that any mempool tx that relies on an input from this
    // block doesn't get orphaned but remain in the mempool.
    // The performance of doing it this way is surprisingly ok, even though it seems like more work,
    // because the readmission code is multithreaded and very efficient, yet the code to slip a tx back into a
    // dependency chain in the mempool performed terribly.
    //
    // We must hold the mempool lock throughout otherwise it would be possible for some txns to slip back into
    // the mempool from the csCommitQFinal.
    WRITELOCK(mempool.cs_txmempool);
    // If tx admission is not paused, the commitment thread will be modifying what this function is clearing
    DbgAssert(txProcessingCorral.region() != CORRAL_TX_COMMITMENT, LOGA("Resubmit transactions during tx commitment"));
    LOG(MEMPOOL, "Clearing mempool and resubmitting transactions");
    {
        if (block)
        {
            // Resubmit the block first
            for (const auto &ptx : block->vtx)
            {
                if (!ptx->IsCoinBase())
                {
                    CTxInputData txd;
                    txd.tx = ptx;
                    txd.nodeName = "rollback";
                    EnqueueTxForAdmission(txd);
                }
            }
        }

        // Resubmit and clear the mempool
        mempool._forEachThenClear([](const auto &entry) {
            CTxInputData txd;
            txd.tx = entry.GetSharedTx();
            txd.nodeName = "rollback";
            EnqueueTxForAdmission(txd);
        });

        // Resumbit and clear all txns currently in the txCommitQ and txCommitQFinal
        mempool.ResubmitCommitQ();
    }
}
/** Disconnect chainActive's tip. */
bool DisconnectTip(CValidationState &state, const Consensus::Params &consensusParams, const bool fRollBack)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(PV->cs_blockvalidationthread);

    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete, consensusParams))
        return AbortNode(state, "DisconnectTip(): Failed to read block");
    // Apply the block atomically to the chain state.
    int64_t nStart = GetStopwatchMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (DisconnectBlock(block, pindexDelete, view) != DISCONNECT_OK)
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        bool result = view.Flush();
        assert(result);
    }
    LOG(BENCH, "- Disconnect block: %.2fms\n", (GetStopwatchMicros() - nStart) * 0.001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;

    // these bloom filters stop us from doing duplicate work on tx we already know about.
    // but since we rewound, we need to do this duplicate work -- clear them so tx we have already processed
    // can be processed again.
    txRecentlyInBlock.reset();
    recentRejects.reset();

    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    for (const auto &ptx : block.vtx)
    {
        SyncWithWallets(ptx, nullptr, -1);
    }

    // Clear mempool if rolling back the chain using the "rollbackchain" rpc command, otherwise clear and
    // place all tx back into the admission queue. "Rollbackchain" is used for significant manually triggered
    // reorganizations, such as switching between forks, so it makes no sense to keep the transactions because
    // they will likely be invalid or already confirmed on the other fork.
    if (fRollBack)
    {
        WRITELOCK(mempool.cs_txmempool);
        mempool._clear();
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        txCommitQ->clear();
    }
    else
    {
        ResubmitTransactions(&block);
    }

    return true;
}


/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool ConnectTip(CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex *pindexNew,
    const CBlock *pblock,
    bool fParallel)
{
    AssertLockHeld(cs_main);

    // During IBD if there are many blocks to connect still it could be a while before shutting down
    // and the user may think the shutdown has hung, so return here and stop connecting any remaining
    // blocks.
    if (ShutdownRequested())
        return false;

    // With PV there is a special case where one chain may be in the process of connecting several blocks but then
    // a second chain also begins to connect blocks and its block beat the first chains block to advance the tip.
    // As a result pindexNew->prev on the first chain will no longer match the chaintip as the second chain continues
    // connecting blocks. Therefore we must return "false" rather than "assert" as was previously the case.
    // assert(pindexNew->pprev == chainActive.Tip());
    if (pindexNew->pprev != chainActive.Tip())
        return false;

    // Read block from disk.
    int64_t nTime1 = GetStopwatchMicros();
    CBlock block;
    if (!pblock)
    {
        if (!ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus()))
            return AbortNode(state, "ConnectTip(): Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetStopwatchMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LOG(BENCH, "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(pcoinsTip);
        bool rv = ConnectBlock(*pblock, state, pindexNew, view, chainparams, false, fParallel);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv)
        {
            if (state.IsInvalid())
            {
                InvalidBlockFound(pindexNew, state);
                return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
            }
            return false;
        }
        int64_t nStart = GetStopwatchMicros();
        bool result = view.Flush();
        nBlockSizeAtChainTip.store(pblock->GetBlockSize());
        assert(result);
        LOG(BENCH, "      - Update Coins %.3fms\n", GetStopwatchMicros() - nStart);

        mapBlockSource.erase(pindexNew->GetBlockHash());
        nTime3 = GetStopwatchMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LOG(BENCH, "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);

        // Search orphan queue for anything that is no longer an orphan due to tx in this block
        // or any tx that has a parent in the mempool, since commited tx may now make that tx available
        // for mempool admission based on a reduction of mempool ancestors.
        std::vector<uint256> vWhatChanged;
        mempool.queryHashes(vWhatChanged);
        vWhatChanged.reserve(vWhatChanged.size() + pblock->vtx.size());
        for (unsigned int j = 0; j < pblock->vtx.size(); j++)
        {
            vWhatChanged.push_back(pblock->vtx[j]->GetHash());
        }
        ProcessOrphans(vWhatChanged);
    }

    int64_t nTime4 = GetStopwatchMicros();
    nTimeFlush += nTime4 - nTime3;
    LOG(BENCH, "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary, and only during IBD, reindex, or importing.
    if (!IsChainNearlySyncd() || fReindex || fImporting)
        if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
            return false;
    int64_t nTime5 = GetStopwatchMicros();
    nTimeChainState += nTime5 - nTime4;
    LOG(BENCH, "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);

    // Remove transactions from the mempool, both those confirmed in the block and conflicting transactions.
    //
    // If we are still in initial block download then skip this step and just clear the mempool. There should
    // be no transactions in the mempool during initial sync, and also there is no need then to parse through each
    // blocks transactions in removeForBlock() looking for transactions to remove.
    std::list<CTransactionRef> txConflicted;
    std::vector<CTxChange> txChanges;
    if (!IsInitialBlockDownload() && !fReindex)
    {
        // txChanges: only if some unconfirmed tx push is turned on, track what transactions may need to be pushed while
        // confirmed transactions are removed from the mempool.
        mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted, !IsInitialBlockDownload(),
            (unconfPushAction.Value() == 0) ? nullptr : &txChanges);
    }
    else
    {
        mempool.clear();
    }
    // Update chainActive & related variables.
    UpdateTip(pindexNew);
    // Tell wallet about transactions that went from mempool
    // to conflicted:
    for (const auto &ptx : txConflicted)
    {
        SyncWithWallets(ptx, nullptr, -1);
    }
    // ... and about transactions that got confirmed:
    int txIdx = 0;
    for (const auto &ptx : pblock->vtx)
    {
        SyncWithWallets(ptx, pblock, txIdx);
        txIdx++;
    }

    int64_t nTime6 = GetStopwatchMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LOG(BENCH, "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LOG(BENCH, "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);

    // If some kind of unconfirmed push is turned on, then do the forwarding.
    if (!IsInitialBlockDownload() && !fReindex && unconfPushAction.Value() != 0)
        ForwardAcceptableTransactions(txChanges);

    return true;
}

static void CheckForkWarningConditionsOnNewFork(CBlockIndex *pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex *pfork = pindexNewForkTip;
    CBlockIndex *plonger = chainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 12 hours if no one mines it) of ours
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork &&
        (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
        pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
        chainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either nullptr or a pointer to a CBlock corresponding to pindexMostWork.
 */
bool ActivateBestChainStep(CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex *pindexMostWork,
    const CBlock *pblock,
    bool fParallel)
{
    if (!pindexMostWork)
        return false;

    AssertLockHeld(cs_main);
    bool fInvalidFound = false;
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);
    CBlockIndex *pindexNewMostWork;

    bool fBlocksDisconnected = false;
    boost::thread::id this_id(boost::this_thread::get_id()); // get this thread's id

    while (chainActive.Tip() && chainActive.Tip() != pindexFork)
    {
        // Indicate that this thread has now initiated a re-org
        LOCK(PV->cs_blockvalidationthread);
        if (!fBlocksDisconnected)
            PV->MarkReorgInProgress(this_id, true, fParallel);

        // When running in parallel block validation mode it is possible that this competing block could get to this
        // point just after the chaintip had already been advanced.  If that were to happen then it could initiate a
        // re-org when in fact a Quit had already been called on this thread.  So we do a check if Quit was previously
        // called and return if true.
        if (PV->QuitReceived(this_id, fParallel))
        {
            PV->MarkReorgInProgress(this_id, false, fParallel);
            return false;
        }

        // Disconnect active blocks which are no longer in the best chain. We do not need to concern ourselves with any
        // block validation threads that may be running for the chain we are rolling back. They will automatically fail
        // validation during ConnectBlock() once the chaintip has changed..
        if (!DisconnectTip(state, chainparams.GetConsensus()))
        {
            PV->MarkReorgInProgress(this_id, false, fParallel);
            return false;
        }

        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex *> vpindexToConnect;
    bool fContinue = true;
    /** Parallel Validation: fBlock determines whether we pass a block or nullptr to ConnectTip().
     *  If the pindexMostWork has been extended while we have been validating the last block then we
     *  want to pass a nullptr so that the next block is read from disk, because we will definitely not
     *  have the block.
     */
    bool fBlock = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight < pindexMostWork->nHeight)
    {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + (int)requester.BLOCK_DOWNLOAD_WINDOW.load(), pindexMostWork->nHeight);
        vpindexToConnect.clear();
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight)
        {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        CBlockIndex *pindexNewTip = nullptr;
        CBlockIndex *pindexLastNotify = nullptr;
        for (auto i = vpindexToConnect.rbegin(); i != vpindexToConnect.rend(); i++)
        {
            // Start with a clear state in case we're connecting another block after
            // trying to connect an invalid on a different chain.
            state = CValidationState();

            CBlockIndex *pindexConnect = *i;
            // Check if the best chain has changed while we were disconnecting or processing blocks.
            // If so then we need to return and continue processing the newer chain.
            pindexNewMostWork = FindMostWorkChain();
            if (!pindexMostWork || !pindexNewMostWork)
                return false;

            if (pindexNewMostWork->nChainWork > pindexMostWork->nChainWork)
            {
                LOG(PARALLEL, "Returning because chain work has changed while connecting blocks\n");
                return true;
            }
            if (!ConnectTip(state, chainparams, pindexConnect,
                    pindexConnect == pindexMostWork && fBlock ? pblock : nullptr, fParallel))
            {
                if (state.IsInvalid())
                {
                    LOGA("Invalid block due to %s\n", state.GetRejectReason().c_str());

                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                }
                else
                {
                    // A system error occurred (disk space, database error, ...) or a Parallel Validation was
                    // terminated.
                    return false;
                }
            }
            else
            {
                pindexNewTip = pindexConnect;

                // Update the syncd status after each block is handled
                IsChainNearlySyncdInit();
                IsInitialBlockDownloadInit();

                if (!IsInitialBlockDownload())
                {
                    // Notify external zmq listeners about the new tip.
                    GetMainSignals().UpdatedBlockTip(pindexConnect);
                }

                // Update the UI at least every 5 seconds just in case we get in a long loop
                // as can happen during IBD.  We need an atomic here because there may be other
                // threads running concurrently.
                static std::atomic<int64_t> nLastUpdate = {GetTime()};
                if (nLastUpdate.load() < GetTime() - 5)
                {
                    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindexNewTip, false);
                    pindexLastNotify = pindexNewTip;
                    nLastUpdate.store(GetTime());
                }

                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork)
                {
                    /* BU: these are commented out for parallel validation:
                           We must always continue so as to find if the pindexMostWork has advanced while we've
                           been trying to connect the last block.
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                    */
                }
            }

            // If we're shutting down then don't connect any more blocks.
            if (shutdown_threads.load())
                return false;
        }

        // Notify the UI with the new block tip information.
        if (pindexMostWork->nHeight >= nHeight && pindexNewTip != nullptr && pindexLastNotify != pindexNewTip)
            uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindexNewTip, false);

        if (fContinue)
        {
            pindexMostWork = FindMostWorkChain();
            if (!pindexMostWork)
                return false;
        }
        fBlock = false; // read next blocks from disk

        // Update the syncd status after each block is handled
        IsChainNearlySyncdInit();
        IsInitialBlockDownloadInit();
    }


    // Relay Inventory
    CBlockIndex *pindexNewTip = chainActive.Tip();
    if (pindexFork != pindexNewTip)
    {
        if (!IsInitialBlockDownload())
        {
            // Find the hashes of all blocks that weren't previously in the best chain.
            std::vector<uint256> vHashes;
            CBlockIndex *pindexToAnnounce = pindexNewTip;
            while (pindexToAnnounce != pindexFork)
            {
                vHashes.push_back(pindexToAnnounce->GetBlockHash());
                pindexToAnnounce = pindexToAnnounce->pprev;
                if (vHashes.size() == MAX_BLOCKS_TO_ANNOUNCE)
                {
                    // Limit announcements in case of a huge reorganization.
                    // Rely on the peer's synchronization mechanism in that case.
                    break;
                }
            }
            // Relay inventory, but don't relay old inventory during initial block download.
            int nBlockEstimate = 0;
            if (fCheckpointsEnabled)
                nBlockEstimate = Checkpoints::GetTotalBlocksEstimate(chainparams.Checkpoints());
            {
                LOCK(cs_vNodes);
                for (CNode *pnode : vNodes)
                {
                    if (chainActive.Height() >
                        (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                    {
                        for (auto i = vHashes.rbegin(); i != vHashes.rend(); i++)
                        {
                            const uint256 &hash = *i;
                            pnode->PushBlockHash(hash);
                        }
                    }
                }
            }
        }
    }

    if (fBlocksDisconnected)
    {
        LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
            GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
    }
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
    {
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
        return false;
    }
    else
        CheckForkWarningConditions();

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either nullptr or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState &state,
    const CChainParams &chainparams,
    const CBlock *pblock,
    bool fParallel,
    CNode *pfrom)
{
    bool result = true;
    CBlockIndex *pindexMostWork = nullptr;

    TxAdmissionPause txlock;
    LOCK(cs_main);

    bool fOneDone = false;
    do
    {
        if (shutdown_threads.load() == true)
        {
            return false;
        }
        if (ShutdownRequested())
        {
            return false;
        }

        CBlockIndex *pindexOldTip = chainActive.Tip();
        pindexMostWork = FindMostWorkChain();
        if (!pindexMostWork)
        {
            return true;
        }


        // This is needed for PV because FindMostWorkChain does not necessarily return the block with the lowest
        // nSequenceId
        if (fParallel && pblock)
        {
            READLOCK(cs_mapBlockIndex);
            std::set<CBlockIndex *, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            while (it != setBlockIndexCandidates.rend())
            {
                if ((*it)->nChainWork == pindexMostWork->nChainWork)
                    if ((*it)->nSequenceId < pindexMostWork->nSequenceId)
                        pindexMostWork = *it;
                it++;
            }
        }

        // Whether we have anything to do at all.
        if (chainActive.Tip() != nullptr)
        {
            if (pindexMostWork->nChainWork <= chainActive.Tip()->nChainWork)
                return true;
        }

        //** PARALLEL BLOCK VALIDATION
        // Find the CBlockIndex of this block if this blocks previous hash matches the old chaintip.  In the
        // case of parallel block validation we may have two or more blocks processing at the same time however
        // their block headers may not represent what is considered the best block as returned by pindexMostWork.
        // Therefore we must supply the blockindex of this block explicitly as being the one with potentially
        // the most work and which will subsequently advance the chain tip if it wins the validation race.
        if (pblock != nullptr && pindexOldTip != nullptr && chainActive.Tip() != chainActive.Genesis() && fParallel)
        {
            if (pblock->GetBlockHeader().hashPrevBlock == *pindexOldTip->phashBlock)
            {
                pindexMostWork = LookupBlockIndex(pblock->GetHash());
                if (!pindexMostWork)
                {
                    LOG(BLK, "Could not find block in mapBlockIndex: %s\n", pblock->GetHash().ToString());
                    return false;
                }

                // Because we are potentially working with a block that is not the pindexMostWork as returned by
                // FindMostWorkChain() but rather are forcing it to point to this block we must check again if
                // this block has enough work to advance the tip.
                if (pindexMostWork->nChainWork <= pindexOldTip->nChainWork)
                {
                    return false;
                }
            }
        }

        // If there is a reorg happening then we can not activate this chain *unless* it
        // has more work that the currently processing reorg chain.  In that case we must terminate the reorg
        // extend this chain instead.
        if (!fOneDone && PV && PV->IsReorgInProgress())
        {
            // find out if this block and chain are more work than the chain
            // being reorg'd to.  If not then just return.  If so then kill the reorg and
            // start connecting this chain.
            if (pindexMostWork->nChainWork > PV->MaxWorkChainBeingProcessed())
            {
                // kill all validating threads except our own.
                boost::thread::id this_id(boost::this_thread::get_id());
                PV->StopAllValidationThreads(this_id);
            }
            else
                return true;
        }

        if (!ActivateBestChainStep(state, chainparams, pindexMostWork,
                ((pblock) && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : nullptr), fParallel))
        {
            // If we fail to activate a chain because it is bad, send a reject message
            // but keep iterating to reactivate the best known chain.
            int nDoS = 0;
            if (state.IsInvalid(nDoS))
            {
                LOGA("Chain activation failed, returning to next best choice\n");
                result = false;

                if (pfrom)
                {
                    pfrom->PushMessage(NetMsgType::REJECT, (std::string)NetMsgType::BLOCK, state.GetRejectCode(),
                        state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), pblock->GetHash());
                    if (nDoS > 0)
                        dosMan.Misbehaving(pfrom, nDoS);
                }
            }
            else
            {
                return false;
            }
        }
        else
        {
            // We may have tried to connect an invalid chain prior to connecting a valid one so we need
            // to reset the result to true if we had previously set it to false.
            result = true;
        }

        // Check if the best chain has changed while we were processing blocks.  If so then we need to
        // continue processing the newer chain.  This satisfies a rare edge case where we have initiated
        // a reorg to another chain but before the reorg is complete we end up reorging to a different
        // chain. Set pblock to nullptr here to make sure as we continue we get blocks from disk.
        pindexMostWork = FindMostWorkChain();
        if (!pindexMostWork)
            return false;
        pblock = nullptr;
        fOneDone = true;
    } while (pindexMostWork->nChainWork > chainActive.Tip()->nChainWork);
    CheckBlockIndex(chainparams.GetConsensus());

    return result;
}

bool ProcessNewBlock(CValidationState &state,
    const CChainParams &chainparams,
    CNode *pfrom,
    const CBlock *pblock,
    bool fForceProcessing,
    CDiskBlockPos *dbp,
    bool fParallel)
{
    int64_t start = GetStopwatchMicros();
    LOG(THIN, "Processing new block %s from peer %s.\n", pblock->GetHash().ToString(),
        pfrom ? pfrom->GetLogName() : "myself");
    // Preliminary checks
    if (!CheckBlockHeader(*pblock, state, true))
    { // block header is bad
        // demerit the sender
        return error("%s: CheckBlockHeader FAILED", __func__);
    }
    if (IsChainNearlySyncd() && !fImporting && !fReindex && connmgr->ExpeditedBlockNodes().size())
        SendExpeditedBlock(*pblock, pfrom);

    bool checked = CheckBlock(*pblock, state);
    if (!checked)
    {
        LOGA("Invalid block: ver:%x time:%d Tx size:%d len:%d\n", pblock->nVersion, pblock->nTime, pblock->vtx.size(),
            pblock->GetBlockSize());
    }
    else if (IsInitialBlockDownload())
    {
        LOCK(cs_BlocksAlreadyChecked);
        setBlocksAlreadyChecked.insert(pblock->GetHash());
        if (setBlocksAlreadyChecked.size() > MAX_SETBLOCKSALREADYCHECKED_SIZE)
            setBlocksAlreadyChecked.erase(setBlocksAlreadyChecked.begin());
    }

    // WARNING: cs_main is not locked here throughout but is released and then re-locked during ActivateBestChain
    //          If you lock cs_main throughout ProcessNewBlock then you will in effect prevent PV from happening.
    //          TODO: in order to lock cs_main all the way through we must remove the locking from ActivateBestChain
    //                but it will require great care because ActivateBestChain requires cs_main however it is also
    //                called from other places.  Currently it seems best to leave cs_main here as is.
    {
        LOCK(cs_main);
        uint256 hash = pblock->GetHash();
        bool fRequested = requester.MarkBlockAsReceived(hash, pfrom);
        fRequested |= fForceProcessing;
        if (!checked)
        {
            return error("%s: CheckBlock FAILED", __func__);
        }

        // Store to disk
        CBlockIndex *pindex = nullptr;
        bool ret = AcceptBlock(*pblock, state, chainparams, &pindex, fRequested, dbp);
        if (pindex && pfrom)
        {
            mapBlockSource[pindex->GetBlockHash()] = pfrom->GetId();
        }
        CheckBlockIndex(chainparams.GetConsensus());

        CInv inv(MSG_BLOCK, hash);
        if (!ret)
        {
            // BU TODO: if block comes out of order (before its parent) this will happen.  We should cache the block
            // until the parents arrive.

            // If the block was not accepted then reset the fProcessing flag to false.
            requester.BlockRejected(inv, pfrom);

            return error("%s: AcceptBlock FAILED", __func__);
        }
        else
        {
            // We must indicate to the request manager that the block was received only after it has
            // been stored to disk (or been shown to be invalid). Doing so prevents unnecessary re-requests.
            requester.Received(inv, pfrom);
        }
    }
    if (!ActivateBestChain(state, chainparams, pblock, fParallel))
    {
        if (state.IsInvalid() || state.IsError())
            return error("%s: ActivateBestChain failed", __func__);
        else
            return false;
    }

    int64_t end = GetStopwatchMicros();
    if (Logging::LogAcceptCategory(BENCH))
    {
        uint64_t maxTxSizeLocal = 0;
        uint64_t maxVin = 0;
        uint64_t maxVout = 0;
        CTransaction txIn;
        CTransaction txOut;
        CTransaction txLen;

        for (unsigned int i = 0; i < pblock->vtx.size(); i++)
        {
            if (pblock->vtx[i]->vin.size() > maxVin)
            {
                maxVin = pblock->vtx[i]->vin.size();
                txIn = *pblock->vtx[i];
            }
            if (pblock->vtx[i]->vout.size() > maxVout)
            {
                maxVout = pblock->vtx[i]->vout.size();
                txOut = *pblock->vtx[i];
            }
            uint64_t len = ::GetSerializeSize(pblock->vtx[i], SER_NETWORK, PROTOCOL_VERSION);
            if (len > maxTxSizeLocal)
            {
                maxTxSizeLocal = len;
                txLen = *pblock->vtx[i];
            }
        }

        LOG(BENCH,
            "ProcessNewBlock, time: %d, block: %s, len: %d, numTx: %d, maxVin: %llu, maxVout: %llu, maxTx:%llu\n",
            end - start, pblock->GetHash().ToString(), pblock->GetBlockSize(), pblock->vtx.size(), maxVin, maxVout,
            maxTxSizeLocal);
        LOG(BENCH, "tx: %s, vin: %llu, vout: %llu, len: %d\n", txIn.GetHash().ToString(), txIn.vin.size(),
            txIn.vout.size(), ::GetSerializeSize(txIn, SER_NETWORK, PROTOCOL_VERSION));
        LOG(BENCH, "tx: %s, vin: %llu, vout: %llu, len: %d\n", txOut.GetHash().ToString(), txOut.vin.size(),
            txOut.vout.size(), ::GetSerializeSize(txOut, SER_NETWORK, PROTOCOL_VERSION));
        LOG(BENCH, "tx: %s, vin: %llu, vout: %llu, len: %d\n", txLen.GetHash().ToString(), txLen.vin.size(),
            txLen.vout.size(), ::GetSerializeSize(txLen, SER_NETWORK, PROTOCOL_VERSION));
    }

    LOCK(cs_blockvalidationtime);
    nBlockValidationTime << (end - start);
    return true;
}

bool IsBlockPruned(const CBlockIndex *pblockindex)
{
    READLOCK(cs_mapBlockIndex); // for nStatus
    return (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0);
}
