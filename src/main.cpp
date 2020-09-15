// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "addrman.h"
#include "arith_uint256.h"
#include "blockrelay/blockrelay_common.h"
#include "blockrelay/graphene.h"
#include "blockrelay/mempool_sync.h"
#include "blockrelay/thinblock.h"
#include "blockstorage/blockstorage.h"
#include "blockstorage/sequential_files.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "connmgr.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "dosman.h"
#include "expedited.h"
#include "hashwrapper.h"
#include "index/txindex.h"
#include "init.h"
#include "merkleblock.h"
#include "net.h"
#include "net_processing.h"
#include "nodestate.h"
#include "parallel.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "requestManager.h"
#include "respend/respenddetector.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "tinyformat.h"
#include "txadmission.h"
#include "txdb.h"
#include "txlookup.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validation/validation.h"
#include "validationinterface.h"
#include "versionbits.h"
#include "xversionkeys.h"
#include "xversionmessage.h"

#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/scope_exit.hpp>
#include <sstream>
#include <thread>

#if defined(NDEBUG)
#error "Bitcoin cannot be compiled without assertions."
#endif

/**
 * Global state
 */

std::atomic<bool> fImporting{false};
std::atomic<bool> fReindex{false};
bool fBlocksOnly = false;
bool fTxIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
unsigned int nBytesPerSigOp = DEFAULT_BYTES_PER_SIGOP;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
uint64_t nPruneTarget = 0;
uint64_t nDBUsedSpace = 0;
uint32_t nXthinBloomFilterSize = SMALLEST_MAX_BLOOM_FILTER_SIZE;

// BU: Move global objects to a single file
extern CTxMemPool mempool;

extern CTweak<unsigned int> blockDownloadWindow;
extern CTweak<uint64_t> reindexTypicalBlockSize;

extern std::map<CNetAddr, ConnectionHistory> mapInboundConnectionTracker;
extern CCriticalSection cs_mapInboundConnectionTracker;

extern CCriticalSection cs_LastBlockFile;

extern std::map<uint256, NodeId> mapBlockSource;
extern std::set<int> setDirtyFileInfo;
extern uint64_t nBlockSequenceId;


/** Number of nodes with fSyncStarted. */
int nSyncStarted = 0;

/** Number of preferable block download peers. */
std::atomic<int> nPreferredDownload{0};

/** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
 * Pruned nodes may have entries where B is missing data.
 */
std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;

/** Global flag to indicate we should check to see if there are
 *  block/undo files that should be deleted.  Set on startup
 *  or if we allocate more file space when we're in prune mode
 */
bool fCheckForPruning = false;

std::vector<CBlockFileInfo> vinfoBlockFile;
int nLastBlockFile = 0;

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace
{
int GetHeight() { return chainActive.Height(); }
void InitializeNode(const CNode *pnode)
{
    // Add an entry to the nodestate map
    nodestate.InitializeNodeState(pnode);

    // Add an entry to requestmanager nodestate map
    requester.InitializeNodeState(pnode->GetId());
}

void FinalizeNode(NodeId nodeid)
{
    // Clean up the sync maps
    ClearDisconnectedFromMempoolSyncMaps(nodeid);

    // Clear thintype block data if we have any.
    thinrelay.ClearAllBlocksToReconstruct(nodeid);
    thinrelay.ClearAllBlocksInFlight(nodeid);

    // Clear Graphene blocks held by sender for this receiver
    thinrelay.ClearSentGrapheneBlocks(nodeid);

    // Update block sync counters
    {
        CNodeStateAccessor state(nodestate, nodeid);
        DbgAssert(state != nullptr, return );

        if (state->fSyncStarted)
            nSyncStarted--;

        nPreferredDownload.fetch_sub(state->fPreferredDownload);
    }

    // Remove nodestate tracking
    nodestate.RemoveNodeState(nodeid);
}

} // anon namespace


bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats)
{
    CNodeRef node(connmgr->FindNodeFromId(nodeid));
    if (!node)
        return false;

    CNodeStateAccessor state(nodestate, nodeid);
    DbgAssert(state != nullptr, return false);

    stats.nMisbehavior = node->nMisbehavior.load();
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;

    std::vector<uint256> vBlocksInFlight;
    requester.GetBlocksInFlight(vBlocksInFlight, nodeid);

    READLOCK(cs_mapBlockIndex);
    for (const uint256 &hash : vBlocksInFlight)
    {
        // lookup block by hash to find height
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex *pindex = (*mi).second;
            if (pindex)
                stats.vHeightInFlight.push_back(pindex->nHeight);
        }
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals &nodeSignals)
{
    nodeSignals.GetHeight.connect(&GetHeight);
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals &nodeSignals)
{
    nodeSignals.GetHeight.disconnect(&GetHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

CBlockIndex *FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator)
{
    // Find the first block the caller has in the main chain
    READLOCK(cs_mapBlockIndex);
    for (const uint256 &hash : locator.vHave)
    {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex *pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

CCoinsViewCache *pcoinsTip = nullptr;
CBlockTreeDB *pblocktree = nullptr;
CBlockTreeDB *pblocktreeother = nullptr;

bool TestLockPointValidity(const LockPoints *lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock)
    {
        // Check whether chainActive is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock))
        {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)", state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", " + state.GetDebugMessage(), state.GetRejectCode());
}


bool AreFreeTxnsDisallowed()
{
    if (GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) > 0)
        return false;

    return true;
}

bool GetTransaction(const uint256 &hash,
    CTransactionRef &txOut,
    int64_t &txTime,
    const Consensus::Params &consensusParams,
    uint256 &hashBlock,
    bool fAllowSlow,
    const CBlockIndex *blockIndex)
{
    const CBlockIndex *pindexSlow = blockIndex;

    CTransactionRef ptx;
    {
        READLOCK(mempool.cs_txmempool);
        CTxMemPool::txiter entryPtr = mempool.mapTx.find(hash);
        if (entryPtr != mempool.mapTx.end())
        {
            txTime = entryPtr->GetTime();
            ptx = entryPtr->GetSharedTx();
        }
    }
    if (ptx)
    {
        txOut = ptx;
        return true;
    }

    if (g_txindex)
    {
        int32_t time = -1;
        if (g_txindex->FindTx(hash, hashBlock, txOut, time))
        {
            if (txTime != -1)
                txTime = time;
            return true;
        }
    }

    if (blockIndex == nullptr)
    {
        // attempt to use coin database to locate block that contains transaction, and scan it
        if (fAllowSlow)
        {
            CoinAccessor coin(*pcoinsTip, hash);
            if (!coin->IsSpent())
                pindexSlow = chainActive[coin->nHeight];
        }
    }

    if (pindexSlow)
    {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow, consensusParams))
        {
            bool ctor_enabled = pindexSlow->nHeight >= consensusParams.nov2018Height;
            int64_t pos = FindTxPosition(block, hash, ctor_enabled);
            if (pos == TX_NOT_FOUND)
            {
                return false;
            }
            txOut = block.vtx.at(pos);
            txTime = block.nTime;
            return true;
        }
    }

    return false;
}


//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;

// Execute a command, as given by -alertnotify, on certain events such as a long fork being seen
void AlertNotify(const std::string &strMessage)
{
    uiInterface.NotifyAlertChanged();
    std::string strCmd = GetArg("-alertnotify", "");
    if (strCmd.empty())
        return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote + safeStatus + singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    boost::thread t(runCommand, strCmd); // thread runs free
}

/** Abort with a message */
bool AbortNode(const std::string &strMessage, const std::string &userMessage = "")
{
    strMiscWarning = strMessage;
    LOGA("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details") : userMessage, "",
        CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage = "")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}


