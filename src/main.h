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
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 1000000;
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

/** Default -persistmempool */
static const bool DEFAULT_PERSIST_MEMPOOL = true;

static const bool DEFAULT_TESTSAFEMODE = false;

/** Maximum number of headers to announce when relaying blocks with headers message.*/
static const unsigned int MAX_BLOCKS_TO_ANNOUNCE = 8;

static const bool DEFAULT_PEERBLOOMFILTERS = true;
static const bool DEFAULT_USE_THINBLOCKS = true;
static const uint64_t DEFAULT_PREFERENTIAL_TIMER = 1000;
static const bool DEFAULT_USE_GRAPHENE_BLOCKS = true;
static const bool DEFAULT_USE_COMPACT_BLOCKS = true;

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

extern CTweak<bool> enableCanonicalTxOrder;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
typedef boost::unordered_map<uint256, CBlockIndex *, BlockHasher> BlockMap;
extern CSharedCriticalSection cs_mapBlockIndex;
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
extern std::atomic<CBlockIndex *> pindexBestHeader;

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


/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
/** Import blocks from an external file */
bool LoadExternalBlockFile(const CChainParams &chainparams, FILE *fileIn, CDiskBlockPos *dbp = nullptr);
/** Do we already have this transaction or has it been seen in a block */
bool AlreadyHaveTx(const CInv &inv);
/** Do we already have this block on disk */
bool AlreadyHaveBlock(const CInv &inv);

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

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);

/**
   Determine whether free transactions are subject to rate limiting. If -limitfreerelay is not zero then rate limiting
   for free txns will be in effect. If it is zero, then no free transactions will be allowed to enter the memory pool.
 */
bool AreFreeTxnsDisallowed();

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
 * Test whether the LockPoints height and time are still valid on the current chain
 */
bool TestLockPointValidity(const LockPoints *lp);

// Checks that the provided block is consistent with the chainparam's checkpoints
bool CheckAgainstCheckpoint(unsigned int height, const uint256 &hash, const CChainParams &chainparams);

/** Store block on disk. If dbp is non-NULL, the file is known to already reside on disk */
bool AcceptBlock(CBlock &block, CValidationState &state, CBlockIndex **pindex, bool fRequested, CDiskBlockPos *dbp);

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex *FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator);


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
