// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Copyright (c) 2016 Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIN_H
#define BITCOIN_MAIN_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "fs.h"
#include "net.h"
#include "policy/policy.h"
#include "script/script_error.h"
#include "sync.h"
#include "txdb.h"
#include "versionbits.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/unordered_map.hpp>

class ValidationResourceTracker;
class CBlockIndex;
class CBlockTreeDB;
class CBloomFilter;
class CChainParams;
class CInv;
class CScriptCheck;
class CScriptCheckAndAnalyze;
class CTxMemPool;
class CValidationInterface;
class CValidationState;

struct CNodeStateStats;
struct LockPoints;

/** Global variable that points to the coins database */
extern CCoinsViewDB *pcoinsdbview;

/** Default for DEFAULT_WHITELISTRELAY. */
static const bool DEFAULT_WHITELISTRELAY = true;
/** Default for DEFAULT_WHITELISTFORCERELAY. */
static const bool DEFAULT_WHITELISTFORCERELAY = true;
/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const unsigned int DEFAULT_MIN_RELAY_TX_FEE = 1000;
//! -maxtxfee default
static const CAmount DEFAULT_TRANSACTION_MAXFEE = 0.1 * COIN;
//! Discourage users to set fees higher than this amount (in satoshis) per kB
static const CAmount HIGH_TX_FEE_PER_KB = 0.01 * COIN;
//! -maxtxfee will warn if called with a higher fee than this amount (in satoshis)
static const CAmount HIGH_MAX_TX_FEE = 100 * HIGH_TX_FEE_PER_KB;
/** Default for -maxorphantx, maximum number of orphan transactions kept in memory.
 *  A high default is chosen which allows for about 1/10 of the default mempool to
 *  be kept as orphans, assuming 250 byte transactions.  We are essentially disabling
 *  the limiting or orphan transactions by number and using orphan pool bytes as
 *  the limiting factor, while at the same time allowing node operators to
 *  limit by number if transactions if they wish by modifying -maxorphantx=<n> if
 *  the have a need to.
 */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 120000;
/** Default for -limitancestorcount, max number of in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_LIMIT = 25;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_SIZE_LIMIT = 101;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_LIMIT = 25;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_SIZE_LIMIT = 101;
/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
static const unsigned int DEFAULT_MEMPOOL_EXPIRY = 72;
/** Default for -orphanpoolexpiry, expiration time for orphan pool transactions in hours */
static const unsigned int DEFAULT_ORPHANPOOL_EXPIRY = 4;
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB

/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
// static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which we must receive a VERACK message after having first sent a VERSION message */
static const unsigned int VERACK_TIMEOUT = 60;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached its tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
static const unsigned int DATABASE_WRITE_INTERVAL = 60 * 60;
/** Time to wait (in seconds) between flushing chainstate to disk. */
static const unsigned int DATABASE_FLUSH_INTERVAL = 24 * 60 * 60;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
/** Average delay between local address broadcasts in seconds. */
static const unsigned int AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL = 24 * 24 * 60;
/** Average delay between peer address broadcasts in seconds. */
static const unsigned int AVG_ADDRESS_BROADCAST_INTERVAL = 30;
/** Block download timeout base, expressed in millionths of the block interval (i.e. 10 min) */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_BASE = 1000000;
/** Additional block download timeout per parallel downloading peer (i.e. 5 min) */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_PER_PEER = 500000;
/** Timeout in secs for the initial sync. If we don't receive the first batch of headers */
static const uint32_t INITIAL_HEADERS_TIMEOUT = 120;
/** The maximum number of headers in the mapUnconnectedHeaders cache **/
static const uint32_t MAX_UNCONNECTED_HEADERS = 144;
/** The maximum length of time, in seconds, we keep unconnected headers in the cache **/
static const uint32_t UNCONNECTED_HEADERS_TIMEOUT = 120;
/** Maximum number of INV's that can be send in one message */
static const int MAX_INV_TO_SEND = 1000;