//
// Called periodically asynchronously; alerts if it smells like
// we're being fed a bad chain (blocks being generated much
// too slowly or too quickly).
//
void PartitionCheck(bool (*initialDownloadCheck)(),
    CCriticalSection &cs_partitionCheck,
    const CBlockIndex *const &bestHeader,
    int64_t nPowTargetSpacing)
{
    if (bestHeader == nullptr || initialDownloadCheck())
        return;

    static int64_t lastAlertTime = 0;
    int64_t now = GetAdjustedTime();
    if (lastAlertTime > now - 60 * 60 * 24)
        return; // Alert at most once per day

    const int SPAN_HOURS = 4;
    const int SPAN_SECONDS = SPAN_HOURS * 60 * 60;
    int BLOCKS_EXPECTED = SPAN_SECONDS / nPowTargetSpacing;

    boost::math::poisson_distribution<double> poisson(BLOCKS_EXPECTED);

    std::string strWarning;
    int64_t startTime = GetAdjustedTime() - SPAN_SECONDS;

    LOCK(cs_partitionCheck);
    const CBlockIndex *i = bestHeader;
    int nBlocks = 0;
    while (i->GetBlockTime() >= startTime)
    {
        ++nBlocks;
        i = i->pprev;
        if (i == nullptr)
            return; // Ran out of chain, we must not be fully sync'ed
    }

    // How likely is it to find that many by chance?
    double p = boost::math::pdf(poisson, nBlocks);

    LOG(PARTITIONCHECK, "%s: Found %d blocks in the last %d hours\n", __func__, nBlocks, SPAN_HOURS);
    LOG(PARTITIONCHECK, "%s: likelihood: %g\n", __func__, p);

    // Aim for one false-positive about every fifty years of normal running:
    const int FIFTY_YEARS = 50 * 365 * 24 * 60 * 60;
    double alertThreshold = 1.0 / (FIFTY_YEARS / SPAN_SECONDS);

    if (p <= alertThreshold && nBlocks < BLOCKS_EXPECTED)
    {
        // Many fewer blocks than expected: alert!
        strWarning = strprintf(
            _("WARNING: check your network connection, %d blocks received in the last %d hours (%d expected)"), nBlocks,
            SPAN_HOURS, BLOCKS_EXPECTED);
    }
    else if (p <= alertThreshold && nBlocks > BLOCKS_EXPECTED)
    {
        // Many more blocks than expected: alert!
        strWarning = strprintf(_("WARNING: abnormally high number of blocks generated, %d blocks received in the last "
                                 "%d hours (%d expected)"),
            nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    if (!strWarning.empty())
    {
        strMiscWarning = strWarning;
        AlertNotify(strWarning);
        lastAlertTime = now;
    }
}

// Protected by cs_main
VersionBitsCache versionbitscache;

bool CheckAgainstCheckpoint(unsigned int height, const uint256 &hash, const CChainParams &chainparams)
{
    const CCheckpointData &ckpt = chainparams.Checkpoints();
    const auto &lkup = ckpt.mapCheckpoints.find(height);
    if (lkup != ckpt.mapCheckpoints.end()) // this block height is checkpointed
    {
        if (hash != lkup->second) // This block does not match the checkpoint
        {
            return false;
        }
    }
    return true;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 100MB)
    if (Params().NetworkIDString() != "regtest" && nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

bool LoadExternalBlockFile(const CChainParams &chainparams, FILE *fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try
    {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2 * (reindexTypicalBlockSize.Value() + MESSAGE_START_SIZE + sizeof(unsigned int)),
            reindexTypicalBlockSize.Value() + MESSAGE_START_SIZE + sizeof(unsigned int), SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();

        while (!blkdat.eof())
        {
            if (shutdown_threads.load() == true)
            {
                return false;
            }

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try
            {
                // even if chainparams.MessageStart() is commonly used as network magic id
                // in this case is also used to separate blocks stored on disk on a block file.
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.MessageStart()[0]);
                // FindByte peeks 1 ahead and locates the file pointer AT the byte, not at the next one as is typical
                // for file ops.  So if we rewind, we want to go one further.
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                // BU NOTE: if we ever get to 4GB blocks the block size data structure will overflow since this is
                // defined as unsigned int (32 bits)
                blkdat >> nSize;
                if (nSize < 80) // BU allow variable block size || nSize > BU_MAX_BLOCK_SIZE)
                {
                    LOG(REINDEX, "Reindex error: Short block: %d\n", nSize);
                    continue;
                }
                if (nSize > 256 * 1024 * 1024)
                {
                    LOG(REINDEX, "Reindex warning: Gigantic block: %d\n", nSize);
                }
                blkdat.GrowTo(2 * (nSize + MESSAGE_START_SIZE + sizeof(unsigned int)));
            }
            catch (const std::exception &)
            {
                // no valid block header found; don't complain
                break;
            }
            try
            {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos); // Unnecessary, I just got the position
                CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != chainparams.GetConsensus().hashGenesisBlock &&
                    LookupBlockIndex(block.hashPrevBlock) == nullptr)
                {
                    LOG(REINDEX, "%s: Out of order block %s (created %s), parent %s not known\n", __func__,
                        hash.ToString(), DateTimeStrFormat("%Y-%m-%d", block.nTime), block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                auto *pindex = LookupBlockIndex(hash);
                bool fHaveData = false;
                if (pindex)
                {
                    READLOCK(cs_mapBlockIndex);
                    fHaveData = (pindex->nStatus & BLOCK_HAVE_DATA);
                }
                if (pindex == nullptr || !fHaveData)
                {
                    CValidationState state;
                    if (ProcessNewBlock(state, chainparams, nullptr, &block, true, dbp, false))
                        nLoaded++;
                    if (state.IsError())
                        break;
                }
                else if (hash != chainparams.GetConsensus().hashGenesisBlock && pindex->nHeight % 1000 == 0)
                {
                    LOG(REINDEX, "Block Import: already had block %s at height %d\n", hash.ToString(), pindex->nHeight);
                }

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty())
                {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator,
                        std::multimap<uint256, CDiskBlockPos>::iterator>
                        range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second)
                    {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        if (ReadBlockFromDiskSequential(block, it->second, chainparams.GetConsensus()))
                        {
                            LOGA("%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(),
                                head.ToString());
                            CValidationState dummy;
                            if (ProcessNewBlock(dummy, chainparams, nullptr, &block, true, &it->second, false))
                            {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                    }
                }
            }
            catch (const std::exception &e)
            {
                LOGA("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    }
    catch (const std::runtime_error &e)
    {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LOGA("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

std::string GetWarnings(const std::string &strFor)
{
    std::string strStatusBar;
    std::string strRPC;
    std::string strGUI;

    if (!CLIENT_VERSION_IS_RELEASE)
    {
        strStatusBar =
            "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications";
        strGUI = _(
            "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
    }

    if (GetBoolArg("-testsafemode", DEFAULT_TESTSAFEMODE))
        strStatusBar = strRPC = strGUI = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        strStatusBar = strGUI = strMiscWarning;
    }

    if (fLargeWorkForkFound)
    {
        strStatusBar = strRPC =
            "Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.";
        strGUI =
            _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    }
    else if (fLargeWorkInvalidChainFound)
    {
        strStatusBar = strRPC = "Warning: We do not appear to fully agree with our peers! You may need to upgrade, or "
                                "other nodes may need to upgrade.";
        strGUI = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes "
                   "may need to upgrade.");
    }

    if (strFor == "gui")
        return strGUI;
    else if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}


//////////////////////////////////////////////////////////////////////////////
//
// Messages
//

bool AlreadyHaveBlock(const CInv &inv)
{
    READLOCK(cs_mapBlockIndex);
    // The Request Manager functionality requires that we return true only when we actually have received
    // the block and not when we have received the header only.  Otherwise the request manager may not
    // be able to update its block source in order to make re-requests.
    BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
    if (mi == mapBlockIndex.end())
        return false;
    if (!(mi->second->nStatus & BLOCK_HAVE_DATA))
        return false;
    return true;
}

ThresholdState VersionBitsTipState(const Consensus::Params &params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsState(chainActive.Tip(), params, pos, versionbitscache);
}

void MainCleanup()
{
    {
        WRITELOCK(cs_mapBlockIndex); // BU apply the appropriate lock so no contention during destruction
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();
    }

    {
        // orphan transactions
        WRITELOCK(orphanpool.cs_orphanpool);
        orphanpool.mapOrphanTransactions.clear();
        orphanpool.mapOrphanTransactionsByPrev.clear();
    }
}
