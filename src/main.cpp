// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "addrman.h"
#include "arith_uint256.h"
#include "blockrelay/graphene.h"
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
#include "hash.h"
#include "init.h"
#include "merkleblock.h"
#include "net.h"
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
#include "txdb.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "uahf_fork.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "versionbits.h"

#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/scope_exit.hpp>
#include <boost/thread.hpp>
#include <sstream>

#if defined(NDEBUG)
#error "Bitcoin cannot be compiled without assertions."
#endif

/**
 * Global state
 */

// Last time the block tip was updated
std::atomic<int64_t> nTimeBestReceived{0};

// BU moved CWaitableCriticalSection csBestBlock;
// BU moved CConditionVariable cvBlockChange;
bool fImporting = false;
bool fReindex = false;
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

extern CTweak<unsigned int> maxBlocksInTransitPerPeer;
extern CTweak<unsigned int> blockDownloadWindow;
extern CTweak<uint64_t> reindexTypicalBlockSize;

extern std::map<CNetAddr, ConnectionHistory> mapInboundConnectionTracker;
extern CCriticalSection cs_mapInboundConnectionTracker;

static void CheckBlockIndex(const Consensus::Params &consensusParams);

extern CCriticalSection cs_LastBlockFile;

extern CBlockIndex *pindexBestInvalid;
extern std::map<uint256, NodeId> mapBlockSource;
extern CCriticalSection cs_recentRejects;
extern std::unique_ptr<CRollingBloomFilter> recentRejects;
extern std::unique_ptr<CRollingBloomFilter> txn_recently_in_block;
extern std::set<int> setDirtyFileInfo;
extern std::map<uint256, std::pair<CBlockHeader, int64_t> > mapUnConnectedHeaders;
extern uint64_t nBlockSequenceId;

// Internal stuff
namespace
{
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

uint256 hashRecentRejectsChainTip;

/**
 * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
 * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
 * missing the data for the block.
 */
std::set<CBlockIndex *, CBlockIndexWorkComparator> setBlockIndexCandidates GUARDED_BY(cs_main);

/** Number of nodes with fSyncStarted. */
int nSyncStarted = 0;

/** Number of preferable block download peers. */
std::atomic<int> nPreferredDownload{0};

} // anon namespace

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
int GetHeight()
{
    LOCK(cs_main);
    return chainActive.Height();
}

void UpdatePreferredDownload(CNode *node, CNodeState *state)
{
    nPreferredDownload.fetch_sub(state->fPreferredDownload);

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = !node->fOneShot && !node->fClient;
    // BU allow downloads from inbound nodes; this may have been limited to stop attackers from connecting
    // and offering a bad chain.  However, we are connecting to multiple nodes and so can choose the most work
    // chain on that basis.
    // state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;
    // LOG(NET, "node %s preferred DL: %d because (%d || %d) && %d && %d\n", node->GetLogName(),
    //   state->fPreferredDownload, !node->fInbound, node->fWhitelisted, !node->fOneShot, !node->fClient);

    nPreferredDownload.fetch_add(state->fPreferredDownload);
}

void InitializeNode(const CNode *pnode)
{
    // Add an entry to the nodestate map
    LOCK(cs_main);
    mapNodeState.emplace_hint(mapNodeState.end(), std::piecewise_construct, std::forward_as_tuple(pnode->GetId()),
        std::forward_as_tuple(pnode->addr, pnode->addrName));
}

void FinalizeNode(NodeId nodeid)
{
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    DbgAssert(state != nullptr, return );

    if (state->fSyncStarted)
        nSyncStarted--;

    std::vector<uint256> vBlocksInFlight;
    requester.GetBlocksInFlight(vBlocksInFlight, nodeid);
    for (const uint256 &hash : vBlocksInFlight)
    {
        // Erase mapblocksinflight entries for this node.
        requester.MapBlocksInFlightErase(hash, nodeid);

        // Reset all requests times to zero so that we can immediately re-request these blocks
        requester.ResetLastBlockRequestTime(hash);
    }
    nPreferredDownload.fetch_sub(state->fPreferredDownload);

    mapNodeState.erase(nodeid);
    requester.RemoveNodeState(nodeid);
    if (mapNodeState.empty())
    {
        // Do a consistency check after the last peer is removed.  Force consistent state if production code
        DbgAssert(requester.MapBlocksInFlightEmpty(), requester.MapBlocksInFlightClear());
        DbgAssert(nPreferredDownload.load() == 0, nPreferredDownload.store(0));
    }
}

// Requires cs_main
bool PeerHasHeader(CNodeState *state, CBlockIndex *pindex)
{
    if (pindex == nullptr)
        return false;
    if (state->pindexBestKnownBlock && pindex == state->pindexBestKnownBlock->GetAncestor(pindex->nHeight))
        return true;
    if (state->pindexBestHeaderSent && pindex == state->pindexBestHeaderSent->GetAncestor(pindex->nHeight))
        return true;
    return false;
}
} // anon namespace


// Requires cs_main
bool CanDirectFetch(const Consensus::Params &consensusParams)
{
    return chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20;
}

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats)
{
    CNodeRef node(connmgr->FindNodeFromId(nodeid));
    if (!node)
        return false;

    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    DbgAssert(state != nullptr, return false);

    stats.nMisbehavior = node->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;

    std::vector<uint256> vBlocksInFlight;
    requester.GetBlocksInFlight(vBlocksInFlight, nodeid);
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

CCoinsViewCache *pcoinsTip = NULL;
CBlockTreeDB *pblocktree = nullptr;
CBlockTreeDB *pblocktreeother = nullptr;

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime =
        (flags & LOCKTIME_MEDIAN_TIME_PAST) ? chainActive.Tip()->GetMedianTimePast() : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

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

bool CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints *lp, bool useExistingLockPoints)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(mempool.cs);

    CBlockIndex *tip = chainActive.Tip();
    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints)
    {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else
    {
        // pcoinsTip contains the UTXO set for chainActive.Tip()
        CCoinsViewMemPool viewMemPool(pcoinsTip, mempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++)
        {
            const CTxIn &txin = tx.vin[txinIndex];
            Coin coin;
            if (!viewMemPool.GetCoin(txin.prevout, coin))
            {
                return error("%s: Missing input", __func__);
            }
            if (coin.nHeight == MEMPOOL_HEIGHT)
            {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            }
            else
            {
                prevheights[txinIndex] = coin.nHeight;
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp)
        {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for (int height : prevheights)
            {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight + 1)
                {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}

// Returns the script flags which should be checked for a given block
static unsigned int GetBlockScriptFlags(const CBlockIndex *pindex, const Consensus::Params &chainparams);

void LimitMempoolSize(CTxMemPool &pool, size_t limit, unsigned long age)
{
    std::vector<COutPoint> vCoinsToUncache;
    int expired = pool.Expire(GetTime() - age, vCoinsToUncache);
    for (const COutPoint &txin : vCoinsToUncache)
        pcoinsTip->Uncache(txin);
    if (expired != 0)
        LOG(MEMPOOL, "Expired %i transactions from the memory pool\n", expired);

    std::vector<COutPoint> vNoSpendsRemaining;
    pool.TrimToSize(limit, &vNoSpendsRemaining);
    for (const COutPoint &removed : vNoSpendsRemaining)
        pcoinsTip->Uncache(removed);
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)", state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", " + state.GetDebugMessage(), state.GetRejectCode());
}

static bool IsDAAEnabled(const Consensus::Params &consensusparams, int nHeight)
{
    return nHeight >= consensusparams.daaHeight;
}

bool IsDAAEnabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev)
{
    if (pindexPrev == nullptr)
    {
        return false;
    }

    return IsDAAEnabled(consensusparams, pindexPrev->nHeight);
}

bool IsMay152018Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev)
{
    if (pindexPrev == nullptr)
    {
        return false;
    }

    return pindexPrev->IsforkActiveOnNextBlock(miningForkTime.Value());
}

bool IsMay152018Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev)
{
    if (pindexPrev == nullptr)
    {
        return false;
    }

    return pindexPrev->forkAtNextBlock(miningForkTime.Value());
}


bool AreFreeTxnsDisallowed()
{
    if (GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) > 0)
        return false;

    return true;
}

static bool AcceptToMemoryPoolWorker(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx,
    std::vector<COutPoint> &vCoinsToUncache,
    bool *isRespend)
{
    *isRespend = false;
    unsigned int nSigOps = 0;
    ValidationResourceTracker resourceTracker;
    unsigned int nSize = 0;
    uint64_t start = GetTimeMicros();
    AssertLockHeld(cs_main);

    // After the May, 15 hard fork, we start accepting larger op_return.
    const CChainParams &chainparams = Params();
    const bool hasMay152018 = IsMay152018Enabled(chainparams.GetConsensus(), chainActive.Tip());

    if (!CheckTransaction(*ptx, state))
        return false;

    // Coinbase is only valid in a block, not as a loose transaction
    if (ptx->IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    // Reject nonstandard transactions if so configured.
    // (-testnet/-regtest allow nonstandard, and explicit submission via RPC)
    std::string reason;
    bool fRequireStandard = chainparams.RequireStandard();
    ;
    if (allowedTx == TransactionClass::STANDARD)
        fRequireStandard = true;
    else if (allowedTx == TransactionClass::NONSTANDARD)
        fRequireStandard = false;
    if (fRequireStandard && !IsStandardTx(*ptx, reason))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);

    // Don't relay version 2 transactions until CSV is active, and we can be
    // sure that such transactions will be mined (unless we're on
    // -testnet/-regtest).
    if (fRequireStandard && ptx->nVersion >= 2 &&
        VersionBitsTipState(chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV) != THRESHOLD_ACTIVE)
    {
        return state.DoS(0, false, REJECT_NONSTANDARD, "premature-version2-tx");
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(*ptx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // is it already in the memory pool?
    uint256 hash = ptx->GetHash();
    if (pool.exists(hash))
        return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool");

    // Check for conflicts with in-memory transactions and triggers actions at
    // end of scope (relay tx, sync wallet, etc)
    respend::RespendDetector respend(pool, ptx);
    *isRespend = respend.IsRespend();

    if (respend.IsRespend() && !respend.IsInteresting())
    {
        // Tx is a respend, and it's not an interesting one (we don't care to
        // validate it further)
        return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");
    }
    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        LockPoints lp;
        {
            READLOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
            view.SetBackend(viewMemPool);

            // do all inputs exist?
            if (pfMissingInputs)
            {
                *pfMissingInputs = false;
                for (const CTxIn &txin : ptx->vin)
                {
                    // At this point we begin to collect coins that are potential candidates for uncaching because as
                    // soon as we make the call below to view.HaveCoin() any missing coins will be pulled into cache.
                    // Therefore, any coin in this transaction that is not already in cache will be tracked here such
                    // that if this transaction fails to enter the memory pool, we will then uncache those coins that
                    // were not already present, unless the transaction is an orphan.
                    //
                    // We still want to keep orphantx coins in the event the orphantx is finally accepted into the
                    // mempool or shows up in a block that is mined.  Therefore if pfMissingInputs returns true then
                    // any coins in vCoinsToUncache will NOT be uncached.
                    if (!pcoinsTip->HaveCoinInCache(txin.prevout))
                    {
                        vCoinsToUncache.push_back(txin.prevout);
                    }

                    if (!view.HaveCoin(txin.prevout))
                    {
                        // fMissingInputs and not state.IsInvalid() is used to detect this condition, don't set
                        // state.Invalid()
                        *pfMissingInputs = true;
                    }
                }
                if (*pfMissingInputs == true)
                    return false;
            }

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(*ptx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);

            // Only accept BIP68 sequence locked transactions that can be mined in the next
            // block; we don't want our mempool filled up with transactions that can't
            // be mined yet.
            // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
            // CoinsViewCache instead of create its own
            if (!CheckSequenceLocks(*ptx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp))
                return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(*ptx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        nSigOps = GetLegacySigOpCount(*ptx);
        nSigOps += GetP2SHSigOpCount(*ptx, view);

        CAmount nValueOut = ptx->GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        double nPriorityDummy = 0;
        pool.ApplyDeltas(hash, nPriorityDummy, nModifiedFees);

        CAmount inChainInputValue;
        double dPriority = view.GetPriority(*ptx, chainActive.Height(), inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        {
            for (const CTxIn &txin : ptx->vin)
            {
                CoinAccessor coin(view, txin.prevout);
                if (coin->IsCoinBase())
                {
                    fSpendsCoinbase = true;
                    break;
                }
            }
        }

        CTxMemPoolEntry entry(ptx, nFees, GetTime(), dPriority, chainActive.Height(), pool.HasNoInputsOf(*ptx),
            inChainInputValue, fSpendsCoinbase, nSigOps, lp);
        nSize = entry.GetTxSize();

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine.
        if (nSigOps > MAX_TX_SIGOPS)
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false, strprintf("%d", nSigOps));

        CAmount mempoolRejectFee =
            pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
        if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee)
        {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false,
                strprintf("%d < %d", nFees, mempoolRejectFee));
        }
        else if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nModifiedFees < ::minRelayTxFee.GetFee(nSize) &&
                 !AllowFree(entry.GetPriority(chainActive.Height() + 1)))
        {
            // Require that free transactions have sufficient priority to be mined in the next block.
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
        }

        // BU - Xtreme Thinblocks Auto Mempool Limiter - begin section
        /* Continuously rate-limit free (really, very-low-fee) transactions
         * This mitigates 'penny-flooding' -- sending thousands of free transactions just to
         * be annoying or make others' transactions take longer to confirm. */
        // maximum nMinRelay in satoshi per byte
        static const int nLimitFreeRelay = GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY);

        // get current memory pool size
        uint64_t poolBytes = pool.GetTotalTxSize();

        // Calculate nMinRelay in satoshis per byte:
        //   When the nMinRelay is larger than the satoshiPerByte of the
        //   current transaction then spam blocking will be in effect. However
        //   Some free transactions will still get through based on -limitfreerelay
        static double nMinRelay = dMinLimiterTxFee.Value();
        static double nFreeLimit = nLimitFreeRelay;
        static int64_t nLastTime = GetTime();
        int64_t nNow = GetTime();

        static double _dMinLimiterTxFee = dMinLimiterTxFee.Value();
        static double _dMaxLimiterTxFee = dMaxLimiterTxFee.Value();

        static CCriticalSection cs_limiter;
        {
            LOCK(cs_limiter);

            // If the tweak values have changed then use them.
            if (dMinLimiterTxFee.Value() != _dMinLimiterTxFee)
            {
                _dMinLimiterTxFee = dMinLimiterTxFee.Value();
                nMinRelay = _dMinLimiterTxFee;
            }
            if (dMaxLimiterTxFee.Value() != _dMaxLimiterTxFee)
            {
                _dMaxLimiterTxFee = dMaxLimiterTxFee.Value();
            }

            // Limit check. Make sure minlimterfee is not > maxlimiterfee
            if (_dMinLimiterTxFee > _dMaxLimiterTxFee)
            {
                dMaxLimiterTxFee.Set(dMinLimiterTxFee.Value());
                _dMaxLimiterTxFee = _dMinLimiterTxFee;
            }

            // When the mempool starts falling use an exponentially decaying ~24 hour window:
            // nFreeLimit = nFreeLimit + ((double)(DEFAULT_LIMIT_FREE_RELAY - nFreeLimit) / pow(1.0 - 1.0/86400,
            // (double)(nNow - nLastTime)));
            nFreeLimit /= std::pow(1.0 - 1.0 / 86400, (double)(nNow - nLastTime));

            // When the mempool starts falling use an exponentially decaying ~24 hour window:
            nMinRelay *= std::pow(1.0 - 1.0 / 86400, (double)(nNow - nLastTime));

            uint64_t nLargestBlockSeen = LargestBlockSeen();

            if (poolBytes < nLargestBlockSeen)
            {
                nMinRelay = std::max(nMinRelay, _dMinLimiterTxFee);
                nFreeLimit = std::min(nFreeLimit, (double)nLimitFreeRelay);
            }
            else if (poolBytes < (nLargestBlockSeen * MAX_BLOCK_SIZE_MULTIPLIER))
            {
                // Gradually choke off what is considered a free transaction
                nMinRelay = std::max(nMinRelay,
                    _dMinLimiterTxFee + ((_dMaxLimiterTxFee - _dMinLimiterTxFee) * (poolBytes - nLargestBlockSeen) /
                                            (nLargestBlockSeen * (MAX_BLOCK_SIZE_MULTIPLIER - 1))));

                // Gradually choke off the nFreeLimit as well but leave at least DEFAULT_MIN_LIMITFREERELAY
                // So that some free transactions can still get through
                nFreeLimit = std::min(
                    nFreeLimit, ((double)nLimitFreeRelay - ((double)(nLimitFreeRelay - DEFAULT_MIN_LIMITFREERELAY) *
                                                               (double)(poolBytes - nLargestBlockSeen) /
                                                               (nLargestBlockSeen * (MAX_BLOCK_SIZE_MULTIPLIER - 1)))));
                if (nFreeLimit < DEFAULT_MIN_LIMITFREERELAY)
                    nFreeLimit = DEFAULT_MIN_LIMITFREERELAY;
            }
            else
            {
                nMinRelay = _dMaxLimiterTxFee;
                nFreeLimit = DEFAULT_MIN_LIMITFREERELAY;
            }

            minRelayTxFee = CFeeRate(nMinRelay * 1000);
            LOG(MEMPOOL, "MempoolBytes:%d  LimitFreeRelay:%.5g  nMinRelay:%.4g  FeesSatoshiPerByte:%.4g  TxBytes:%d  "
                         "TxFees:%d\n",
                poolBytes, nFreeLimit, nMinRelay, ((double)nFees) / nSize, nSize, nFees);
            if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize))
            {
                static double dFreeCount = 0;

                // Use an exponentially decaying ~10-minute window:
                dFreeCount *= std::pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));

                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                LOG(MEMPOOL, "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount + nSize);
                if ((dFreeCount + nSize) >=
                    (nFreeLimit * 10 * 1000 * nLargestBlockSeen / BLOCKSTREAM_CORE_MAX_BLOCK_SIZE))
                {
                    thindata.UpdateMempoolLimiterBytesSaved(nSize);
                    LOG(MEMPOOL, "AcceptToMemoryPool : free transaction %s rejected by rate limiter\n",
                        hash.ToString());
                    return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "rate limited free transaction");
                }
                dFreeCount += nSize;
            }
            nLastTime = nNow;
        }
        // BU - Xtreme Thinblocks Auto Mempool Limiter - end section

        // BU: we calculate the recommended fee by looking at what's in the mempool.  This starts at 0 though for an
        // empty mempool.  So set the minimum "absurd" fee to 10000 satoshies per byte.  If for some reason fees rise
        // above that, you can specify up to 100x what other txns are paying in the mempool
        if (fRejectAbsurdFee && nFees > std::max((int64_t)100L * nSize, maxTxFee.Value()) * 100)
            return state.Invalid(false, REJECT_HIGHFEE, "absurdly-high-fee",
                strprintf("%d > %d", nFees, std::max((int64_t)1L, maxTxFee.Value()) * 10000));

        // Calculate in-mempool ancestors, up to a limit.
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
        std::string errString;

        // Set extraFlags as a set of flags that needs to be activated.
        uint32_t extraFlags = 0;
        if (hasMay152018)
        {
            extraFlags |= SCRIPT_ENABLE_MAY152018_OPCODES;
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        unsigned char sighashType = 0;
        if (!CheckInputs(*ptx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS | extraFlags, true, &resourceTracker,
                nullptr, &sighashType))
        {
            LOG(MEMPOOL, "CheckInputs failed for tx: %s\n", ptx->GetHash().ToString().c_str());
            return false;
        }
        entry.UpdateRuntimeSigOps(resourceTracker.GetSigOps(), resourceTracker.GetSighashBytes());

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        unsigned char sighashType2 = 0;
        if (!CheckInputs(*ptx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS | extraFlags, true, nullptr, nullptr,
                &sighashType2))
        {
            return error(
                "%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                __func__, hash.ToString(), FormatStateMessage(state));
        }

        entry.sighashType = sighashType | sighashType2;
        // This code denies old style tx from entering the mempool as soon as we fork
        if (IsUAHFforkActiveOnNextBlock(chainActive.Tip()->nHeight) && !IsTxUAHFOnly(entry))
        {
            return state.Invalid(false, REJECT_WRONG_FORK, "txn-uses-old-sighash-algorithm");
        }

        respend.SetValid(true);
        if (respend.IsRespend())
            return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");

        {
            READLOCK(pool.cs);

            if (!pool._CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize,
                    nLimitDescendants, nLimitDescendantSize, errString))
            {
                return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
            }
            // Store transaction in memory
            pool.addUnchecked(hash, entry, setAncestors, !IsInitialBlockDownload());
        }

        // trim mempool and check if tx was trimmed
        if (!fOverrideMempoolLimit)
        {
            LimitMempoolSize(pool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
            if (!pool.exists(hash))
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
        }
        // BU: update tx per second when a tx is valid and accepted
        pool.UpdateTransactionsPerSecond();
        // BU - Xtreme Thinblocks - trim the orphan pool by entry time and do not allow it to be overidden.
    }

    if (!fRejectAbsurdFee)
        SyncWithWallets(ptx, nullptr, -1);

    int64_t end = GetTimeMicros();

    LOG(BENCH, "ValidateTransaction, time: %d, tx: %s, len: %d, sigops: %llu (legacy: %u), sighash: %llu, Vin: "
               "%llu, Vout: %llu\n",
        end - start, ptx->GetHash().ToString(), nSize, resourceTracker.GetSigOps(), (unsigned int)nSigOps,
        resourceTracker.GetSighashBytes(), ptx->vin.size(), ptx->vout.size());
    nTxValidationTime << (end - start);

    return true;
}