/** The maximum number of free transactions (in KB) that can enter the mempool per minute.
 *  For a 1MB block we allow 15KB of free transactions per 1 minute.
 */
static const uint32_t DEFAULT_LIMITFREERELAY = DEFAULT_BLOCK_MAX_SIZE * 0.000015;
/** Subject free transactions to priority checking when entering the mempool */
static const bool DEFAULT_RELAYPRIORITY = false;
/** The number of MiB that we will wait for the block storage method to go over before pruning */
static const uint64_t DEFAULT_PRUNE_INTERVAL = 100;

static const int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;

/** Default for -permitbaremultisig */
static const bool DEFAULT_PERMIT_BAREMULTISIG = true;
static const unsigned int DEFAULT_BYTES_PER_SIGOP = 20;
static const bool DEFAULT_CHECKPOINTS_ENABLED = true;

static const bool DEFAULT_TESTSAFEMODE = false;

/** Maximum number of headers to announce when relaying blocks with headers message.*/
static const unsigned int MAX_BLOCKS_TO_ANNOUNCE = 8;

static const bool DEFAULT_PEERBLOOMFILTERS = true;
static const bool DEFAULT_USE_THINBLOCKS = true;
static const bool DEFAULT_USE_GRAPHENE_BLOCKS = false;

static const bool DEFAULT_REINDEX = false;
static const bool DEFAULT_DISCOVER = true;
static const bool DEFAULT_PRINTTOCONSOLE = false;

// BU - Xtreme Thinblocks Auto Mempool Limiter - begin section
/** The default value for -minrelaytxfee in sat/byte */
static const double DEFAULT_MINLIMITERTXFEE = 0.0;
/** The default value for -maxrelaytxfee in sat/byte */
static const double DEFAULT_MAXLIMITERTXFEE = (double)DEFAULT_MIN_RELAY_TX_FEE / 1000;
/** The number of block heights to gradually choke spam transactions over */
static const unsigned int MAX_BLOCK_SIZE_MULTIPLIER = 3;
/** The minimum value possible for -limitfreerelay when rate limiting */
static const unsigned int DEFAULT_MIN_LIMITFREERELAY = 1;
// BU - Xtreme Thinblocks Auto Mempool Limiter - end section

struct BlockHasher
{
    size_t operator()(const uint256 &hash) const { return hash.GetCheapHash(); }
};

extern CCriticalSection cs_main;
extern CTxMemPool mempool;
typedef boost::unordered_map<uint256, CBlockIndex *, BlockHasher> BlockMap;
extern BlockMap mapBlockIndex;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern bool fImporting;
extern bool fReindex;
extern bool fTxIndex;
extern bool fIsBareMultisigStd;
extern unsigned int nBytesPerSigOp;
extern bool fCheckBlockIndex;
extern bool fCheckpointsEnabled;
extern int64_t nCoinCacheMaxSize;
/** A fee rate smaller than this is considered zero fee (for relaying, mining and transaction creation) */
extern CFeeRate minRelayTxFee;
/** Absolute maximum transaction fee (in satoshis) used by wallet and mempool (rejects high fee in sendrawtransaction)
 */
extern CTweak<CAmount> maxTxFee;
/** If the tip is older than this (in seconds), the node is considered to be in initial block download. */
extern int64_t nMaxTipAge;

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex *pindexBestHeader;

/** Used to determine whether it is time to check the orphan pool for any txns that can be evicted. */
extern int64_t nLastOrphanCheck;

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/** Pruning-related variables and constants */
/** True if any block files have ever been pruned. */
extern bool fHavePruned;
/** True if we're running in -prune mode. */
extern bool fPruneMode;
/** Number of MiB of block files that we're trying to stay below. */
extern uint64_t nPruneTarget;
/** Number of MiB the blockdb is using. */
extern uint64_t nDBUsedSpace;
/** The maximum bloom filter size that we will support for an xthin request. This value is communicated to
 *  our peer at the time we first make the connection.
 */