TransactionClass ParseTransactionClass(const std::string &s)
{
    std::string low = boost::algorithm::to_lower_copy(s);
    if (low == "nonstandard")
    {
        return TransactionClass::NONSTANDARD;
    }
    if (low == "standard")
    {
        return TransactionClass::STANDARD;
    }
    if (low == "default")
    {
        return TransactionClass::DEFAULT;
    }

    return TransactionClass::INVALID;
}

bool AcceptToMemoryPool(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx)
{
    std::vector<COutPoint> vCoinsToUncache;
    bool isRespend;
    bool res = AcceptToMemoryPoolWorker(pool, state, ptx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit,
        fRejectAbsurdFee, allowedTx, vCoinsToUncache, &isRespend);

    // Uncache any coins for txns that failed to enter the mempool but were NOT orphan txns
    if (isRespend || (pfMissingInputs && !res && !*pfMissingInputs))
    {
        for (const COutPoint &remove : vCoinsToUncache)
            pcoinsTip->Uncache(remove);
    }
    return res;
}

/** Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash,
    CTransactionRef &txOut,
    const Consensus::Params &consensusParams,
    uint256 &hashBlock,
    bool fAllowSlow)
{
    CBlockIndex *pindexSlow = nullptr;

    LOCK(cs_main);

    CTransactionRef ptx = mempool.get(hash);
    if (ptx)
    {
        txOut = ptx;
        return true;
    }

    if (fTxIndex)
    {
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(hash, postx))
        {
            CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try
            {
                file >> header;
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                file >> txOut;
            }
            catch (const std::exception &e)
            {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut->GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }

    // use coin database to locate block that contains transaction, and scan it
    if (fAllowSlow)
    {
        CoinAccessor coin(*pcoinsTip, hash);
        if (!coin->IsSpent())
            pindexSlow = chainActive[coin->nHeight];
    }

    if (pindexSlow)
    {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow, consensusParams))
        {
            for (const auto &tx : block.vtx)
            {
                if (tx->GetHash() == hash)
                {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}


//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

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

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
CBlockIndex *pindexBestForkTip = NULL, *pindexBestForkBase = NULL;

// Execute a command, as given by -alertnotify, on certain events such as a long fork being seen
static void AlertNotify(const std::string &strMessage)
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
        pindexBestForkTip = NULL;

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

void CheckForkWarningConditionsOnNewFork(CBlockIndex *pindexNewForkTip)
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

void static InvalidChainFound(CBlockIndex *pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
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


void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state)
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
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);

        // Now mark every block index on every chain that contains pindex as child of invalid
        MarkAllContainingChainsInvalid(pindex);
    }
}

void UpdateCoins(const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase())
    {
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn &txin : tx.vin)
        {
            txundo.vprevout.emplace_back();
            inputs.SpendCoin(txin.prevout, &txundo.vprevout.back());
        }
    }
    // add outputs
    AddCoins(inputs, tx, nHeight);
}

void UpdateCoins(const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, state, inputs, txundo, nHeight);
}

bool CScriptCheck::operator()()
{
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    CachingTransactionSignatureChecker checker(ptxTo, nIn, amount, nFlags, cacheStore);
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, checker, &error, &sighashType))
        return false;
    if (resourceTracker)
        resourceTracker->Update(ptxTo->GetHash(), checker.GetNumSigops(), checker.GetBytesHashed());
    return true;
}


bool CheckInputs(const CTransaction &tx,
    CValidationState &state,
    const CCoinsViewCache &inputs,
    bool fScriptChecks,
    unsigned int flags,
    bool cacheStore,
    ValidationResourceTracker *resourceTracker,
    std::vector<CScriptCheck> *pvChecks,
    unsigned char *sighashType)
{
    if (!tx.IsCoinBase())
    {
        if (!Consensus::CheckTxInputs(tx, state, inputs))
            return false;
        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

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
            for (unsigned int i = 0; i < tx.vin.size(); i++)
            {
                const COutPoint &prevout = tx.vin[i].prevout;
                CoinAccessor coin(inputs, prevout);

                if (coin->IsSpent())
                {
                    LOGA("ASSERTION: no inputs available\n");
                }
                assert(!coin->IsSpent());

                // We very carefully only pass in things to CScriptCheck which
                // are clearly committed. This provides
                // a sanity check that our caching is not introducing consensus
                // failures through additional data in, eg, the coins being
                // spent being checked as a part of CScriptCheck.
                const CScript scriptPubKey = coin->out.scriptPubKey;
                const CAmount amount = coin->out.nValue;

                // Verify signature
                CScriptCheck check(resourceTracker, scriptPubKey, amount, tx, i, flags, cacheStore);
                if (pvChecks)
                {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                }
                else if (!check())
                {
                    const bool hasNonMandatoryFlags = (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) != 0;
                    const bool doesNotHaveMay152018 = (flags & SCRIPT_ENABLE_MAY152018_OPCODES) == 0;
                    if (hasNonMandatoryFlags || doesNotHaveMay152018)
                    {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        //
                        // We also check activating the may152018 opcodes as it is a
                        // strictly additive change and we would not like to ban some of
                        // our peer that are ahead of us and are considering the fork
                        // as activated.
                        CScriptCheck check2(nullptr, scriptPubKey, amount, tx, i,
                            (flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS) | SCRIPT_ENABLE_MAY152018_OPCODES,
                            cacheStore);
                        if (check2())
                            return state.Invalid(
                                false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)",
                                                               ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100, false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)",
                                                                     ScriptErrorString(check.GetScriptError())));
                }
                if (sighashType)
                    *sighashType = check.sighashType;
            }
        }
    }

    return true;
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

enum DisconnectResult
{
    DISCONNECT_OK, // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED // Something else went wrong.
};

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
        fClean = false; // overwriting transaction output

    if (undo.nHeight == 0)
    {
        // Missing undo metadata (height and coinbase). Older versions included this
        // information only in undo records for the last spend of a transactions'
        // outputs. This implies that it must be present for some other output of the same tx.
        CoinAccessor alternate(view, out.hash);
        if (!alternate->IsSpent())
        {
            undo.nHeight = alternate->nHeight;
            undo.fCoinBase = alternate->fCoinBase;
        }
        else
        {
            return DISCONNECT_FAILED; // adding output for transaction without known metadata
        }
    }
    view.AddCoin(out, std::move(undo), undo.fCoinBase);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When UNCLEAN or FAILED is returned, view is left in an indeterminate state. */
static DisconnectResult DisconnectBlock(const CBlock &block, const CBlockIndex *pindex, CCoinsViewCache &view)
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
    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--)
    {
        const CTransaction &tx = *(block.vtx[i]);
        uint256 hash = tx.GetHash();

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        for (size_t o = 0; o < tx.vout.size(); o++)
        {
            if (!tx.vout[o].scriptPubKey.IsUnspendable())
            {
                COutPoint out(hash, o);
                Coin coin;
                view.SpendCoin(out, &coin);
                if (tx.vout[o] != coin.out)
                {
                    fClean = false; // transaction output mismatch
                }
            }
        }

        // restore inputs
        if (i > 0)
        { // not coinbases
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
                    return DISCONNECT_FAILED;
                fClean = fClean && res != DISCONNECT_UNCLEAN;
            }
            // At this point, all of txundo.vprevout should have been moved out.
        }
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

//
// Called periodically asynchronously; alerts if it smells like
// we're being fed a bad chain (blocks being generated much
// too slowly or too quickly).
//
void PartitionCheck(bool (*initialDownloadCheck)(),
    CCriticalSection &cs,
    const CBlockIndex *const &bestHeader,
    int64_t nPowTargetSpacing)
{
    if (bestHeader == NULL || initialDownloadCheck())
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

    LOCK(cs);
    const CBlockIndex *i = bestHeader;
    int nBlocks = 0;
    while (i->GetBlockTime() >= startTime)
    {
        ++nBlocks;
        i = i->pprev;
        if (i == NULL)
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
            ThresholdState state = VersionBitsState(pindexPrev, params, (Consensus::DeploymentPos)i, versionbitscache);
            // activate the bits that are STARTED or LOCKED_IN according to their deployments
            if (state == THRESHOLD_LOCKED_IN || state == THRESHOLD_STARTED)
            {
                nVersion |= VersionBitsMask(params, (Consensus::DeploymentPos)i);
            }
        }
        // bip135 end
    }

    return nVersion;
}

// bip135 : removed WarningBitsConditionChecker - no longer needed

// Protected by cs_main
static ThresholdConditionCache warningcache[Consensus::MAX_VERSION_BITS_DEPLOYMENTS];

static uint32_t GetBlockScriptFlags(const CBlockIndex *pindex, const Consensus::Params &consensusparams)
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

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    if (VersionBitsState(pindex->pprev, consensusparams, Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
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

    // The May 15, 2018 HF enable a set of opcodes.
    if (IsMay152018Enabled(consensusparams, pindex->pprev))
    {
        flags |= SCRIPT_ENABLE_MAY152018_OPCODES;
    }


    return flags;
}

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

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

bool ConnectBlock(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const CChainParams &chainparams,
    bool fJustCheck,
    bool fParallel)
{
    /** BU: Start Section to validate inputs - if there are parallel blocks being checked
     *      then the winner of this race will get to update the UTXO.
     */
    AssertLockHeld(cs_main);

    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
    if (!CheckBlock(block, state, !fJustCheck, !fJustCheck))
    {
        return false;
    }

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == NULL ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

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

    const int64_t timeBarrier = GetTime() - (24 * 3600 * checkScriptDays.Value());
    // Blocks that have various days of POW behind them makes them secure in that
    // real online nodes have checked the scripts.  Therefore, during initial block
    // download we don't need to check most of those scripts except for the most
    // recent ones.
    bool fScriptChecks = true;
    if (pindexBestHeader)
    {
        if (fReindex || fImporting)
            fScriptChecks = !fCheckpointsEnabled || block.nTime > timeBarrier;
        else
            fScriptChecks = !fCheckpointsEnabled || block.nTime > timeBarrier ||
                            (uint32_t)pindex->nHeight > pindexBestHeader->nHeight - (144 * checkScriptDays.Value());
    }

    int64_t nTime1 = GetTimeMicros();
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

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindex->pprev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    // Get the script flags for this block
    uint32_t flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus());
    bool fStrictPayToScriptHash = flags & SCRIPT_VERIFY_P2SH;


    int64_t nTime2 = GetTimeMicros();
    nTimeForks += nTime2 - nTime1;
    LOG(BENCH, "    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;
    ValidationResourceTracker resourceTracker;
    std::vector<int> prevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    int nChecked = 0;
    int nOrphansChecked = 0;
    const arith_uint256 nStartingChainWork = chainActive.Tip()->nChainWork;

    // Create a vector for storing hashes that will be deleted from the unverified and perverified txn sets.
    // We will delete these hashes only if and when this block is the one that is accepted saving us the unnecessary
    // repeated locking and unlocking of cs_xval.
    std::vector<uint256> vHashesToDelete;

    // Section for boost scoped lock on the scriptcheck_mutex
    boost::thread::id this_id(boost::this_thread::get_id());

    // Get the next available mutex and the associated scriptcheckqueue. Then lock this thread
    // with the mutex so that the checking of inputs can be done with the chosen scriptcheckqueue.
    CCheckQueue<CScriptCheck> *pScriptQueue(PV->GetScriptCheckQueue());

    // Aquire the control that is used to wait for the script threads to finish. Do this after aquiring the
    // scoped lock to ensure the scriptqueue is free and available.
    CCheckQueueControl<CScriptCheck> control(fScriptChecks && PV->ThreadCount() ? pScriptQueue : NULL);

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
        BOOST_SCOPE_EXIT(&fParallel) { PV->SetLocks(fParallel); }
        BOOST_SCOPE_EXIT_END


        // Start checking Inputs
        bool inOrphanCache;
        bool inVerifiedCache;
        // When in parallel mode then unlock cs_main for this loop to give any other threads
        // a chance to process in parallel. This is crucial for parallel validation to work.
        // NOTE: the only place where cs_main is needed is if we hit PV->ChainWorkHasChanged, which
        //       internally grabs the cs_main lock when needed.
        for (unsigned int i = 0; i < block.vtx.size(); i++)
        {
            const CTransaction &tx = *(block.vtx[i]);

            nInputs += tx.vin.size();
            nSigOps += GetLegacySigOpCount(tx);
            // if (nSigOps > MAX_BLOCK_SIGOPS)
            //    return state.DoS(100, error("ConnectBlock(): too many sigops"),
            //                    REJECT_INVALID, "bad-blk-sigops");

            if (!tx.IsCoinBase())
            {
                if (!view.HaveInputs(tx))
                {
                    // If we were validating at the same time as another block and the other block wins the validation
                    // race
                    // and updates the UTXO first, then we may end up here with missing inputs.  Therefore we checke to
                    // see
                    // if the chainwork has advanced or if we recieved a quit and if so return without DOSing the node.
                    if (PV->ChainWorkHasChanged(nStartingChainWork) || PV->QuitReceived(this_id, fParallel))
                    {
                        return false;
                    }
                    return state.DoS(100, error("ConnectBlock(): inputs missing/spent"), REJECT_INVALID,
                        "bad-txns-inputs-missingorspent");
                }

                // Check that transaction is BIP68 final
                // BIP68 lock checks (as opposed to nLockTime checks) must
                // be in ConnectBlock because they require the UTXO set
                prevheights.resize(tx.vin.size());
                {
                    for (size_t j = 0; j < tx.vin.size(); j++)
                    {
                        prevheights[j] = CoinAccessor(view, tx.vin[j].prevout)->nHeight;
                    }
                }

                if (!SequenceLocks(tx, nLockTimeFlags, &prevheights, *pindex))
                {
                    return state.DoS(100, error("%s: contains a non-BIP68-final transaction", __func__), REJECT_INVALID,
                        "bad-txns-nonfinal");
                }

                if (fStrictPayToScriptHash)
                {
                    // Add in sigops done by pay-to-script-hash inputs;
                    // this is to prevent a "rogue miner" from creating
                    // an incredibly-expensive-to-validate block.
                    nSigOps += GetP2SHSigOpCount(tx, view);
                    // if (nSigOps > MAX_BLOCK_SIGOPS)
                    //    return state.DoS(100, error("ConnectBlock(): too many sigops"),
                    //                     REJECT_INVALID, "bad-blk-sigops");
                }

                nFees += view.GetValueIn(tx) - tx.GetValueOut();

                // Only check inputs when the tx hash in not in the setPreVerifiedTxHash as would only
                // happen if this were a regular block or when a tx is found within the returning XThinblock.
                uint256 hash = tx.GetHash();
                {
                    {
                        LOCK(cs_xval);
                        inOrphanCache = setUnVerifiedOrphanTxHash.count(hash);
                        inVerifiedCache = setPreVerifiedTxHash.count(hash);
                    } /* We don't want to hold the lock while inputs are being checked or we'll slow down the competing
                         thread, if there is one */

                    if ((inOrphanCache) || (!inVerifiedCache && !inOrphanCache))
                    {
                        if (inOrphanCache)
                            nOrphansChecked++;

                        std::vector<CScriptCheck> vChecks;
                        bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks
                                                            (still consult the cache, though) */
                        if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, &resourceTracker,
                                PV->ThreadCount() ? &vChecks : NULL))
                        {
                            return error("ConnectBlock(): CheckInputs on %s failed with %s", tx.GetHash().ToString(),
                                FormatStateMessage(state));
                        }
                        control.Add(vChecks);
                        nChecked++;
                    }
                    else
                    {
                        vHashesToDelete.push_back(hash);
                    }
                }
            }

            CTxUndo undoDummy;
            if (i > 0)
            {
                blockundo.vtxundo.push_back(CTxUndo());
            }
            UpdateCoins(tx, state, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);
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
        LOG(THIN, "Number of CheckInputs() performed: %d  Orphan count: %d\n", nChecked, nOrphansChecked);


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

        CAmount blockReward = nFees + GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
        if (block.vtx[0]->GetValueOut() > blockReward)
            return state.DoS(100, error("ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",
                                      block.vtx[0]->GetValueOut(), blockReward),
                REJECT_INVALID, "bad-cb-amount");

        if (fJustCheck)
            return true;


        /*****************************************************************************************************************
         *                         Start update of UTXO, if this block wins the validation race *
         *****************************************************************************************************************/
        // If in PV mode and we win the race then we lock everyone out by taking cs_main but before updating the UTXO
        // and
        // terminating any competing threads.
    } // cs_main is re-aquired automatically as we go out of scope from the BOOST scope guard

    // Last check for chain work just in case the thread manages to get here before being terminated.
    if (PV->ChainWorkHasChanged(nStartingChainWork) || PV->QuitReceived(this_id, fParallel))
    {
        return false; // no need to lock cs_main before returning as it should already be locked.
    }

    // Quit any competing threads may be validating which have the same previous block before updating the UTXO.
    PV->QuitCompetingThreads(block.GetBlockHeader().hashPrevBlock);

    int64_t nTime3 = GetTimeMicros();
    nTimeConnect += nTime3 - nTime2;
    LOG(BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(),
        0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(),
        nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs - 1), nTimeConnect * 0.000001);

    int64_t nTime4 = GetTimeMicros();
    nTimeVerify += nTime4 - nTime2;
    LOG(BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime4 - nTime2),
        nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs - 1), nTimeVerify * 0.000001);


    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull())
        {
            CDiskBlockPos _pos;
            if (!FindUndoPos(state, pindex->nFile, _pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock(): FindUndoPos failed");

            if (!WriteUndoToDisk(blockundo, _pos, pindex->pprev, chainparams.MessageStart()))
                return AbortNode(state, "Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = _pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    // add this block to the view's block chain (the main UTXO in memory cache)
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetTimeMicros();
    nTimeIndex += nTime5 - nTime4;
    LOG(BENCH, "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0]->GetHash();

    int64_t nTime6 = GetTimeMicros();
    nTimeCallbacks += nTime6 - nTime5;
    LOG(BENCH, "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);

    PV->Cleanup(block, pindex); // NOTE: this must be run whether in fParallel or not!

    // Track all recent txns in a block so we don't re-request them again. This can happen a txn announcement
    // arrives just after the block is received.
    {
        LOCK(cs_recentRejects);
        for (const CTransactionRef &ptx : block.vtx)
        {
            txn_recently_in_block->insert(ptx->GetHash());
        }
    }

    // Delete hashes from unverified and preverified sets that will no longer be needed after the block is accepted.
    {
        LOCK(cs_xval);
        for (const uint256 &hash : vHashesToDelete)
        {
            setPreVerifiedTxHash.erase(hash);
            setUnVerifiedOrphanTxHash.erase(hash);
        }
    }
    return true;
}

// bip135 begin
/** Check for conspicuous versionbit signal events in last 100 blocks and alert. */
void static CheckAndAlertUnknownVersionbits(const CChainParams &chainParams, const CBlockIndex *chainTip)
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
                for (int i = 0; i < BIT_WARNING_WINDOW && iindex != NULL; i++)
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
        if (!fWarned)
        {
            AlertNotify(strMiscWarning);
            fWarned = true;
        }
    }
}
// bip135 end

/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex *pindexNew)
{
    const CChainParams &chainParams = Params();
    chainActive.SetTip(pindexNew);

    // Check Activate May 2018 HF rules after each new tip is connected and the blockindex updated.

    // First check if the next block is the fork block and set non-consensus parameters appropriately
    if (IsMay152018Next(chainParams.GetConsensus(), pindexNew))
    {
        // Bump the default generated size to 8MB
        if (miningForkMG.Value() > maxGeneratedBlock)
            maxGeneratedBlock = miningForkMG.Value();
    }

    // Next, check every on every block for EB < 32MB and force this as the minimum because this is a consensus issue
    if (IsMay152018Enabled(chainParams.GetConsensus(), pindexNew))
    {
        if (miningForkEB.Value() > excessiveBlockSize)
        {
            excessiveBlockSize = miningForkEB.Value();
            settingsToUserAgentString();
        }
    }

    // New best block
    nTimeBestReceived.store(GetTime());
    mempool.AddTransactionsUpdated(1);

    LOGA("%s: new best=%s  height=%d bits=%d log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%utxo)\n",
        __func__, chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), chainActive.Tip()->nBits,
        log(chainActive.Tip()->nChainWork.getdouble()) / log(2.0), (unsigned long)chainActive.Tip()->nChainTx,
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip()),
        pcoinsTip->DynamicMemoryUsage() * (1.0 / (1 << 20)), pcoinsTip->GetCacheSize());

    cvBlockChange.notify_all();

    if (!IsInitialBlockDownload())
    {
        // Check the version of the last 100 blocks,
        // alert if significant signaling changes.
        CheckAndAlertUnknownVersionbits(chainParams, chainActive.Tip());
    }
}

/** Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and manually re-limit mempool size
 * after this, with cs_main held. */