extern uint32_t nXthinBloomFilterSize;
/** Block files containing a block-height within MIN_BLOCKS_TO_KEEP of chainActive.Tip() will not be pruned. */
static const unsigned int MIN_BLOCKS_TO_KEEP = 288;

static const signed int DEFAULT_CHECKBLOCKS = 6;
static const unsigned int DEFAULT_CHECKLEVEL = 3;

// Require that user allocate at least 550MB for block & undo files (blk???.dat and rev???.dat)
// At 1MB per block, 288 blocks = 288MB.
// Add 15% for Undo data = 331MB
// Add 20% for Orphan block rate = 397MB
// We want the low water mark after pruning to be at least 397 MB and since we prune in
// full block file chunks, we need the high water mark which triggers the prune to be
// one 128MB block file + added 15% undo data = 147MB greater for a total of 545MB
// Setting the target to > than 550MB will make it likely we can respect the target.
static const uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 550 * 1024 * 1024;

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals &nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals &nodeSignals);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during
 * validation/connection/etc of otherwise unrelated blocks during reorganisation; or it may be set to an Invalid state
 * if pblock is itself invalid (but this is not guaranteed even when the block is checked). If you want to *possibly*
 * get feedback on whether pblock is valid, you must also install a CValidationInterface (see validationinterface.h) -
 * this will have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be
 * penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used for non-network block sources and
 * whitelisted peers.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(CValidationState &state,
    const CChainParams &chainparams,
    CNode *pfrom,
    const CBlock *pblock,
    bool fForceProcessing,
    CDiskBlockPos *dbp,
    bool fParallel);
/** Disconnect the current chainActive.Tip() */
bool DisconnectTip(CValidationState &state, const Consensus::Params &consensusParams, const bool fRollBack = false);
/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
/** Import blocks from an external file */
bool LoadExternalBlockFile(const CChainParams &chainparams, FILE *fileIn, CDiskBlockPos *dbp = nullptr);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex(const CChainParams &chainparams);
/** Load the block tree and coins database from disk */
bool LoadBlockIndex();
/** Unload database information */
void UnloadBlockIndex();
/** Process protocol messages received from a given node */
bool ProcessMessages(CNode *pfrom);
/** Do we already have this transaction or has it been seen in a block */
bool AlreadyHaveTx(const CInv &inv);
/** Do we already have this block on disk */
bool AlreadyHaveBlock(const CInv &inv);
bool AcceptBlockHeader(const CBlockHeader &block,
    CValidationState &state,
    const CChainParams &chainparams,
    CBlockIndex **ppindex = nullptr);

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

/** Process a single protocol messages received from a given node */
bool ProcessMessage(CNode *pfrom, std::string strCommand, CDataStream &vRecv, int64_t nTimeReceived);

/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 */
bool SendMessages(CNode *pto);
// BU: moves to parallel.h
/** Run an instance of the script checking thread */
// void ThreadScriptCheck();

/** Try to detect Partition (network isolation) attacks against us */
void PartitionCheck(bool (*initialDownloadCheck)(),
    CCriticalSection &cs,
    const CBlockIndex *const &bestHeader,
    int64_t nPowTargetSpacing);
/** Format a string that describes several potential problems detected by the core.
 * strFor can have three values:
 * - "rpc": get critical warnings, which should put the client in safe mode if non-empty
 * - "statusbar": get all warnings
 * - "gui": get all warnings, translated (where possible) for GUI
 * This function only returns the highest priority warning of the set selected by strFor.
 */
std::string GetWarnings(const std::string &strFor);
/** Retrieve a transaction (from memory pool, or from disk, if possible) */
bool GetTransaction(const uint256 &hash,
    CTransactionRef &tx,
    const Consensus::Params &params,
    uint256 &hashBlock,
    bool fAllowSlow = false);