bool DisconnectTip(CValidationState &state, const Consensus::Params &consensusParams, const bool fRollBack)
{
    AssertLockHeld(cs_main);

    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete, consensusParams))
        return AbortNode(state, "DisconnectTip(): Failed to read block");
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (DisconnectBlock(block, pindexDelete, view) != DISCONNECT_OK)
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        bool result = view.Flush();
        assert(result);
    }
    LOG(BENCH, "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;

    // If this block enabled the may152018 opcodes, then we need to
    // clear the mempool of any transaction using them.
    if (IsMay152018Enabled(consensusParams, pindexDelete) && !IsMay152018Enabled(consensusParams, pindexDelete->pprev))
    {
        mempool.clear();
    }

    // Resurrect mempool transactions from the disconnected block but do not do this step if we are
    // rolling back the chain using the "rollbackchain" rpc command.
    if (!fRollBack)
    {
        std::vector<uint256> vHashUpdate;
        for (const auto &ptx : block.vtx)
        {
            // ignore validation errors in resurrected transactions
            std::list<CTransactionRef> removed;
            CValidationState stateDummy;
            if (ptx->IsCoinBase() ||
                !AcceptToMemoryPool(mempool, stateDummy, ptx, AreFreeTxnsDisallowed(), nullptr, true))
            {
                mempool.remove(*ptx, removed, true);
            }
            else if (mempool.exists(ptx->GetHash()))
            {
                vHashUpdate.push_back(ptx->GetHash());
            }
        }
        // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
        // no in-mempool children, which is generally not true when adding
        // previously-confirmed transactions back to the mempool.
        // UpdateTransactionsFromBlock finds descendants of any transactions in this
        // block that were added back and cleans up the mempool state.
        mempool.UpdateTransactionsFromBlock(vHashUpdate);
    }

    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    for (const auto &ptx : block.vtx)
    {
        SyncWithWallets(ptx, nullptr, -1);
    }

    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool static ConnectTip(CValidationState &state,
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
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock)
    {
        if (!ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus()))
            return AbortNode(state, "ConnectTip(): Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
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
        int64_t nStart = GetTimeMicros();
        bool result = view.Flush();
        assert(result);
        LOG(BENCH, "      - Update Coins %.3fms\n", GetTimeMicros() - nStart);

        mapBlockSource.erase(pindexNew->GetBlockHash());
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LOG(BENCH, "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
    }

    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    LOG(BENCH, "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary, and only during IBD, reindex, or importing.
    if (!IsChainNearlySyncd() || fReindex || fImporting)
        if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
            return false;
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    LOG(BENCH, "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.
    std::list<CTransactionRef> txConflicted;
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted, !IsInitialBlockDownload());
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

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LOG(BENCH, "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LOG(BENCH, "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
CBlockIndex *FindMostWorkChain()
{
    AssertLockHeld(cs_main);
    do
    {
        CBlockIndex *pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex *, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
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
            if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
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
    DbgAssert(0, return NULL); // should never get here
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates()
{
    AssertLockHeld(cs_main);
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex *, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip()))
    {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex *pindexMostWork,
    const CBlock *pblock,
    bool fParallel)
{
    AssertLockHeld(cs_main);
    bool fInvalidFound = false;
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);
    CBlockIndex *pindexNewMostWork;

    bool fBlocksDisconnected = false;
    boost::thread::id this_id(boost::this_thread::get_id()); // get this thread's id

    while (chainActive.Tip() && chainActive.Tip() != pindexFork)
    {
        // When running in parallel block validation mode it is possible that this competing block could get to this
        // point just after the chaintip had already been advanced.  If that were to happen then it could initiate a
        // re-org when in fact a Quit had already been called on this thread.  So we do a check if Quit was previously
        // called and return if true.
        if (PV->QuitReceived(this_id, fParallel))
            return false;

        // Indicate that this thread has now initiated a re-org
        PV->IsReorgInProgress(this_id, true, fParallel);

        // Disconnect active blocks which are no longer in the best chain. We do not need to concern ourselves with any
        // block validation threads that may be running for the chain we are rolling back. They will automatically fail
        // validation during ConnectBlock() once the chaintip has changed..
        if (!DisconnectTip(state, chainparams.GetConsensus()))
            return false;

        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex *> vpindexToConnect;
    bool fContinue = true;
    /** Parallel Validation: fBlock determines whether we pass a block or NULL to ConnectTip().
     *  If the pindexMostWork has been extended while we have been validating the last block then we
     *  want to pass a NULL so that the next block is read from disk, because we will definitely not
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
            CBlockIndex *pindexConnect = *i;
            // Check if the best chain has changed while we were disconnecting or processing blocks.
            // If so then we need to return and continue processing the newer chain.
            pindexNewMostWork = FindMostWorkChain();
            if (!pindexMostWork)
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
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
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
                    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindexNewTip);
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
        }
        if (fInvalidFound)
            break; // stop processing more blocks if the last one was invalid.

        // Notify the UI with the new block tip information.
        if (pindexMostWork->nHeight >= nHeight && pindexNewTip != nullptr && pindexLastNotify != pindexNewTip)
            uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindexNewTip);

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
        mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
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
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState &state, const CChainParams &chainparams, const CBlock *pblock, bool fParallel)
{
    CBlockIndex *pindexMostWork = nullptr;
    LOCK(cs_main);

    bool fOneDone = false;
    do
    {
        boost::this_thread::interruption_point();
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
                BlockMap::iterator mi = mapBlockIndex.find(pblock->GetHash());
                if (mi == mapBlockIndex.end())
                {
                    LOGA("Could not find block in mapBlockIndex: %s\n", pblock->GetHash().ToString());
                    return false;
                }
                else
                {
                    // Because we are potentially working with a block that is not the pindexMostWork as returned by
                    // FindMostWorkChain() but rather are forcing it to point to this block we must check again if
                    // this block has enough work to advance the tip.
                    pindexMostWork = (*mi).second;
                    if (pindexMostWork->nChainWork <= pindexOldTip->nChainWork)
                    {
                        return false;
                    }
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
                ((pblock) && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL), fParallel))
            return false;

        // Check if the best chain has changed while we were processing blocks.  If so then we need to
        // continue processing the newer chain.  This satisfies a rare edge case where we have initiated
        // a reorg to another chain but before the reorg is complete we end up reorging to a different
        // chain. Set pblock to NULL here to make sure as we continue we get blocks from disk.
        pindexMostWork = FindMostWorkChain();
        if (!pindexMostWork)
            return false;
        pblock = nullptr;
        fOneDone = true;
    } while (pindexMostWork->nChainWork > chainActive.Tip()->nChainWork);
    CheckBlockIndex(chainparams.GetConsensus());

    return true;
}

bool InvalidateBlock(CValidationState &state, const Consensus::Params &consensusParams, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    while (chainActive.Contains(pindex))
    {
        CBlockIndex *pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, consensusParams))
        {
            mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
            return false;
        }
    }

    LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
        GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
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

    InvalidChainFound(pindex);
    // Now mark every block index on every chain that contains pindex as child of invalid
    MarkAllContainingChainsInvalid(pindex);
    mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);
    return true;
}

bool ReconsiderBlock(CValidationState &state, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

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
            if (it->second == pindexBestInvalid)
            {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL)
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

CBlockIndex *AddToBlockIndex(const CBlockHeader &block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex *pindexNew = new CBlockIndex(block);
    assert(pindexNew);
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
        // BU If the prior block or an ancestor has failed, mark this one failed
        if (pindexNew->pprev && pindexNew->pprev->nStatus & BLOCK_FAILED_MASK)
            pindexNew->nStatus |= BLOCK_FAILED_CHILD;
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);

    if ((!(pindexNew->nStatus & BLOCK_FAILED_MASK)) &&
        (pindexBestHeader == NULL || pindexBestHeader->nChainWork < pindexNew->nChainWork))
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindexNew,
    const CDiskBlockPos &pos)
{
    AssertLockHeld(cs_main); // for setBlockIndexCandidates use
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

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx)
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
            {
                pindex->nSequenceId = ++nBlockSequenceId;
            }
            if (chainActive.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip()))
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

bool FindBlockPos(CValidationState &state,
    CDiskBlockPos &pos,
    unsigned int nAddSize,
    unsigned int nHeight,
    uint64_t nTime,
    bool fKnown = false)
{
    // nDataPos for blockdb is a flag, just set to 1 to indicate we have that data. nFile is unused.
    if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
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
    if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
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

bool CheckBlock(const CBlock &block, CValidationState &state, bool fCheckPOW, bool fCheckMerkleRoot, bool fConservative)
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
        if (!CheckTransaction(*tx, state))
            return error("CheckBlock(): CheckTransaction of %s failed with %s", tx->GetHash().ToString(),
                FormatStateMessage(state));

    uint64_t nSigOps = 0;
    // BU: count the number of transactions in case the CheckExcessive function wants to use this as criteria
    uint64_t nTx = 0;
    uint64_t nLargestTx = 0; // BU: track the longest transaction

    for (const auto &tx : block.vtx)
    {
        nTx++;
        nSigOps += GetLegacySigOpCount(*tx);
        uint64_t nTxSize = ::GetSerializeSize(*tx, SER_NETWORK, PROTOCOL_VERSION);
        if (nTxSize > nLargestTx)
            nLargestTx = nTxSize;
    }

    // BU only enforce sigops during block generation not acceptance
    if (fConservative && (nSigOps > BLOCKSTREAM_CORE_MAX_BLOCK_SIGOPS))
        return state.DoS(100, error("CheckBlock(): out-of-bounds SigOpCount"), REJECT_INVALID, "bad-blk-sigops", true);

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    // BU: Check whether this block exceeds what we want to relay.
    block.fExcessive = CheckExcessive(block, block.GetBlockSize(), nSigOps, nTx, nLargestTx);

    return true;
}

bool CheckAgainstCheckpoint(unsigned int height, const uint256 &hash, const CChainParams &chainparams)
{
    const CCheckpointData &ckpt = chainparams.Checkpoints();
    const auto &lkup = ckpt.mapCheckpoints.find(height);
    if (lkup != ckpt.mapCheckpoints.end()) // this block height is checkpointed
    {
        if (hash != lkup->second) // This block does not match the checkpoint
            return false;
    }
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

bool ContextualCheckBlock(const CBlock &block, CValidationState &state, CBlockIndex *const pindexPrev)
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params &consensusParams = Params().GetConsensus();

    // Start enforcing BIP113 (Median Time Past) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_CSV, versionbitscache) == THRESHOLD_ACTIVE)
    {
        nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    int64_t nLockTimeCutoff;
    if (pindexPrev == nullptr)
        nLockTimeCutoff = block.GetBlockTime();
    else
        nLockTimeCutoff =
            (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST) ? pindexPrev->GetMedianTimePast() : block.GetBlockTime();

    // Check that all transactions are finalized
    for (const auto &tx : block.vtx)
    {
        if (!IsFinalTx(*tx, nHeight, nLockTimeCutoff))
        {
            return state.DoS(
                10, error("%s: contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
        }
    }

    // Enforce block nVersion=2 rule that the coinbase starts with serialized block height
    if (nHeight >= consensusParams.BIP34Height)
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0]->vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0]->vin[0].scriptSig.begin()))
        {
            int blockCoinbaseHeight = block.GetHeight();
            uint256 hashp = block.hashPrevBlock;
            uint256 hash = block.GetHash();
            return state.DoS(100, error("%s: block height mismatch in coinbase, expected %d, got %d, block is %s, "
                                        "parent block is %s, pprev is %s",
                                      __func__, nHeight, blockCoinbaseHeight, hash.ToString(), hashp.ToString(),
                                      pindexPrev->phashBlock->ToString()),
                REJECT_INVALID, "bad-cb-height");
        }
    }

    // UAHF enforce that the fork block is > 1MB
    // (note subsequent blocks can be <= 1MB...)
    // An exception is added -- if the fork block is block 1 then it can be <= 1MB.  This allows test chains to
    // fork without having to create a large block so long as the fork time is in the past.
    // TODO: check if we can remove the second conditions since on regtest uahHeight is 0
    if (pindexPrev && UAHFforkAtNextBlock(pindexPrev->nHeight) && (pindexPrev->nHeight > 1))
    {
        DbgAssert(block.GetBlockSize(), );
        if (block.GetBlockSize() <= BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
        {
            uint256 hash = block.GetHash();
            return state.DoS(100,
                error("%s: UAHF fork block (%s, height %d) must exceed %d, but this block is %d bytes", __func__,
                                 hash.ToString(), nHeight, BLOCKSTREAM_CORE_MAX_BLOCK_SIZE, block.GetBlockSize()),
                REJECT_INVALID, "bad-blk-too-small");
        }
    }

    return true;
}

bool AcceptBlockHeader(const CBlockHeader &block,
    CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex **ppindex)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetHash();
    CBlockIndex *pindex = NULL;
    if (hash != chainparams.GetConsensus().hashGenesisBlock)
    {
        BlockMap::iterator miSelf = mapBlockIndex.find(hash);
        if (miSelf != mapBlockIndex.end())
        {
            // Block header is already known.
            pindex = miSelf->second;
            if (ppindex)
                *ppindex = pindex;
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return state.Invalid(
                    error("%s: block %s height %d is marked invalid", __func__, hash.ToString(), pindex->nHeight), 0,
                    "duplicate");
            return true;
        }

        if (!CheckBlockHeader(block, state))
            return false;

        // Get prev block index
        CBlockIndex *pindexPrev = NULL;
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end())
            return state.DoS(10, error("%s: previous block %s not found while accepting %s", __func__,
                                     block.hashPrevBlock.ToString(), hash.ToString()),
                0, "bad-prevblk");
        pindexPrev = (*mi).second;
        assert(pindexPrev);
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100,
                error("%s: previous block %s is invalid", __func__, pindexPrev->GetBlockHash().GetHex().c_str()),
                REJECT_INVALID, "bad-prevblk");

        // If the parent block belongs to the set of checkpointed blocks but it has a mismatched hash,
        // then we are on the wrong fork so ignore
        if (fCheckpointsEnabled && !CheckAgainstCheckpoint(pindexPrev->nHeight, *pindexPrev->phashBlock, chainparams))
            return error("%s: CheckAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

        if (!ContextualCheckBlockHeader(block, state, pindexPrev))
            return false;
    }
    if (pindex == NULL)
        pindex = AddToBlockIndex(block);

    // If the block belongs to the set of check-pointed blocks but it has a mismatched hash,
    // then we are on the wrong fork so ignore
    if (fCheckpointsEnabled && !CheckAgainstCheckpoint(pindex->nHeight, *pindex->phashBlock, chainparams))
    {
        pindex->nStatus |= BLOCK_FAILED_VALID; // block doesn't match checkpoints so invalid
        pindex->nStatus &= ~BLOCK_VALID_CHAIN;
    }

    if (ppindex)
        *ppindex = pindex;

    return true;
}

/** Store block on disk. If dbp is non-NULL, the file is known to already reside on disk */
static bool AcceptBlock(const CBlock &block,
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
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
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
    if (!fRequested)
    { // If we didn't ask for it:
        if (pindex->nTx != 0)
            return true; // This is a previously-processed block that was pruned
        if (!fHasMoreWork)
            return true; // Don't process less-work chains
        if (fTooFarAhead)
            return true; // Block height is too high
    }
    if ((!CheckBlock(block, state)) || !ContextualCheckBlock(block, state, pindex->pprev))
    {
        if (state.IsInvalid() && !state.CorruptionPossible())
        {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            setDirtyBlockIndex.insert(pindex);
            // Now mark every block index on every chain that contains pindex as child of invalid
            MarkAllContainingChainsInvalid(pindex);
        }
        return false;
    }
    int nHeight = pindex->nHeight;
    // Write block to history file
    try
    {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != NULL)
        {
            blockPos = *dbp;
        }
        if (!FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != NULL))
        {
            return error("AcceptBlock(): FindBlockPos failed");
        }
        if (dbp == NULL)
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

bool ProcessNewBlock(CValidationState &state,
    const CChainParams &chainparams,
    CNode *pfrom,
    const CBlock *pblock,
    bool fForceProcessing,
    CDiskBlockPos *dbp,
    bool fParallel)
{
    int64_t start = GetTimeMicros();
    LOG(THIN, "Processing new block %s from peer %s.\n", pblock->GetHash().ToString(),
        pfrom ? pfrom->GetLogName() : "myself");
    // Preliminary checks
    if (!CheckBlockHeader(*pblock, state, true))
    { // block header is bad
        // demerit the sender
        return error("%s: CheckBlockHeader FAILED", __func__);
    }
    if (IsChainNearlySyncd() && !fImporting && !fReindex)
        SendExpeditedBlock(*pblock, pfrom);

    bool checked = CheckBlock(*pblock, state);
    if (!checked)
    {
        LOGA("Invalid block: ver:%x time:%d Tx size:%d len:%d\n", pblock->nVersion, pblock->nTime, pblock->vtx.size(),
            pblock->GetBlockSize());
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
        CBlockIndex *pindex = NULL;
        bool ret = AcceptBlock(*pblock, state, chainparams, &pindex, fRequested, dbp);
        if (pindex && pfrom)
        {
            mapBlockSource[pindex->GetBlockHash()] = pfrom->GetId();
        }
        CheckBlockIndex(chainparams.GetConsensus());
        if (!ret)
        {
            // BU TODO: if block comes out of order (before its parent) this will happen.  We should cache the block
            // until the parents arrive.
            return error("%s: AcceptBlock FAILED", __func__);
        }

        // We must indicate to the request manager that the block was received only after it has
        // been stored to disk. Doing so prevents unnecessary re-requests.
        CInv inv(MSG_BLOCK, hash);
        requester.Received(inv, pfrom);
    }
    if (!ActivateBestChain(state, chainparams, pblock, fParallel))
    {
        if (state.IsInvalid() || state.IsError())
            return error("%s: ActivateBestChain failed", __func__);
        else
            return false;
    }

    int64_t end = GetTimeMicros();

    if (Logging::LogAcceptCategory(Logging::BENCH))
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


bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}


CBlockIndex *InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex *pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw std::runtime_error("LoadBlockIndex(): new CBlockIndex failed");
    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool static LoadBlockIndexDB()
{
    AssertLockHeld(cs_main);
    const CChainParams &chainparams = Params();
    if (!pblocktree->LoadBlockIndexGuts())
    {
        return false;
    }

    /** This sync method will break on pruned nodes so we cant use if pruned*/
    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (!fHavePruned)
    {
        // by default we want to sync from disk instead of network if possible
        bool syncBlocks = true;
        if (!DetermineStorageSync())
        {
            syncBlocks = false;
        }
        if (syncBlocks)
        {
            // run a db sync here to sync storage methods
            // may increase startup time significantly but is faster than network sync
            LOGA("Upgrading block database...\n");
            uiInterface.InitMessage(_("Upgrading block database...This could take a while."));
            SyncStorage(chainparams);
        }
    }

    delete pblocktreeother;
    pblocktreeother = nullptr;
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

    boost::this_thread::interruption_point();

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
        if (pindex->nStatus & BLOCK_FAILED_MASK &&
            (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) &&
            (pindexBestHeader == nullptr || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    if (BLOCK_DB_MODE != DB_BLOCK_STORAGE)
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
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LOGA("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

    // Load pointer to end of best chain
    uint256 bestblockhash;
    if (BLOCK_DB_MODE == SEQUENTIAL_BLOCK_FILES)
    {
        bestblockhash = pcoinsdbview->GetBestBlockSeq();
    }
    if (BLOCK_DB_MODE == DB_BLOCK_STORAGE)
    {
        bestblockhash = pcoinsdbview->GetBestBlockDb();
    }
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

CVerifyDB::CVerifyDB() { uiInterface.ShowProgress(_("Verifying blocks..."), 0); }
CVerifyDB::~CVerifyDB() { uiInterface.ShowProgress("", 100); }
bool CVerifyDB::VerifyDB(const CChainParams &chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == NULL || chainActive.Tip()->pprev == NULL)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LOGA("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex *pindexState = chainActive.Tip();
    CBlockIndex *pindexFailure = NULL;
    int nGoodTransactions = 0;
    CValidationState state;
    for (CBlockIndex *pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        uiInterface.ShowProgress(_("Verifying blocks..."),
            std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth *
                                           (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainActive.Height() - nCheckDepth)
            break;
        if (fPruneMode && !(pindex->nStatus & BLOCK_HAVE_DATA))
        {
            // If pruning, only go back as far as we have data.
            LOGA("VerifyDB(): block verification stopping at height %d (pruning, no data)\n", pindex->nHeight);
            break;
        }
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state))
            return error(
                "VerifyDB(): *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex)
        {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull())
            {
                if (!ReadUndoFromDisk(undo, pos, pindex->pprev))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight,
                        pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState &&
            (int64_t)(coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheMaxSize)
        {
            bool fClean = true;
            DisconnectResult res = DisconnectBlock(block, pindex, coins);
            if (res == DISCONNECT_FAILED)
            {
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s",
                    pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->pprev;
            if (!fClean)
            {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            }
            else
                nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error(
            "VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n",
            chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4)
    {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip())
        {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."),
                std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) /
                                                     (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                    pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins, chainparams))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight,
                    pindex->GetBlockHash().ToString());
        }
    }

    LOGA("No coin database inconsistencies in last %i blocks (%i transactions)\n",
        chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

void UnloadBlockIndex()
{
    {
        LOCK(orphanpool.cs);
        orphanpool.mapOrphanTransactions.clear();
        orphanpool.mapOrphanTransactionsByPrev.clear();
        orphanpool.nBytesOrphanPool = 0;
    }

    nPreferredDownload.store(0);

    LOCK(cs_main);
    nBlockSequenceId = 1;
    nSyncStarted = 0;
    nLastBlockFile = 0;
    mapUnConnectedHeaders.clear();
    setBlockIndexCandidates.clear();
    chainActive.SetTip(nullptr);
    pindexBestInvalid = nullptr;
    pindexBestHeader = nullptr;
    mempool.clear();
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    mapBlockSource.clear();
    requester.MapBlocksInFlightClear();
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    mapNodeState.clear();
    versionbitscache.Clear();
    for (int b = 0; b < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; b++)
    {
        warningcache[b].clear();
    }

    for (BlockMap::value_type &entry : mapBlockIndex)
    {
        delete entry.second;
    }
    mapBlockIndex.clear();
    fHavePruned = false;

    LOCK(cs_recentRejects);
    recentRejects.reset(nullptr);
}

bool LoadBlockIndex()
{
    LOCK(cs_main);
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexDB())
        return false;
    return true;
}

bool InitBlockIndex(const CChainParams &chainparams)
{
    LOCK(cs_main);

    // Initialize global variables that cannot be constructed at startup.
    {
        LOCK(cs_recentRejects);
        recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));

        // Using an average 500 bytes per transaction to calculate number of bloom filter elements.
        //
        // We hold a maximum of two blocks worth of data in the event that two blocks
        // are mined very close in time. But in general we only need one block of data.
        uint32_t nElements = 2 * excessiveBlockSize / 500;
        txn_recently_in_block.reset(new CRollingBloomFilter(nElements, 0.000001));
    }

    // Check whether we're already initialized
    if (chainActive.Genesis() != nullptr)
        return true;

    // Use the provided setting for -txindex in the new database
    fTxIndex = GetBoolArg("-txindex", DEFAULT_TXINDEX);
    pblocktree->WriteFlag("txindex", fTxIndex);
    LOGA("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex)
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
            boost::this_thread::interruption_point();

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
                    mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end())
                {
                    LOG(REINDEX, "%s: Out of order block %s (created %s), parent %s not known\n", __func__,
                        hash.ToString(), DateTimeStrFormat("%Y-%m-%d", block.nTime), block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0)
                {
                    CValidationState state;
                    if (ProcessNewBlock(state, chainparams, NULL, &block, true, dbp, false))
                        nLoaded++;
                    if (state.IsError())
                        break;
                }
                else if (hash != chainparams.GetConsensus().hashGenesisBlock &&
                         mapBlockIndex[hash]->nHeight % 1000 == 0)
                {
                    LOG(REINDEX, "Block Import: already had block %s at height %d\n", hash.ToString(),
                        mapBlockIndex[hash]->nHeight);
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
                            if (ProcessNewBlock(dummy, chainparams, NULL, &block, true, &it->second, false))
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

void static CheckBlockIndex(const Consensus::Params &consensusParams)
{
    if (!fCheckBlockIndex)
    {
        return;
    }

    LOCK(cs_main);

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
        rangeGenesis = forward.equal_range(NULL);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex *pindexFirstInvalid = NULL; // Oldest ancestor of pindex which is invalid.
    CBlockIndex *pindexFirstMissing = NULL; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex *pindexFirstNeverProcessed = NULL; // Oldest ancestor of pindex for which nTx == 0.
    // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex *pindexFirstNotTreeValid = NULL;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex *pindexFirstNotTransactionsValid = NULL;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex *pindexFirstNotChainValid = NULL;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    CBlockIndex *pindexFirstNotScriptsValid = NULL;
    while (pindex != NULL)
    {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID)
            pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA))
            pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == NULL && pindex->nTx == 0)
            pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE)
            pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTransactionsValid == NULL &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS)
            pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN)
            pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS)
            pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL)
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
        assert((pindexFirstNeverProcessed != NULL) == (pindex->nChainTx == 0));
        assert((pindexFirstNotTransactionsValid != NULL) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork);
        // The pskip pointer must point back for all but the first 2 blocks.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight)));
        assert(pindexFirstNotTreeValid == NULL); // All mapBlockIndex entries must at least be TREE valid
        // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE)
            assert(pindexFirstNotTreeValid == NULL);
        // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN)
            assert(pindexFirstNotChainValid == NULL);
        // SCRIPTS valid implies all parents are SCRIPTS valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS)
            assert(pindexFirstNotScriptsValid == NULL);
        if (pindexFirstInvalid == NULL)
        {
            // Checks for not-invalid blocks.
            // The failed mask cannot be set for blocks without invalid parents.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0);
        }

        /*  This section does not apply to PV since blocks can arrive and be processed in potentially any order.
            Leaving the commented section for now for further review.
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstNeverProcessed == NULL) {
            if (pindexFirstInvalid == NULL) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.

                // PV:  this is no longer true under certain condition for PV - leaving it in here for further review
                // BU: if the chain is excessive it won't be on the list of active chain candidates
                //if ((!chainContainsExcessive(pindex)) && (pindexFirstMissing == NULL || pindex == chainActive.Tip()) )
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
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != NULL &&
            pindexFirstInvalid == NULL)
        {
            // If this block has block data available, some parent was never received, and has no invalid parents, it
            // must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (!(pindex->nStatus & BLOCK_HAVE_DATA))
            assert(!foundInUnlinked);
        // BU: blocks that are excessive are placed in the unlinked map
        if ((pindexFirstMissing == NULL) && (!chainContainsExcessive(pindex)))
        {
            assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == NULL &&
            pindexFirstMissing != NULL)
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
                if (pindexFirstInvalid == NULL)
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
                pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing)
                pindexFirstMissing = NULL;
            if (pindex == pindexFirstNeverProcessed)
                pindexFirstNeverProcessed = NULL;
            if (pindex == pindexFirstNotTreeValid)
                pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotTransactionsValid)
                pindexFirstNotTransactionsValid = NULL;
            if (pindex == pindexFirstNotChainValid)
                pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid)
                pindexFirstNotScriptsValid = NULL;
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


bool AlreadyHaveTx(const CInv &inv)
{
    // Make checks for anything that requires cs_recentRejects
    {
        LOCK(cs_recentRejects);
        DbgAssert(recentRejects, return false);
        if (chainActive.Tip()->GetBlockHash() != hashRecentRejectsChainTip)
        {
            // If the chain tip has changed previously rejected transactions
            // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
            // or a double-spend. Reset the rejects filter and give those
            // txs a second chance.
            hashRecentRejectsChainTip = chainActive.Tip()->GetBlockHash();
            if (recentRejects)
            {
                recentRejects->reset();
            }
            else
            {
                recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));
            }
        }
        if (txn_recently_in_block->contains(inv.hash))
            return true;
        if (recentRejects->contains(inv.hash))
            return true;
    }

    // Both these require either the mempool.cs or orphanpool.cs locks so we do them outside the scope
    // of cs_recentRejects so we don't have to worry about locking orders.
    return mempool.exists(inv.hash) || orphanpool.AlreadyHaveOrphan(inv.hash);
}

bool AlreadyHaveBlock(const CInv &inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
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

void static ProcessGetData(CNode *pfrom, const Consensus::Params &consensusParams)
{
    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

    std::vector<CInv> vNotFound;

    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end())
    {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        const CInv &inv = *it;
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK || inv.type == MSG_THINBLOCK)
            {
                bool fSend = false;
                BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    if (chainActive.Contains(mi->second))
                    {
                        fSend = true;
                    }
                    else
                    {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a month older (both in time, and in
                        // best equivalent proof of work) than the best header chain we know about.
                        fSend = mi->second->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) &&
                                (pindexBestHeader->GetBlockTime() - mi->second->GetBlockTime() < nOneMonth) &&
                                (GetBlockProofEquivalentTime(
                                     *pindexBestHeader, *mi->second, *pindexBestHeader, consensusParams) < nOneMonth);
                        if (!fSend)
                        {
                            LOGA("%s: ignoring request from peer=%i for old block that isn't in the main chain\n",
                                __func__, pfrom->GetId());
                        }
                        else
                        { // BU: don't relay excessive blocks
                            if (mi->second->nStatus & BLOCK_EXCESSIVE)
                                fSend = false;
                            if (!fSend)
                                LOGA("%s: ignoring request from peer=%i for excessive block of height %d not on "
                                     "the main chain\n",
                                    __func__, pfrom->GetId(), mi->second->nHeight);
                        }
                        // BU: in the future we can throttle old block requests by setting send=false if we are out of
                        // bandwidth
                    }
                }
                // disconnect node in case we have reached the outbound limit for serving historical blocks
                // never disconnect whitelisted nodes
                static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
                if (fSend && CNode::OutboundTargetReached(true) &&
                    (((pindexBestHeader != NULL) &&
                         (pindexBestHeader->GetBlockTime() - mi->second->GetBlockTime() > nOneWeek)) ||
                        inv.type == MSG_FILTERED_BLOCK) &&
                    !pfrom->fWhitelisted)
                {
                    LOG(NET, "historical block serving limit reached, disconnect peer %s\n", pfrom->GetLogName());

                    // disconnect node
                    pfrom->fDisconnect = true;
                    fSend = false;
                }
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (fSend && (mi->second->nStatus & BLOCK_HAVE_DATA))
                {
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, (*mi).second, consensusParams))
                    {
                        // its possible that I know about it but haven't stored it yet
                        LOG(THIN, "unable to load block %s from disk\n",
                            (*mi).second->phashBlock ? (*mi).second->phashBlock->ToString() : "");
                        // no response
                    }
                    else
                    {
                        if (inv.type == MSG_BLOCK)
                        {
                            pfrom->blocksSent += 1;
                            pfrom->PushMessage(NetMsgType::BLOCK, block);
                        }
                        else if (inv.type == MSG_THINBLOCK)
                        {
                            LOG(THIN, "Sending thinblock by INV queue getdata message\n");
                            SendXThinBlock(MakeBlockRef(block), pfrom, inv);
                        }
                        else // MSG_FILTERED_BLOCK)
                        {
                            LOCK(pfrom->cs_filter);
                            if (pfrom->pfilter)
                            {
                                CMerkleBlock merkleBlock(block, *pfrom->pfilter);
                                pfrom->PushMessage(NetMsgType::MERKLEBLOCK, merkleBlock);
                                pfrom->blocksSent += 1;
                                // CMerkleBlock just contains hashes, so also push any transactions in the block the
                                // client did not see
                                // This avoids hurting performance by pointlessly requiring a round-trip
                                // Note that there is currently no way for a node to request any single transactions we
                                // didn't send here -
                                // they must either disconnect and retry or request the full block.
                                // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                                // however we MUST always provide at least what the remote peer needs
                                typedef std::pair<unsigned int, uint256> PairType;
                                for (PairType &pair : merkleBlock.vMatchedTxn)
                                {
                                    pfrom->txsSent += 1;
                                    pfrom->PushMessage(NetMsgType::TX, block.vtx[pair.first]);
                                }
                            }
                            // else
                            // no response
                        }

                        // Trigger the peer node to send a getblocks request for the next batch of inventory
                        if (inv.hash == pfrom->hashContinue)
                        {
                            // Bypass PushInventory, this must send even if redundant,
                            // and we want it right after the last block so they don't
                            // wait for other stuff first.
                            std::vector<CInv> vInv;
                            vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                            pfrom->PushMessage(NetMsgType::INV, vInv);
                            pfrom->hashContinue.SetNull();
                        }
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool fPushed = false;
                {
                    CTransactionRef ptx;

                    // We need to release this lock before push message. There is a potential deadlock because
                    // cs_vSend is often taken before cs_mapRelay
                    {
                        LOCK(cs_mapRelay);
                        std::map<CInv, CTransactionRef>::iterator mi = mapRelay.find(inv);
                        if (mi != mapRelay.end())
                        {
                            // Copy shared ptr to second because it may be deleted once lock is released
                            ptx = (*mi).second;
                            fPushed = true;
                        }
                    }

                    if (fPushed)
                    {
                        pfrom->PushMessage(inv.GetCommand(), ptx);
                    }
                }
                if (!fPushed && inv.type == MSG_TX)
                {
                    CTransactionRef ptx = nullptr;
                    ptx = mempool.get(inv.hash);
                    if (ptx)
                    {
                        pfrom->PushMessage(NetMsgType::TX, ptx);
                        fPushed = true;
                        pfrom->txsSent += 1;
                    }
                }
                if (!fPushed)
                {
                    vNotFound.push_back(inv);
                }
            }

            // Track requests for our stuff.
            GetMainSignals().Inventory(inv.hash);

            // We only want to process one of these message types before returning. These are high
            // priority messages and we don't want to sit here processing a large number of messages
            // while we hold the cs_main lock, but rather allow these messages to be sent first and
            // process the return message before potentially reading from the queue again.
            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK || inv.type == MSG_THINBLOCK)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty())
    {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage(NetMsgType::NOTFOUND, vNotFound);
    }
}

bool ProcessMessage(CNode *pfrom, std::string strCommand, CDataStream &vRecv, int64_t nTimeReceived)
{
    int64_t receiptTime = GetTime();
    const CChainParams &chainparams = Params();
    RandAddSeedPerfmon();
    unsigned int msgSize = vRecv.size(); // BU for statistics
    UpdateRecvStats(pfrom, strCommand, msgSize, nTimeReceived);
    LOG(NET, "received: %s (%u bytes) peer=%s\n", SanitizeString(strCommand), msgSize, pfrom->GetLogName());
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LOGA("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (!(nLocalServices & NODE_BLOOM) &&
        (strCommand == NetMsgType::FILTERLOAD || strCommand == NetMsgType::FILTERADD ||
            strCommand == NetMsgType::FILTERCLEAR))
    {
        if (pfrom->nVersion >= NO_BLOOM_VERSION)
        {
            dosMan.Misbehaving(pfrom, 100);
            return false;
        }
        else
        {
            LOG(NET, "Inconsistent bloom filter settings peer %s\n", pfrom->GetLogName());
            pfrom->fDisconnect = true;
            return false;
        }
    }


    if (strCommand == NetMsgType::VERSION)
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage(
                NetMsgType::REJECT, strCommand, REJECT_DUPLICATE, std::string("Duplicate version message"));
            pfrom->fDisconnect = true;
            return error("Duplicate version message received - disconnecting peer=%s version=%s", pfrom->GetLogName(),
                pfrom->cleanSubVer);
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;

        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
        {
            // ban peers older than this proto version
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                strprintf("Protocol Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            dosMan.Misbehaving(pfrom, 100);
            return error("Using obsolete protocol version %i - banning peer=%s version=%s", pfrom->nVersion,
                pfrom->GetLogName(), pfrom->cleanSubVer);
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty())
        {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LOGA("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));

        // Send VERACK handshake message
        pfrom->PushMessage(NetMsgType::VERACK);
        pfrom->fVerackSent = true;

        // Change version
        pfrom->ssSend.SetVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                FastRandomContext insecure_rand;
                if (addr.IsRoutable())
                {
                    LOG(NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                }
                else if (IsPeerAddrLocalGood(pfrom))
                {
                    addr.SetIP(pfrom->addrLocal);
                    LOG(NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage(NetMsgType::GETADDR);
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        }
        else
        {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        LOG(NET, "receive version message: %s: version %d, blocks=%d, us=%s, peer=%s\n", pfrom->cleanSubVer,
            pfrom->nVersion, pfrom->nStartingHeight, addrMe.ToString(), pfrom->GetLogName());

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        AddTimeData(pfrom->addr, nTimeOffset);

        // Feeler connections exist only to verify if address is online.
        if (pfrom->fFeeler)
        {
            // Should never occur but if it does correct the value.
            // We can't have an inbound "feeler" connection, so the value must be improperly set.
            DbgAssert(pfrom->fInbound == false, pfrom->fFeeler = false);
            if (pfrom->fInbound == false)
            {
                LOG(NET, "Disconnecting feeler to peer %s\n", pfrom->GetLogName());
                pfrom->fDisconnect = true;
            }
        }
    }


    else if (pfrom->nVersion == 0 && !pfrom->fWhitelisted)
    {
        // Must have version message before anything else (Although we may send our VERSION before
        // we receive theirs, it would not be possible to receive their VERACK before their VERSION).
        pfrom->fDisconnect = true;
        return error("%s receieved before VERSION message - disconnecting peer=%s", strCommand, pfrom->GetLogName());
    }


    else if (strCommand == NetMsgType::VERACK)
    {
        // If we haven't sent a VERSION message yet then we should not get a VERACK message.
        if (pfrom->tVersionSent < 0)
        {
            pfrom->fDisconnect = true;
            return error("VERACK received but we never sent a VERSION message - disconnecting peer=%s version=%s",
                pfrom->GetLogName(), pfrom->cleanSubVer);
        }
        if (pfrom->fSuccessfullyConnected)
        {
            pfrom->fDisconnect = true;
            return error("duplicate VERACK received - disconnecting peer=%s version=%s", pfrom->GetLogName(),
                pfrom->cleanSubVer);
        }

        pfrom->fSuccessfullyConnected = true;
        pfrom->SetRecvVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode)
            pfrom->fCurrentlyConnected = true;

        if (pfrom->nVersion >= SENDHEADERS_VERSION)
        {
            // Tell our peer we prefer to receive headers rather than inv's
            // We send this to non-NODE NETWORK peers as well, because even
            // non-NODE NETWORK peers can announce blocks (such as pruning
            // nodes)

            pfrom->PushMessage(NetMsgType::SENDHEADERS);
        }

        // Tell the peer what maximum xthin bloom filter size we will consider acceptable.
        if (pfrom->ThinBlockCapable() && IsThinBlocksEnabled())
        {
            pfrom->PushMessage(NetMsgType::FILTERSIZEXTHIN, nXthinBloomFilterSize);
        }

        // BU expedited procecessing requires the exchange of the listening port id but we have to send it in a separate
        // version
        // message because we don't know if in the future Core will append more data to the end of the current VERSION
        // message.
        // The BUVERSION should be after the VERACK message otherwise Core may flag an error if another messaged shows
        // up before the VERACK is received.
        // The BUVERSION message is active from the protocol EXPEDITED_VERSION onwards.
        if (pfrom->nVersion >= EXPEDITED_VERSION)
        {
            pfrom->PushMessage(NetMsgType::BUVERSION, GetListenPort());
            pfrom->fBUVersionSent = true;
        }
    }


    else if (!pfrom->fSuccessfullyConnected && GetTime() - pfrom->tVersionSent > VERACK_TIMEOUT &&
             pfrom->tVersionSent >= 0)
    {
        // If verack is not received within timeout then disconnect.
        // The peer may be slow so disconnect them only, to give them another chance if they try to re-connect.
        // If they are a bad peer and keep trying to reconnect and still do not VERACK, they will eventually
        // get banned by the connection slot algorithm which tracks disconnects and reconnects.
        pfrom->fDisconnect = true;
        LOG(NET, "ERROR: disconnecting - VERACK not received within %d seconds for peer=%s version=%s\n",
            VERACK_TIMEOUT, pfrom->GetLogName(), pfrom->cleanSubVer);

        // update connection tracker which is used by the connection slot algorithm.
        LOCK(cs_mapInboundConnectionTracker);
        CNetAddr ipAddress = (CNetAddr)pfrom->addr;
        mapInboundConnectionTracker[ipAddress].nEvictions += 1;
        mapInboundConnectionTracker[ipAddress].nLastEvictionTime = GetTime();

        return true; // return true so we don't get any process message failures in the log.
    }


    else if (strCommand == NetMsgType::ADDR)
    {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        FastRandomContext insecure_rand;
        for (CAddress &addr : vAddr)
        {
            boost::this_thread::interruption_point();

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(
                        UintToArith256(hashSalt) ^ (hashAddr << 32) ^ ((GetTime() + hashAddr) / (24 * 60 * 60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    std::multimap<uint256, CNode *> mapMix;
                    for (CNode *pnode : vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(std::make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (std::multimap<uint256, CNode *>::iterator mi = mapMix.begin();
                         mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr, insecure_rand);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
        {
            LOG(NET, "Disconnecting %s: one shot\n", pfrom->GetLogName());
            pfrom->fDisconnect = true;
        }
    }

    else if (strCommand == NetMsgType::SENDHEADERS)
    {
        LOCK(cs_main);
        State(pfrom->GetId())->fPreferHeaders = true;
    }

    else if (strCommand == NetMsgType::INV)
    {
        if (fImporting || fReindex)
            return true;

        std::vector<CInv> vInv;
        vRecv >> vInv;

        // Message Consistency Checking
        //   Check size == 0 to be intolerant of an empty and useless request.
        //   Validate that INVs are a valid type and not null.
        if (vInv.size() > MAX_INV_SZ || vInv.empty())
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("message inv size() = %u", vInv.size());
        }

        bool fBlocksOnly = GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);

        // Allow whitelisted peers to send data other than blocks in blocks only mode if whitelistrelay is true
        if (pfrom->fWhitelisted && GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY))
            fBlocksOnly = false;

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            boost::this_thread::interruption_point();

            const CInv &inv = vInv[nInv];
            if (!((inv.type == MSG_TX) || (inv.type == MSG_BLOCK)) || inv.hash.IsNull())
            {
                dosMan.Misbehaving(pfrom, 20);
                return error("message inv invalid type = %u or is null hash %s", inv.type, inv.hash.ToString());
            }

            if (inv.type == MSG_BLOCK)
            {
                LOCK(cs_main);
                bool fAlreadyHaveBlock = AlreadyHaveBlock(inv);
                LOG(NET, "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHaveBlock ? "have" : "new", pfrom->id);

                requester.UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                // RE !IsInitialBlockDownload(): We do not want to get the block if the system is executing the initial
                // block download because
                // blocks are stored in block files in the order of arrival.  So grabbing blocks "early" will cause new
                // blocks to be sprinkled
                // throughout older block files.  This will stop those files from being pruned.
                // !IsInitialBlockDownload() can be removed if
                // a better block storage system is devised.
                if ((!fAlreadyHaveBlock && !IsInitialBlockDownload()) ||
                    (!fAlreadyHaveBlock && Params().NetworkIDString() == "regtest"))
                {
                    // Since we now only rely on headers for block requests, if we get an INV from an older node or
                    // if there was a very large re-org which resulted in a revert to block announcements via INV,
                    // we will instead request the header rather than the block.  This is safer and prevents an
                    // attacker from sending us fake INV's for blocks that do not exist or try to get us to request
                    // and download fake blocks.
                    pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexBestHeader), inv.hash);
                }
                else
                {
                    LOG(NET, "skipping request of block %s.  already have: %d  importing: %d  reindex: %d  "
                             "isChainNearlySyncd: %d\n",
                        inv.hash.ToString(), fAlreadyHaveBlock, fImporting, fReindex, IsChainNearlySyncd());
                }
            }
            else // If we get here then inv.type must == MSG_TX.
            {
                bool fAlreadyHaveTx = AlreadyHaveTx(inv);
                LOG(NET, "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHaveTx ? "have" : "new", pfrom->id);

                pfrom->AddInventoryKnown(inv);
                if (fBlocksOnly)
                {
                    LOG(NET, "transaction (%s) inv sent in violation of protocol peer=%d\n", inv.hash.ToString(),
                        pfrom->id);
                }
                // RE !IsInitialBlockDownload(): during IBD, its a waste of bandwidth to grab transactions, they will
                // likely be included in blocks that we IBD download anyway.  This is especially important as
                // transaction volumes increase.
                else if (!fAlreadyHaveTx && !IsInitialBlockDownload())
                    requester.AskFor(inv, pfrom);
            }

            // Track requests for our stuff.
            GetMainSignals().Inventory(inv.hash);

            if (pfrom->nSendSize > (SendBufferSize() * 2))
            {
                dosMan.Misbehaving(pfrom, 50);
                return error("send buffer size() = %u", pfrom->nSendSize);
            }
        }
    }


    else if (strCommand == NetMsgType::GETDATA)
    {
        if (fImporting || fReindex)
            return true;

        std::vector<CInv> vInv;
        vRecv >> vInv;
        // BU check size == 0 to be intolerant of an empty and useless request
        if ((vInv.size() > MAX_INV_SZ) || (vInv.size() == 0))
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("message getdata size() = %u", vInv.size());
        }

        // Validate that INVs are a valid type
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];
            if (!((inv.type == MSG_TX) || (inv.type == MSG_BLOCK) || (inv.type == MSG_FILTERED_BLOCK) ||
                    (inv.type == MSG_THINBLOCK)))
            {
                dosMan.Misbehaving(pfrom, 20);
                return error("message inv invalid type = %u", inv.type);
            }
            // inv.hash does not need validation, since SHA2556 hash can be any value
        }


        if (fDebug || (vInv.size() != 1))
            LOG(NET, "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);

        if ((fDebug && vInv.size() > 0) || (vInv.size() == 1))
            LOG(NET, "received getdata for: %s peer=%d\n", vInv[0].ToString(), pfrom->id);

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom, chainparams.GetConsensus());
    }


    else if (strCommand == NetMsgType::GETBLOCKS)
    {
        if (fImporting || fReindex)
            return true;

        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex *pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LOG(NET, "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1),
            hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LOG(NET, "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            // If pruning, don't inv blocks unless we have on disk and are likely to still have
            // for some reasonable time window (1 hour) that block relay might require.
            const int nPrunedBlocksLikelyToHave =
                MIN_BLOCKS_TO_KEEP - 3600 / chainparams.GetConsensus().nPowTargetSpacing;
            if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) ||
                                  pindex->nHeight <= chainActive.Tip()->nHeight - nPrunedBlocksLikelyToHave))
            {
                LOG(NET, " getblocks stopping, pruned or too old block at %d %s\n", pindex->nHeight,
                    pindex->GetBlockHash().ToString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LOG(NET, "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }


    else if (strCommand == NetMsgType::GETHEADERS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);
        CNodeState *nodestate = State(pfrom->GetId());
        CBlockIndex *pindex = NULL;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            BlockMap::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = (*mi).second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chainActive, locator);
            if (pindex)
                pindex = chainActive.Next(pindex);
        }

        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        std::vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        LOG(NET, "getheaders height %d for block %s from peer %s\n", (pindex ? pindex->nHeight : -1),
            hashStop.ToString(), pfrom->GetLogName());
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        // pindex can be NULL either if we sent chainActive.Tip() OR
        // if our peer has chainActive.Tip() (and thus we are sending an empty
        // headers message). In both cases it's safe to update
        // pindexBestHeaderSent to be our tip.
        nodestate->pindexBestHeaderSent = pindex ? pindex : chainActive.Tip();
        pfrom->PushMessage(NetMsgType::HEADERS, vHeaders);
    }


    else if (strCommand == NetMsgType::TX)
    {
        // Stop processing the transaction early if
        // We are in blocks only mode and peer is either not whitelisted or whitelistrelay is off
        if (GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY) &&
            (!pfrom->fWhitelisted || !GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY)))
        {
            LOG(NET, "transaction sent in violation of protocol peer=%d\n", pfrom->id);
            return true;
        }

        std::vector<uint256> vWorkQueue;
        std::vector<uint256> vEraseQueue;
        CTransactionRef ptx;
        vRecv >> ptx;

        CInv inv(MSG_TX, ptx->GetHash());
        pfrom->AddInventoryKnown(inv);
        requester.Received(inv, pfrom, msgSize);

        LOCK(cs_main);

        bool fMissingInputs = false;
        CValidationState state;

        // Check for recently rejected (and do other quick existence checks)
        if (!AlreadyHaveTx(inv) && AcceptToMemoryPool(mempool, state, ptx, true, &fMissingInputs))
        {
            mempool.check(pcoinsTip);
            RelayTransaction(ptx);
            vWorkQueue.push_back(inv.hash);

            LOG(MEMPOOL, "AcceptToMemoryPool: peer=%d: accepted %s (poolsz %u txn, %u kB)\n", pfrom->id,
                ptx->GetHash().ToString(), mempool.size(), mempool.DynamicMemoryUsage() / 1000);

            // Recursively process any orphan transactions that depended on this one
            LOCK(orphanpool.cs);
            std::set<NodeId> setMisbehaving;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                std::map<uint256, std::set<uint256> >::iterator itByPrev =
                    orphanpool.mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
                if (itByPrev == orphanpool.mapOrphanTransactionsByPrev.end())
                    continue;
                for (std::set<uint256>::iterator mi = itByPrev->second.begin(); mi != itByPrev->second.end(); ++mi)
                {
                    const uint256 &orphanHash = *mi;

                    // Make sure we actually have an entry on the orphan cache. While this should never fail because
                    // we always erase orphans and any mapOrphanTransactionsByPrev at the same time, still we need to
                    // be sure.
                    bool fOk = true;
                    DbgAssert(orphanpool.mapOrphanTransactions.count(orphanHash), fOk = false);
                    if (!fOk)
                        continue;

                    const CTransactionRef pOrphanTx = orphanpool.mapOrphanTransactions[orphanHash].ptx;
                    NodeId fromPeer = orphanpool.mapOrphanTransactions[orphanHash].fromPeer;
                    bool fMissingInputs2 = false;
                    // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                    // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                    // anyone relaying LegitTxX banned)
                    CValidationState stateDummy;


                    if (setMisbehaving.count(fromPeer))
                        continue;
                    if (AcceptToMemoryPool(mempool, stateDummy, pOrphanTx, true, &fMissingInputs2))
                    {
                        LOG(MEMPOOL, "   accepted orphan tx %s\n", orphanHash.ToString());
                        RelayTransaction(pOrphanTx);
                        vWorkQueue.push_back(orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    }
                    else if (!fMissingInputs2)
                    {
                        int nDos = 0;
                        if (stateDummy.IsInvalid(nDos) && nDos > 0)
                        {
                            // Punish peer that gave us an invalid orphan tx
                            dosMan.Misbehaving(fromPeer, nDos);
                            setMisbehaving.insert(fromPeer);
                            LOG(MEMPOOL, "   invalid orphan tx %s\n", orphanHash.ToString());
                        }
                        // Has inputs but not accepted to mempool
                        // Probably non-standard or insufficient fee/priority
                        LOG(MEMPOOL, "   removed orphan tx %s\n", orphanHash.ToString());
                        vEraseQueue.push_back(orphanHash);
                        if (recentRejects)
                            recentRejects->insert(orphanHash); // should always be true
                    }
                    mempool.check(pcoinsTip);
                }
            }
            for (uint256 &hash : vEraseQueue)
                orphanpool.EraseOrphanTx(hash);

            //  BU: Xtreme thinblocks - purge orphans that are too old
            orphanpool.EraseOrphansByTime();
        }
        else if (fMissingInputs)
        {
            // If we've forked and this is probably not a valid tx, then skip adding it to the orphan pool
            if (!IsUAHFforkActiveOnNextBlock(chainActive.Tip()->nHeight) || IsTxProbablyNewSigHash(*ptx))
            {
                LOCK(orphanpool.cs);
                orphanpool.AddOrphanTx(ptx, pfrom->GetId());

                // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
                static unsigned int nMaxOrphanTx =
                    (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
                static uint64_t nMaxOrphanPoolSize =
                    (uint64_t)std::max((int64_t)0, (GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000 / 10));
                unsigned int nEvicted = orphanpool.LimitOrphanTxSize(nMaxOrphanTx, nMaxOrphanPoolSize);
                if (nEvicted > 0)
                    LOG(MEMPOOL, "mapOrphan overflow, removed %u tx\n", nEvicted);
            }
        }
        else
        {
            if (recentRejects)
                recentRejects->insert(ptx->GetHash()); // should always be true

            if (pfrom->fWhitelisted && GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY))
            {
                // Always relay transactions received from whitelisted peers, even
                // if they were already in the mempool or rejected from it due
                // to policy, allowing the node to function as a gateway for
                // nodes hidden behind it.
                //
                // Never relay transactions that we would assign a non-zero DoS
                // score for, as we expect peers to do the same with us in that
                // case.
                int nDoS = 0;
                if (!state.IsInvalid(nDoS) || nDoS == 0)
                {
                    LOGA("Force relaying tx %s from whitelisted peer=%d\n", ptx->GetHash().ToString(), pfrom->id);
                    RelayTransaction(ptx);
                }
                else
                {
                    LOGA("Not relaying invalid transaction %s from whitelisted peer=%d (%s)\n",
                        ptx->GetHash().ToString(), pfrom->id, FormatStateMessage(state));
                }
            }
        }
        int nDoS = 0;
        if (state.IsInvalid(nDoS))
        {
            LOG(MEMPOOLREJ, "%s from peer=%d was not accepted: %s\n", ptx->GetHash().ToString(), pfrom->id,
                FormatStateMessage(state));
            if (state.GetRejectCode() < REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
                pfrom->PushMessage(NetMsgType::REJECT, strCommand, (unsigned char)state.GetRejectCode(),
                    state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0)
            {
                dosMan.Misbehaving(pfrom, nDoS);
            }
        }
        FlushStateToDisk(state, FLUSH_STATE_PERIODIC);

        // The flush to disk above is only periodic therefore we need to continuously trim any excess from the cache.
        pcoinsTip->Trim(nCoinCacheMaxSize);
    }


    else if (strCommand == NetMsgType::HEADERS) // Ignore headers received while importing
    {
        if (fImporting)
        {
            LOG(NET, "skipping processing of HEADERS because importing\n");
            return true;
        }
        if (fReindex)
        {
            LOG(NET, "skipping processing of HEADERS because reindexing\n");
            return true;
        }
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS)
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++)
        {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        LOCK(cs_main);

        // Nothing interesting. Stop asking this peers for more headers.
        if (nCount == 0)
            return true;

        // Check all headers to make sure they are continuous before attempting to accept them.
        // This prevents and attacker from keeping us from doing direct fetch by giving us out
        // of order headers.
        bool fNewUnconnectedHeaders = false;
        uint256 hashLastBlock;
        hashLastBlock.SetNull();
        for (const CBlockHeader &header : headers)
        {
            // check that the first header has a previous block in the blockindex.
            if (hashLastBlock.IsNull())
            {
                BlockMap::iterator mi = mapBlockIndex.find(header.hashPrevBlock);
                if (mi != mapBlockIndex.end())
                    hashLastBlock = header.hashPrevBlock;
            }

            // Add this header to the map if it doesn't connect to a previous header
            if (header.hashPrevBlock != hashLastBlock)
            {
                // If we still haven't finished downloading the initial headers during node sync and we get
                // an out of order header then we must disconnect the node so that we can finish downloading
                // initial headers from a diffeent peer. An out of order header at this point is likely an attack
                // to prevent the node from syncing.
                if (header.GetBlockTime() < GetAdjustedTime() - 24 * 60 * 60)
                {
                    pfrom->fDisconnect = true;
                    return error("non-continuous-headers sequence during node sync - disconnecting peer=%s",
                        pfrom->GetLogName());
                }
                fNewUnconnectedHeaders = true;
            }

            // if we have an unconnected header then add every following header to the unconnected headers cache.
            if (fNewUnconnectedHeaders)
            {
                uint256 hash = header.GetHash();
                if (mapUnConnectedHeaders.size() < MAX_UNCONNECTED_HEADERS)
                    mapUnConnectedHeaders[hash] = std::make_pair(header, GetTime());

                // update hashLastUnknownBlock so that we'll be able to download the block from this peer even
                // if we receive the headers, which will connect this one, from a different peer.
                requester.UpdateBlockAvailability(pfrom->GetId(), hash);
            }

            hashLastBlock = header.GetHash();
        }
        // return without error if we have an unconnected header.  This way we can try to connect it when the next
        // header arrives.
        if (fNewUnconnectedHeaders)
            return true;

        // If possible add any previously unconnected headers to the headers vector and remove any expired entries.
        std::map<uint256, std::pair<CBlockHeader, int64_t> >::iterator mi = mapUnConnectedHeaders.begin();
        while (mi != mapUnConnectedHeaders.end())
        {
            std::map<uint256, std::pair<CBlockHeader, int64_t> >::iterator toErase = mi;

            // Add the header if it connects to the previous header
            if (headers.back().GetHash() == (*mi).second.first.hashPrevBlock)
            {
                headers.push_back((*mi).second.first);
                mapUnConnectedHeaders.erase(toErase);

                // if you found one to connect then search from the beginning again in case there is another
                // that will connect to this new header that was added.
                mi = mapUnConnectedHeaders.begin();
                continue;
            }

            // Remove any entries that have been in the cache too long.  Unconnected headers should only exist
            // for a very short while, typically just a second or two.
            int64_t nTimeHeaderArrived = (*mi).second.second;
            uint256 headerHash = (*mi).first;
            mi++;
            if (GetTime() - nTimeHeaderArrived >= UNCONNECTED_HEADERS_TIMEOUT)
            {
                mapUnConnectedHeaders.erase(toErase);
            }
            // At this point we know the headers in the list received are known to be in order, therefore,
            // check if the header is equal to some other header in the list. If so then remove it from the cache.
            else
            {
                for (const CBlockHeader &header : headers)
                {
                    if (header.GetHash() == headerHash)
                    {
                        mapUnConnectedHeaders.erase(toErase);
                        break;
                    }
                }
            }
        }

        // Check and accept each header in dependency order (oldest block to most recent)
        CBlockIndex *pindexLast = nullptr;
        int i = 0;
        for (const CBlockHeader &header : headers)
        {
            CValidationState state;
            if (!AcceptBlockHeader(header, state, chainparams, &pindexLast))
            {
                int nDos;
                if (state.IsInvalid(nDos))
                {
                    if (nDos > 0)
                    {
                        dosMan.Misbehaving(pfrom, nDos);
                    }
                }
                // all headers from this one forward reference a fork that we don't follow, so erase them
                headers.erase(headers.begin() + i, headers.end());
                nCount = headers.size();
                break;
            }
            else
                PV->UpdateMostWorkOurFork(header);

            i++;
        }

        if (pindexLast)
            requester.UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        if (nCount == MAX_HEADERS_RESULTS && pindexLast)
        {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            LOG(NET, "more getheaders (%d) to end to peer=%s (startheight:%d)\n", pindexLast->nHeight,
                pfrom->GetLogName(), pfrom->nStartingHeight);
            pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexLast), uint256());

            {
                CNodeState *state = State(pfrom->GetId());
                DbgAssert(state != nullptr, );
                if (state)
                    state->nSyncStartTime = GetTime(); // reset the time because more headers needed
            }

            // During the process of IBD we need to update block availability for every connected peer. To do that we
            // request, from each NODE_NETWORK peer, a header that matches the last blockhash found in this recent set
            // of headers. Once the reqeusted header is received then the block availability for this peer will get
            // updated.
            if (IsInitialBlockDownload())
            {
                // To maintain locking order with cs_main we have to addrefs for each node and then release
                // the lock on cs_vNodes before aquiring cs_main further down.
                std::vector<CNode *> vNodesCopy;
                {
                    LOCK(cs_vNodes);
                    vNodesCopy = vNodes;
                    for (CNode *pnode : vNodes)
                    {
                        pnode->AddRef();
                    }
                }

                for (CNode *pnode : vNodesCopy)
                {
                    if (!pnode->fClient && pnode != pfrom)
                    {
                        LOCK(cs_main);
                        CNodeState *state = State(pfrom->GetId());
                        DbgAssert(state != nullptr, ); // do not return, we need to release refs later.
                        if (state == nullptr)
                            continue;

                        if (state->pindexBestKnownBlock == nullptr ||
                            pindexLast->nChainWork > state->pindexBestKnownBlock->nChainWork)
                        {
                            // We only want one single header so we pass a null for CBlockLocator.
                            pnode->PushMessage(NetMsgType::GETHEADERS, CBlockLocator(), pindexLast->GetBlockHash());
                            LOG(NET | BLK, "Requesting header for blockavailability, peer=%s block=%s height=%d\n",
                                pnode->GetLogName(), pindexLast->GetBlockHash().ToString().c_str(),
                                pindexBestHeader->nHeight);
                        }
                    }
                }

                // release refs
                for (CNode *pnode : vNodesCopy)
                    pnode->Release();
            }
        }

        bool fCanDirectFetch = CanDirectFetch(chainparams.GetConsensus());
        CNodeState *nodestate = State(pfrom->GetId());
        DbgAssert(nodestate != nullptr, return false);

        // During the initial peer handshake we must receive the initial headers which should be greater
        // than or equal to our block height at the time of requesting GETHEADERS. This is because the peer has
        // advertised a height >= to our own. Furthermore, because the headers max returned is as much as 2000 this
        // could not be a mainnet re-org.
        if (!nodestate->fFirstHeadersReceived)
        {
            // We want to make sure that the peer doesn't just send us any old valid header. The block height of the
            // last header they send us should be equal to our block height at the time we made the GETHEADERS request.
            if (pindexLast && nodestate->nFirstHeadersExpectedHeight <= pindexLast->nHeight)
            {
                nodestate->fFirstHeadersReceived = true;
                LOG(NET, "Initial headers received for peer=%s\n", pfrom->GetLogName());
            }

            // Allow for very large reorgs (> 2000 blocks) on the nol test chain or other test net.
            if (Params().NetworkIDString() != "main" && Params().NetworkIDString() != "regtest")
                nodestate->fFirstHeadersReceived = true;
        }

        // update the syncd status.  This should come before we make calls to requester.AskFor().
        IsChainNearlySyncdInit();
        IsInitialBlockDownloadInit();

        // If this set of headers is valid and ends in a block with at least as
        // much work as our tip, download as much as possible.
        if (fCanDirectFetch && pindexLast && pindexLast->IsValid(BLOCK_VALID_TREE) &&
            chainActive.Tip()->nChainWork <= pindexLast->nChainWork)
        {
            // Set tweak value.  Mostly used in testing direct fetch.
            if (maxBlocksInTransitPerPeer.Value() != 0)
                pfrom->nMaxBlocksInTransit.store(maxBlocksInTransitPerPeer.Value());

            std::vector<CBlockIndex *> vToFetch;
            CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast.
            while (pindexWalk && !chainActive.Contains(pindexWalk))
            {
                vToFetch.push_back(pindexWalk);
                pindexWalk = pindexWalk->pprev;
            }

            // Download as much as possible, from earliest to latest.
            unsigned int nAskFor = 0;
            for (auto pindex_iter = vToFetch.rbegin(); pindex_iter != vToFetch.rend(); pindex_iter++)
            {
                CBlockIndex *pindex = *pindex_iter;
                // pindex must be nonnull because we populated vToFetch a few lines above
                CInv inv(MSG_BLOCK, pindex->GetBlockHash());
                if (!AlreadyHaveBlock(inv))
                {
                    requester.AskFor(inv, pfrom);
                    LOG(REQ, "AskFor block via headers direct fetch %s (%d) peer=%d\n",
                        pindex->GetBlockHash().ToString(), pindex->nHeight, pfrom->id);
                    nAskFor++;
                }
                // We don't care about how many blocks are in flight.  We just need to make sure we don't
                // ask for more than the maximum allowed per peer because the request manager will take care
                // of any duplicate requests.
                if (nAskFor >= pfrom->nMaxBlocksInTransit.load())
                {
                    LOG(NET, "Large reorg, could only direct fetch %d blocks\n", nAskFor);
                    break;
                }
            }
            if (nAskFor > 1)
            {
                LOG(NET, "Downloading blocks toward %s (%d) via headers direct fetch\n",
                    pindexLast->GetBlockHash().ToString(), pindexLast->nHeight);
            }
        }

        CheckBlockIndex(chainparams.GetConsensus());
    }

    // BUIP010 Xtreme Thinblocks: begin section
    else if (strCommand == NetMsgType::GET_XTHIN && !fImporting && !fReindex && IsThinBlocksEnabled())
    {
        if (!pfrom->ThinBlockCapable())
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("Thinblock message received from a non thinblock node, peer=%d", pfrom->GetId());
        }

        // Check for Misbehaving and DOS
        // If they make more than 20 requests in 10 minutes then disconnect them
        {
            LOCK(cs_vNodes);
            if (pfrom->nGetXthinLastTime <= 0)
                pfrom->nGetXthinLastTime = GetTime();
            uint64_t nNow = GetTime();
            pfrom->nGetXthinCount *= std::pow(1.0 - 1.0 / 600.0, (double)(nNow - pfrom->nGetXthinLastTime));
            pfrom->nGetXthinLastTime = nNow;
            pfrom->nGetXthinCount += 1;
            LOG(THIN, "nGetXthinCount is %f\n", pfrom->nGetXthinCount);
            if (chainparams.NetworkIDString() == "main") // other networks have variable mining rates
            {
                if (pfrom->nGetXthinCount >= 20)
                {
                    dosMan.Misbehaving(pfrom, 100); // If they exceed the limit then disconnect them
                    return error("requesting too many get_xthin");
                }
            }
        }

        CBloomFilter filterMemPool;
        CInv inv;
        vRecv >> inv >> filterMemPool;
        if (!((inv.type == MSG_XTHINBLOCK) || (inv.type == MSG_THINBLOCK)))
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("message inv invalid type = %u", inv.type);
        }

        // Message consistency checking
        if (!((inv.type == MSG_XTHINBLOCK) || (inv.type == MSG_THINBLOCK)) || inv.hash.IsNull())
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("invalid get_xthin type=%u hash=%s", inv.type, inv.hash.ToString());
        }


        // Validates that the filter is reasonably sized.
        LoadFilter(pfrom, &filterMemPool);
        {
            LOCK(cs_main);
            BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
            if (mi == mapBlockIndex.end())
            {
                dosMan.Misbehaving(pfrom, 100);
                return error("Peer %srequested nonexistent block %s", pfrom->GetLogName(), inv.hash.ToString());
            }

            CBlock block;
            const Consensus::Params &consensusParams = Params().GetConsensus();
            if (!ReadBlockFromDisk(block, (*mi).second, consensusParams))
            {
                // We don't have the block yet, although we know about it.
                return error(
                    "Peer %s requested block %s that cannot be read", pfrom->GetLogName(), inv.hash.ToString());
            }
            else
            {
                SendXThinBlock(MakeBlockRef(block), pfrom, inv);
            }
        }
    }


    else if (strCommand == NetMsgType::XPEDITEDREQUEST)
    {
        return HandleExpeditedRequest(vRecv, pfrom);
    }


    else if (strCommand == NetMsgType::XPEDITEDBLK)
    {
        // ignore the expedited message unless we are at the chain tip...
        if (!fImporting && !fReindex && !IsInitialBlockDownload())
        {
            if (!HandleExpeditedBlock(vRecv, pfrom))
            {
                dosMan.Misbehaving(pfrom, 5);
                return false;
            }
        }
    }


    // BUVERSION is used to pass BU specific version information similar to NetMsgType::VERSION
    // and is exchanged after the VERSION and VERACK are both sent and received.
    else if (strCommand == NetMsgType::BUVERSION)
    {
        // If we never sent a VERACK message then we should not get a BUVERSION message.
        if (!pfrom->fVerackSent)
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("BUVERSION received but we never sent a VERACK message - banning peer=%s version=%s",
                pfrom->GetLogName(), pfrom->cleanSubVer);
        }
        // Each connection can only send one version message
        if (pfrom->addrFromPort != 0)
        {
            pfrom->PushMessage(
                NetMsgType::REJECT, strCommand, REJECT_DUPLICATE, std::string("Duplicate BU version message"));
            dosMan.Misbehaving(pfrom, 100);
            return error("Duplicate BU version message received from peer=%s version=%s", pfrom->GetLogName(),
                pfrom->cleanSubVer);
        }

        // addrFromPort is needed for connecting and initializing Xpedited forwarding.
        vRecv >> pfrom->addrFromPort;
        pfrom->PushMessage(NetMsgType::BUVERACK);
    }
    // Final handshake for BU specific version information similar to NetMsgType::VERACK
    else if (strCommand == NetMsgType::BUVERACK)
    {
        // If we never sent a BUVERSION message then we should not get a VERACK message.
        if (!pfrom->fBUVersionSent)
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("BUVERACK received but we never sent a BUVERSION message - banning peer=%s version=%s",
                pfrom->GetLogName(), pfrom->cleanSubVer);
        }

        // This step done after final handshake
        CheckAndRequestExpeditedBlocks(pfrom);
    }

    else if (strCommand == NetMsgType::XTHINBLOCK && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        return CXThinBlock::HandleMessage(vRecv, pfrom, strCommand, 0);
    }


    else if (strCommand == NetMsgType::THINBLOCK && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        return CThinBlock::HandleMessage(vRecv, pfrom);
    }


    else if (strCommand == NetMsgType::GET_XBLOCKTX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        return CXRequestThinBlockTx::HandleMessage(vRecv, pfrom);
    }


    else if (strCommand == NetMsgType::XBLOCKTX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        return CXThinBlockTx::HandleMessage(vRecv, pfrom);
    }
    // BUIP010 Xtreme Thinblocks: end section

    // BUIPXXX Graphene blocks: begin section
    else if (strCommand == NetMsgType::GET_GRAPHENE && !fImporting && !fReindex && IsGrapheneBlockEnabled())
    {
        return HandleGrapheneBlockRequest(vRecv, pfrom, chainparams);
    }

    else if (strCommand == NetMsgType::GRAPHENEBLOCK && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsGrapheneBlockEnabled())
    {
        return CGrapheneBlock::HandleMessage(vRecv, pfrom, strCommand, 0);
    }


    else if (strCommand == NetMsgType::GET_GRAPHENETX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsGrapheneBlockEnabled())
    {
        return CRequestGrapheneBlockTx::HandleMessage(vRecv, pfrom);
    }


    else if (strCommand == NetMsgType::GRAPHENETX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsGrapheneBlockEnabled())
    {
        return CGrapheneBlockTx::HandleMessage(vRecv, pfrom);
    }
    // BUIPXXX Graphene blocks: end section


    else if (strCommand == NetMsgType::BLOCK && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlockRef pblock(new CBlock());
        {
            uint64_t nCheckBlockSize = vRecv.size();
            vRecv >> *pblock;

            // Sanity check. The serialized block size should match the size that is in our receive queue.  If not
            // this could be an attack block of some kind.
            DbgAssert(nCheckBlockSize == pblock->GetBlockSize(), return true);
        }

        CInv inv(MSG_BLOCK, pblock->GetHash());
        LOG(BLK, "received block %s peer=%d\n", inv.hash.ToString(), pfrom->id);
        UnlimitedLogBlock(*pblock, inv.hash.ToString(), receiptTime);

        if (IsChainNearlySyncd()) // BU send the received block out expedited channels quickly
        {
            CValidationState state;
            if (CheckBlockHeader(*pblock, state, true)) // block header is fine
                SendExpeditedBlock(*pblock, pfrom);
        }

        {
            LOCK(cs_main);
            CNodeState *state = State(pfrom->GetId());
            DbgAssert(state != nullptr, );
            if (state)
                state->nSyncStartTime = GetTime(); // reset the getheaders time because block can consume all bandwidth
        }
        pfrom->nPingUsecStart = GetTimeMicros(); // Reset ping time because block can consume all bandwidth

        // Message consistency checking
        // NOTE: consistency checking is handled by checkblock() which is called during
        //       ProcessNewBlock() during HandleBlockMessage.
        PV->HandleBlockMessage(pfrom, strCommand, pblock, inv);
    }


    else if (strCommand == NetMsgType::GETADDR)
    {
        // This asymmetric behavior for inbound and outbound connections was introduced
        // to prevent a fingerprinting attack: an attacker can send specific fake addresses
        // to users' AddrMan and later request them by sending getaddr messages.
        // Making nodes which are behind NAT and can only make outgoing connections ignore
        // the getaddr message mitigates the attack.
        if (!pfrom->fInbound)
        {
            LOG(NET, "Ignoring \"getaddr\" from outbound connection. peer=%d\n", pfrom->id);
            return true;
        }

        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr)
        {
            LOG(NET, "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->id);
            return true;
        }
        pfrom->fSentAddr = true;

        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = addrman.GetAddr();
        FastRandomContext insecure_rand;
        for (const CAddress &addr : vAddr)
            pfrom->PushAddress(addr, insecure_rand);
    }


    else if (strCommand == NetMsgType::MEMPOOL)
    {
        if (CNode::OutboundTargetReached(false) && !pfrom->fWhitelisted)
        {
            LOG(NET, "mempool request with bandwidth limit reached, disconnect peer %s\n", pfrom->GetLogName());
            pfrom->fDisconnect = true;
            return true;
        }
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        std::vector<CInv> vInv;
        for (uint256 &hash : vtxid)
        {
            CInv inv(MSG_TX, hash);
            if (pfrom->pfilter)
            {
                CTransactionRef ptx = nullptr;
                ptx = mempool.get(inv.hash);
                if (ptx == nullptr)
                    continue; // another thread removed since queryHashes, maybe...
                if (!pfrom->pfilter->IsRelevantAndUpdate(*ptx))
                    continue;
            }
            vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ)
            {
                pfrom->PushMessage(NetMsgType::INV, vInv);
                vInv.clear();
            }
        }
        if (vInv.size() > 0)
            pfrom->PushMessage(NetMsgType::INV, vInv);
    }


    else if (strCommand == NetMsgType::PING)
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage(NetMsgType::PONG, nonce);
        }
    }


    else if (strCommand == NetMsgType::PONG)
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce))
        {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0)
            {
                if (nonce == pfrom->nPingNonceSent)
                {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0)
                    {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime, pingUsecTime);
                    }
                    else
                    {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                }
                else
                {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0)
                    {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            }
            else
            {
                sProblem = "Unsolicited pong without ping";
            }
        }
        else
        {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty()))
        {
            LOG(NET, "pong peer=%d: %s, %x expected, %x received, %u bytes\n", pfrom->id, sProblem,
                pfrom->nPingNonceSent, nonce, nAvail);
        }
        if (bPingFinished)
        {
            pfrom->nPingNonceSent = 0;
        }
    }


    else if (strCommand == NetMsgType::FILTERLOAD)
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
        {
            // There is no excuse for sending a too-large filter
            dosMan.Misbehaving(pfrom, 100);
            return false;
        }
        else
        {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
        }
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == NetMsgType::FILTERADD)
    {
        std::vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE)
        {
            dosMan.Misbehaving(pfrom, 100);
        }
        else
        {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                dosMan.Misbehaving(pfrom, 100);
        }
    }


    else if (strCommand == NetMsgType::FILTERCLEAR)
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }

    else if (strCommand == NetMsgType::FILTERSIZEXTHIN)
    {
        if (pfrom->ThinBlockCapable())
        {
            vRecv >> pfrom->nXthinBloomfilterSize;

            // As a safeguard don't allow a smaller max bloom filter size than the default max size.
            if (!pfrom->nXthinBloomfilterSize || (pfrom->nXthinBloomfilterSize < SMALLEST_MAX_BLOOM_FILTER_SIZE))
            {
                pfrom->PushMessage(
                    NetMsgType::REJECT, strCommand, REJECT_INVALID, std::string("filter size was too small"));
                LOG(NET, "Disconnecting %s: bloom filter size too small\n", pfrom->GetLogName());
                pfrom->fDisconnect = true;
                return false;
            }
        }
        else
        {
            pfrom->fDisconnect = true;
            return false;
        }
    }

    else if (strCommand == NetMsgType::REJECT)
    {
        // BU: Request manager: this was restructured to not just be active in fDebug mode so that the request manager
        // can be notified of request rejections.
        try
        {
            std::string strMsg;
            unsigned char ccode;
            std::string strReason;
            uint256 hash;

            vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >>
                LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);
            std::ostringstream ss;
            ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

            // BU: Check request manager reject codes
            if (strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX)
            {
                vRecv >> hash;
                ss << ": hash " << hash.ToString();

                // We need to see this reject message in either "req" or "net" debug mode
                LOG(REQ | NET, "Reject %s\n", SanitizeString(ss.str()));

                if (strMsg == NetMsgType::BLOCK)
                {
                    requester.Rejected(CInv(MSG_BLOCK, hash), pfrom, ccode);
                }
                else if (strMsg == NetMsgType::TX)
                {
                    requester.Rejected(CInv(MSG_TX, hash), pfrom, ccode);
                }
            }
            // if (fDebug) {
            // ostringstream ss;
            // ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

            // if (strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX)
            //  {
            //    ss << ": hash " << hash.ToString();
            //  }
            // LOG(NET, "Reject %s\n", SanitizeString(ss.str()));
            // }
        }
        catch (const std::ios_base::failure &)
        {
            // Avoid feedback loops by preventing reject messages from triggering a new reject message.
            LOG(NET, "Unparseable reject message received\n");
            LOG(REQ, "Unparseable reject message received\n");
        }
    }

    else
    {
        // Ignore unknown commands for extensibility
        LOG(NET, "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->id);
    }

    return true;
}


bool ProcessMessages(CNode *pfrom)
{
    AssertLockHeld(pfrom->cs_vRecvMsg);
    const CChainParams &chainparams = Params();
    // if (fDebug)
    //    LOGA("%s(%u messages)\n", __func__, pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom, chainparams.GetConsensus());

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty())
        return fOk;

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end())
    {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage &msg = *it;

        // if (fDebug)
        //    LOGA("%s(message %u msgsz, %u bytes, complete:%s)\n", __func__,
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Scan for message start
        if (memcmp(msg.hdr.pchMessageStart, pfrom->GetMagic(chainparams), MESSAGE_START_SIZE) != 0)
        {
            LOG(NET, "PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%s\n", SanitizeString(msg.hdr.GetCommand()),
                pfrom->GetLogName());
            if (!pfrom->fWhitelisted)
            {
                dosMan.Ban(pfrom->addr, BanReasonNodeMisbehaving, 4 * 60 * 60); // ban for 4 hours
            }
            fOk = false;
            break;
        }

        // Read header
        CMessageHeader &hdr = msg.hdr;
        if (!hdr.IsValid(pfrom->GetMagic(chainparams)))
        {
            LOGA(
                "PROCESSMESSAGE: ERRORS IN HEADER %s peer=%s\n", SanitizeString(hdr.GetCommand()), pfrom->GetLogName());
            continue;
        }
        std::string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream &vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = ReadLE32((unsigned char *)&hash);
        if (nChecksum != hdr.nChecksum)
        {
            LOGA("%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", __func__,
                SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        }
        catch (const std::ios_base::failure &e)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_MALFORMED, std::string("error parsing message"));
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LOGA("%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than "
                     "its stated length\n",
                    __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LOGA("%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize,
                    e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (const boost::thread_interrupted &)
        {
            throw;
        }
        catch (const std::exception &e)
        {
            PrintExceptionContinue(&e, "ProcessMessages()");
        }
        catch (...)
        {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
            LOGA("%s(%s, %u bytes) FAILED peer %s\n", __func__, SanitizeString(strCommand), nMessageSize,
                pfrom->GetLogName());

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}

static bool CheckForDownloadTimeout(CNode *pto, bool fReceived, int64_t &nRequestTime)
{
    // Use a timeout of 6 times the retry inverval before disconnecting.  This way only a max of 6
    // re-requested thinblocks or graphene blocks could be in memory at any one time.
    if (!fReceived && (GetTime() - nRequestTime) > 6 * blkReqRetryInterval / 1000000)
    {
        if (!pto->fWhitelisted && Params().NetworkIDString() != "regtest")
        {
            LOG(THIN, "ERROR: Disconnecting peer %s due to thinblock download timeout exceeded (%d secs)\n",
                pto->GetLogName(), (GetTime() - nRequestTime));
            pto->fDisconnect = true;
            return true;
        }
    }
    return false;
}

bool SendMessages(CNode *pto)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();
    {
        // First set fDisconnect if appropriate.
        pto->DisconnectIfBanned();

        // Check for an internal disconnect request and if true then set fDisconnect. This would typically happen
        // during initial sync when a peer has a slow connection and we want to disconnect them.  We want to then
        // wait for any blocks that are still in flight before disconnecting, rather than re-requesting them again.
        if (pto->fDisconnectRequest)
        {
            NodeId nodeid = pto->GetId();
            int nInFlight = requester.GetNumBlocksInFlight(nodeid);
            LOG(IBD, "peer %s, checking disconnect request with %d in flight blocks\n", pto->GetLogName(), nInFlight);
            if (nInFlight == 0)
            {
                pto->fDisconnect = true;
                LOG(IBD, "peer %s, disconnect request was set, so disconnected\n", pto->GetLogName());
            }
        }

        // Now exit early if disconnecting or the version handshake is not complete.  We must not send PING or other
        // connection maintenance messages before the handshake is done.
        if (pto->fDisconnect || !pto->fSuccessfullyConnected)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued)
        {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros())
        {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend)
        {
            uint64_t nonce = 0;
            while (nonce == 0)
            {
                GetRandBytes((unsigned char *)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION)
            {
                pto->nPingNonceSent = nonce;
                pto->PushMessage(NetMsgType::PING, nonce);
            }
            else
            {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                pto->PushMessage(NetMsgType::PING);
            }
        }

        // Check to see if there are any thinblocks or graphene blocks in flight that have gone beyond the
        // timeout interval. If so then we need to disconnect them so that the thinblock data is nullified.
        // We could null the associated data here but that would possibly cause a node to be banned later if
        // the thinblock or graphene block finally did show up, so instead we just disconnect this slow node.
        {
            LOCK(pto->cs_mapthinblocksinflight);
            if (!pto->mapThinBlocksInFlight.empty())
            {
                for (auto &item : pto->mapThinBlocksInFlight)
                {
                    if (CheckForDownloadTimeout(pto, item.second.fReceived, item.second.nRequestTime))
                        break;
                }
            }
        }
        {
            LOCK(pto->cs_mapgrapheneblocksinflight);
            if (!pto->mapGrapheneBlocksInFlight.empty())
            {
                for (auto &item : pto->mapGrapheneBlocksInFlight)
                {
                    if (CheckForDownloadTimeout(pto, item.second.fReceived, item.second.nRequestTime))
                        break;
                }
            }
        }

        // Check for block download timeout and disconnect node if necessary. Does not require cs_main.
        int64_t nNow = GetTimeMicros();
        requester.DisconnectOnDownloadTimeout(pto, consensusParams, nNow);

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
        {
            // LOG(NET, "skipping SendMessages to %s, cs_main is locked\n", pto->addr.ToString());
            return true;
        }
        TRY_LOCK(pto->cs_vSend, lockSend);
        if (!lockSend)
        {
            // LOG(NET, "skipping SendMessages to %s, pto->cs_vSend is locked\n", pto->addr.ToString());
            return true;
        }

        // Address refresh broadcast
        if (!IsInitialBlockDownload() && pto->nNextLocalAddrSend < nNow)
        {
            AdvertiseLocal(pto);
            pto->nNextLocalAddrSend = PoissonNextSend(nNow, AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL);
        }

        //
        // Message: addr
        //
        if (pto->nNextAddrSend < nNow)
        {
            pto->nNextAddrSend = PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
            std::vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for (const CAddress &addr : pto->vAddrToSend)
            {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage(NetMsgType::ADDR, vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage(NetMsgType::ADDR, vAddr);
        }

        CNodeState &state = *State(pto->GetId());

        // If a sync has been started check whether we received the first batch of headers requested within the timeout
        // period. If not then disconnect and ban the node and a new node will automatically be selected to start the
        // headers download.
        if ((state.fSyncStarted) && (state.nSyncStartTime < GetTime() - INITIAL_HEADERS_TIMEOUT) &&
            (!state.fFirstHeadersReceived) && !pto->fWhitelisted)
        {
            // pto->fDisconnect = true;
            LOGA("Initial headers were either not received or not received before the timeout\n", pto->GetLogName());
        }

        // Start block sync
        if (pindexBestHeader == nullptr)
            pindexBestHeader = chainActive.Tip();
        // Download if this is a nice peer, or we have no nice peers and this one might do.
        bool fFetch = state.fPreferredDownload || (nPreferredDownload.load() == 0 && !pto->fClient && !pto->fOneShot);
        if (!state.fSyncStarted && !pto->fClient && !fImporting && !fReindex)
        {
            // Only actively request headers from a single peer, unless we're close to today.
            if ((nSyncStarted < MAX_HEADER_REQS_DURING_IBD && fFetch) ||
                chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - SINGLE_PEER_REQUEST_MODE_AGE)
            {
                const CBlockIndex *pindexStart = chainActive.Tip();
                /* If possible, start at the block preceding the currently
                   best known header.  This ensures that we always get a
                   non-empty list of headers back as long as the peer
                   is up-to-date.  With a non-empty response, we can initialise
                   the peer's known best block.  This wouldn't be possible
                   if we requested starting at pindexBestHeader and
                   got back an empty response.  */
                if (pindexStart->pprev)
                    pindexStart = pindexStart->pprev;
                // BU Bug fix for Core:  Don't start downloading headers unless our chain is shorter
                if (pindexStart->nHeight < pto->nStartingHeight)
                {
                    state.fSyncStarted = true;
                    state.nSyncStartTime = GetTime();
                    state.fRequestedInitialBlockAvailability = true;
                    state.nFirstHeadersExpectedHeight = pindexStart->nHeight;
                    nSyncStarted++;

                    LOG(NET, "initial getheaders (%d) to peer=%s (startheight:%d)\n", pindexStart->nHeight,
                        pto->GetLogName(), pto->nStartingHeight);
                    pto->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexStart), uint256());
                }
            }
        }

        // During IBD and when a new NODE_NETWORK peer connects we have to ask for if it has our best header in order
        // to update our block availability. We only want/need to do this only once per peer (if the initial batch of
        // headers has still not been etirely donwnloaded yet then the block availability will be updated during that
        // process rather than here).
        if (IsInitialBlockDownload() && !state.fRequestedInitialBlockAvailability &&
            state.pindexBestKnownBlock == nullptr && !fReindex && !fImporting)
        {
            if (!pto->fClient)
            {
                state.fRequestedInitialBlockAvailability = true;

                // We only want one single header so we pass a null CBlockLocator.
                pto->PushMessage(NetMsgType::GETHEADERS, CBlockLocator(), pindexBestHeader->GetBlockHash());
                LOG(NET | BLK, "Requesting header for initial blockavailability, peer=%s block=%s height=%d\n",
                    pto->GetLogName(), pindexBestHeader->GetBlockHash().ToString().c_str(), pindexBestHeader->nHeight);
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fImporting && !IsInitialBlockDownload())
        {
            GetMainSignals().Broadcast(nTimeBestReceived.load());
        }

        //
        // Try sending block announcements via headers
        //
        {
            // If we have less than MAX_BLOCKS_TO_ANNOUNCE in our
            // list of block hashes we're relaying, and our peer wants
            // headers announcements, then find the first header
            // not yet known to our peer but would connect, and send.
            // If no header would connect, or if we have too many
            // blocks, or if the peer doesn't want headers, just
            // add all to the inv queue.
            LOCK(pto->cs_inventory);
            std::vector<CBlock> vHeaders;
            bool fRevertToInv = (!state.fPreferHeaders || pto->vBlockHashesToAnnounce.size() > MAX_BLOCKS_TO_ANNOUNCE);
            CBlockIndex *pBestIndex = NULL; // last header queued for delivery
            requester.ProcessBlockAvailability(pto->id); // ensure pindexBestKnownBlock is up-to-date

            if (!fRevertToInv)
            {
                bool fFoundStartingHeader = false;
                // Try to find first header that our peer doesn't have, and
                // then send all headers past that one.  If we come across any
                // headers that aren't on chainActive, give up.
                for (const uint256 &hash : pto->vBlockHashesToAnnounce)
                {
                    BlockMap::iterator mi = mapBlockIndex.find(hash);
                    // BU skip blocks that we don't know about.  was: assert(mi != mapBlockIndex.end());
                    if (mi == mapBlockIndex.end())
                        continue;
                    CBlockIndex *pindex = mi->second;
                    if (chainActive[pindex->nHeight] != pindex)
                    {
                        // Bail out if we reorged away from this block
                        fRevertToInv = true;
                        break;
                    }
                    if (pBestIndex != nullptr && pindex->pprev != pBestIndex)
                    {
                        // This means that the list of blocks to announce don't
                        // connect to each other.
                        // This shouldn't really be possible to hit during
                        // regular operation (because reorgs should take us to
                        // a chain that has some block not on the prior chain,
                        // which should be caught by the prior check), but one
                        // way this could happen is by using invalidateblock /
                        // reconsiderblock repeatedly on the tip, causing it to
                        // be added multiple times to vBlockHashesToAnnounce.
                        // Robustly deal with this rare situation by reverting
                        // to an inv.
                        fRevertToInv = true;
                        break;
                    }
                    pBestIndex = pindex;
                    if (fFoundStartingHeader)
                    {
                        // add this to the headers message
                        vHeaders.push_back(pindex->GetBlockHeader());
                    }
                    else if (PeerHasHeader(&state, pindex))
                    {
                        continue; // keep looking for the first new block
                    }
                    else if (pindex->pprev == NULL || PeerHasHeader(&state, pindex->pprev))
                    {
                        // Peer doesn't have this header but they do have the prior one.
                        // Start sending headers.
                        fFoundStartingHeader = true;
                        vHeaders.push_back(pindex->GetBlockHeader());
                    }
                    else
                    {
                        // Peer doesn't have this header or the prior one -- nothing will
                        // connect, so bail out.
                        fRevertToInv = true;
                        break;
                    }
                }
            }
            if (fRevertToInv)
            {
                // If falling back to using an inv, just try to inv the tip.
                // The last entry in vBlockHashesToAnnounce was our tip at some point
                // in the past.
                if (!pto->vBlockHashesToAnnounce.empty())
                {
                    for (const uint256 &hashToAnnounce : pto->vBlockHashesToAnnounce)
                    {
                        BlockMap::iterator mi = mapBlockIndex.find(hashToAnnounce);
                        if (mi != mapBlockIndex.end())
                        {
                            CBlockIndex *pindex = mi->second;

                            // Warn if we're announcing a block that is not on the main chain.
                            // This should be very rare and could be optimized out.
                            // Just log for now.
                            if (chainActive[pindex->nHeight] != pindex)
                            {
                                LOG(NET, "Announcing block %s not on main chain (tip=%s)\n", hashToAnnounce.ToString(),
                                    chainActive.Tip()->GetBlockHash().ToString());
                            }

                            // If the peer announced this block to us, don't inv it back.
                            // (Since block announcements may not be via inv's, we can't solely rely on
                            // setInventoryKnown to track this.)
                            if (!PeerHasHeader(&state, pindex))
                            {
                                pto->PushInventory(CInv(MSG_BLOCK, hashToAnnounce));
                                LOG(NET, "%s: sending inv peer=%d hash=%s\n", __func__, pto->id,
                                    hashToAnnounce.ToString());
                            }
                        }
                    }
                }
            }
            else if (!vHeaders.empty())
            {
                if (vHeaders.size() > 1)
                {
                    LOG(NET, "%s: %u headers, range (%s, %s), to peer=%d\n", __func__, vHeaders.size(),
                        vHeaders.front().GetHash().ToString(), vHeaders.back().GetHash().ToString(), pto->id);
                }
                else
                {
                    LOG(NET, "%s: sending header %s to peer=%d\n", __func__, vHeaders.front().GetHash().ToString(),
                        pto->id);
                }
                pto->PushMessage(NetMsgType::HEADERS, vHeaders);
                state.pindexBestHeaderSent = pBestIndex;
            }
            pto->vBlockHashesToAnnounce.clear();
        }

        //
        // Message: inventory
        //

        FastRandomContext insecure_rand;
        std::vector<CInv> vInvWait;
        std::vector<CInv> vInvSend;
        {
            bool fSendTrickle = !pto->fWhitelisted;
            if (pto->nNextInvSend < nNow)
            {
                fSendTrickle = false;
                pto->nNextInvSend = PoissonNextSend(nNow, AVG_INVENTORY_BROADCAST_INTERVAL);
            }

            if (1)
            {
                // BU - here we only want to forward message inventory if our peer has actually been requesting
                // useful data or giving us useful data.  We give them 2 minutes to be useful but then choke off
                // their inventory.  This prevents fake peers from connecting and listening to our inventory
                // while providing no value to the network.
                // However we will still send them block inventory in the case they are a pruned node or wallet
                // waiting for block announcements, therefore we have to check each inv in pto->vInventoryToSend.
                bool chokeTxInv = (pto->nActivityBytes == 0 && (nNow / 1000000 - pto->nTimeConnected) > 120);
                LOCK(pto->cs_inventory);

                int invsz = pto->vInventoryToSend.size();
                vInvSend.reserve(invsz);
                // about 3/4 of the nodes should end up in this list, so over-allocate by 1/10th + 10 items
                vInvWait.reserve((invsz * 3) / 4 + invsz / 10 + 10);

                // Make copy of vInventoryToSend while cs_inventory is locked but also ignore some tx and defer others
                for (const CInv &inv : pto->vInventoryToSend)
                {
                    if (inv.type == MSG_TX)
                    {
                        if (chokeTxInv)
                            continue;
                        // skip if we already know abt this one
                        if (pto->filterInventoryKnown.contains(inv.hash))
                            continue;
                        if (fSendTrickle)
                        {
                            // 1/4 of tx invs blast to all immediately
                            if ((insecure_rand.rand32() & 3) != 0)
                            {
                                vInvWait.push_back(inv);
                                continue;
                            }
                        }
                    }
                    vInvSend.push_back(inv);
                    pto->filterInventoryKnown.insert(inv.hash);
                }
                pto->vInventoryToSend = vInvWait;
            }

            const int MAX_INV_ELEMENTS = 1000;
            int sz = vInvSend.size();
            if (sz)
            {
                LOCK(pto->cs_vSend);
                for (int i = 0; i < sz; i += MAX_INV_ELEMENTS)
                {
                    int sendsz = std::min(MAX_INV_ELEMENTS, sz - i);
                    std::vector<CInv> vInv(sendsz);
                    for (int j = 0; j < sendsz; j++)
                        vInv[j] = vInvSend[i + j];
                    pto->PushMessage(NetMsgType::INV, vInv); // TODO subvector PushMessage to avoid copy
                }
            }
        }

        // Request the next blocks. Mostly this will get exucuted during IBD but sometimes even
        // when the chain is syncd a block will get request via this method.
        requester.RequestNextBlocksToDownload(pto);
    }
    return true;
}

ThresholdState VersionBitsTipState(const Consensus::Params &params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsState(chainActive.Tip(), params, pos, versionbitscache);
}

void MainCleanup()
{
    if (1)
    {
        LOCK(cs_main); // BU apply the appropriate lock so no contention during destruction
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();
    }

    if (1)
    {
        LOCK(orphanpool.cs); // BU apply the appropriate lock so no contention during destruction
        // orphan transactions
        orphanpool.mapOrphanTransactions.clear();
        orphanpool.mapOrphanTransactionsByPrev.clear();
    }
}