/** Find the best known block, and make it the tip of the block chain */
bool ActivateBestChain(CValidationState &state,
    const CChainParams &chainparams,
    const CBlock *pblock = nullptr,
    bool fParallel = false);
CAmount GetBlockSubsidy(int nHeight, const Consensus::Params &consensusParams);

/** Create a new block index entry for a given block hash */
CBlockIndex *InsertBlockIndex(uint256 hash);
/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);

/** Check is Cash HF has activated. */
bool IsDAAEnabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev);

/**
   Determine whether free transactions are subject to rate limiting. If -limitfreerelay is not zero then rate limiting
   for free txns will be in effect. If it is zero, then no free transactions will be allowed to enter the memory pool.
 */
bool AreFreeTxnsDisallowed();

/** Communicate what class of transaction is acceptable to add to the memory pool
 */
enum class TransactionClass
{
    INVALID,
    DEFAULT,
    STANDARD,
    NONSTANDARD
};

TransactionClass ParseTransactionClass(const std::string &s);

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit = false,
    bool fRejectAbsurdFee = false,
    TransactionClass allowedTx = TransactionClass::DEFAULT);

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state);

/** Get the BIP135 state for a given deployment at the current tip. */
ThresholdState VersionBitsTipState(const Consensus::Params &params, Consensus::DeploymentPos pos);

struct CNodeStateStats
{
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

/**
 * Check whether all inputs of this transaction are valid (no double spends, scripts & sigs, amounts)
 * This does not modify the UTXO set. If pvChecks is not NULL, script checks are pushed onto it
 * instead of being performed inline.
 */
bool CheckInputs(const CTransaction &tx,
    CValidationState &state,
    const CCoinsViewCache &view,
    bool fScriptChecks,
    unsigned int flags,
    bool cacheStore,
    ValidationResourceTracker *resourceTracker,
    std::vector<CScriptCheck> *pvChecks = nullptr,
    unsigned char *sighashType = nullptr);


/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, int nHeight);

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransaction &tx, int flags = -1);

/**
 * Test whether the LockPoints height and time are still valid on the current chain
 */
bool TestLockPointValidity(const LockPoints *lp);

/*
 * Check if transaction will be BIP 68 final in the next block to be created.
 *
 * Simulates calling SequenceLocks() with data from the tip of the current active chain.
 * Optionally stores in LockPoints the resulting height and time calculated and the hash
 * of the block needed for calculation or skips the calculation and uses the LockPoints
 * passed in for evaluation.
 * The LockPoints should not be considered valid if CheckSequenceLocks returns false.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckSequenceLocks(const CTransaction &tx,
    int flags,
    LockPoints *lp = nullptr,
    bool useExistingLockPoints = false);

/**
 * Class that keeps track of number of signature operations
 * and bytes hashed to compute signature hashes.
 */
class ValidationResourceTracker
{
private:
    mutable CCriticalSection cs;
    uint64_t nSigops;
    uint64_t nSighashBytes;

public:
    ValidationResourceTracker() : nSigops(0), nSighashBytes(0) {}
    void Update(const uint256 &txid, uint64_t nSigopsIn, uint64_t nSighashBytesIn)
    {
        LOCK(cs);
        nSigops += nSigopsIn;
        nSighashBytes += nSighashBytesIn;
        return;
    }
    uint64_t GetSigOps() const
    {
        LOCK(cs);
        return nSigops;
    }
    uint64_t GetSighashBytes() const
    {
        LOCK(cs);
        return nSighashBytes;
    }
};


/** Functions for validating blocks and updating the block tree */

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  In case pfClean is provided, operation will try to be tolerant about errors, and *pfClean
 *  will be true if no problems were found. Otherwise, the return value will be false in case
 *  of problems. Note that in any case, coins may be modified. */
bool DisconnectBlock(const CBlock &block,
    CValidationState &state,
    const CBlockIndex *pindex,
    CCoinsViewCache &coins,
    bool *pfClean = nullptr);

/** Apply the effects of this block (with given index) on the UTXO set represented by coins */
bool ConnectBlock(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const CChainParams &chainparams,
    bool fJustCheck = false,
    bool fParallel = false);

/** Context-independent validity checks */
bool CheckBlockHeader(const CBlockHeader &block, CValidationState &state, bool fCheckPOW = true);
// BU: returns the blocksize if block is valid.  Otherwise 0
bool CheckBlock(const CBlock &block,
    CValidationState &state,
    bool fCheckPOW = true,
    bool fCheckMerkleRoot = true,
    bool conservative = false);

/** Context-dependent validity checks */
bool ContextualCheckBlockHeader(const CBlockHeader &block, CValidationState &state, CBlockIndex *pindexPrev);
bool ContextualCheckBlock(const CBlock &block, CValidationState &state, CBlockIndex *pindexPrev);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main
 * held) */
bool TestBlockValidity(CValidationState &state,
    const CChainParams &chainparams,
    const CBlock &block,
    CBlockIndex *pindexPrev,
    bool fCheckPOW = true,
    bool fCheckMerkleRoot = true);

// Checks that the provided block is consistent with the chainparam's checkpoints
bool CheckAgainstCheckpoint(unsigned int height, const uint256 &hash, const CChainParams &chainparams);

/** Store block on disk. If dbp is non-NULL, the file is known to already reside on disk */
bool AcceptBlock(CBlock &block, CValidationState &state, CBlockIndex **pindex, bool fRequested, CDiskBlockPos *dbp);

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB
{
public:
    CVerifyDB();
    ~CVerifyDB();
    bool VerifyDB(const CChainParams &chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth);
};

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex *FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState &state, const Consensus::Params &consensusParams, CBlockIndex *pindex);

/** Remove invalidity status from a block and its descendants. */
bool ReconsiderBlock(CValidationState &state, CBlockIndex *pindex);

bool FindBlockPos(CValidationState &state,
    CDiskBlockPos &pos,
    unsigned int nAddSize,
    unsigned int nHeight,
    uint64_t nTime,
    bool fKnown);

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindexNew,
    const CDiskBlockPos &pos);

CBlockIndex *AddToBlockIndex(const CBlockHeader &block);

/** The currently-connected chain of blocks (protected by cs_main). */
extern CChain chainActive;

/** Global variable that points to the active CCoinsView (protected by cs_utxo) */
extern CCoinsViewCache *pcoinsTip;

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB *pblocktree;
/** Global variable that points to the block tree on the inactive storage method (protected by cs_main) */
extern CBlockTreeDB *pblocktreeother;

extern std::vector<CBlockFileInfo> vinfoBlockFile;
extern int nLastBlockFile;


extern VersionBitsCache versionbitscache;

/**
 * Determine what nVersion a new block should use.
 */
int32_t ComputeBlockVersion(const CBlockIndex *pindexPrev, const Consensus::Params &params);

/** Reject codes greater or equal to this can be returned by AcceptToMemPool
 * for transactions, to signal internal conditions. They cannot and should not
 * be sent over the P2P network.
 */
static const unsigned int REJECT_INTERNAL = 0x100;
/** Too high fee. Can not be triggered by P2P transactions */
static const unsigned int REJECT_HIGHFEE = 0x100;
/** Transaction is already known (either in mempool or blockchain) */
static const unsigned int REJECT_ALREADY_KNOWN = 0x101;
/** Transaction conflicts with a transaction already known */
static const unsigned int REJECT_CONFLICT = 0x102;
/** Transaction cannot be committed on my fork */
static const unsigned int REJECT_WRONG_FORK = 0x103;

CBlockIndex *FindMostWorkChain();

/** Test if fork is active */
bool IsMay152018Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev);

// BU cleaning up at destuction time creates many global variable dependencies.  Instead clean up in a function called
// in main()
#if 0
class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup();
};
#endif


#endif // BITCOIN_MAIN_H
