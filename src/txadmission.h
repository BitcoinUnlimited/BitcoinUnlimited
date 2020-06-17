// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXADMISSION_H
#define BITCOIN_TXADMISSION_H

#include "fastfilter.h"
#include "main.h"
#include "net.h"
#include "threadgroup.h"
#include "txdebugger.h"
#include "txmempool.h"
#include <queue>

/** The default value for -minrelaytxfee in sat/byte */
static const double DEFAULT_MINLIMITERTXFEE = (double)DEFAULT_MIN_RELAY_TX_FEE / 1000;
/** The default value for -maxrelaytxfee in sat/byte */
static const double DEFAULT_MAXLIMITERTXFEE = (double)DEFAULT_MIN_RELAY_TX_FEE / 1000;
/** The number of block heights to gradually choke spam transactions over */
static const unsigned int MAX_BLOCK_SIZE_MULTIPLIER = 3;

/** The maximum number of free transactions (in KB) that can enter the mempool per minute.
 *  For a 1MB block we allow 15KB of free transactions per 1 minute.
 */
// static const uint32_t DEFAULT_LIMITFREERELAY = DEFAULT_BLOCK_MAX_SIZE * 0.000015;
static const uint32_t DEFAULT_LIMITFREERELAY = 0;
/** The minimum value possible for -limitfreerelay when rate limiting */
// static const unsigned int DEFAULT_MIN_LIMITFREERELAY = 1;
static const unsigned int DEFAULT_MIN_LIMITFREERELAY = 0;

/** Subject free transactions to priority checking when entering the mempool */
static const bool DEFAULT_RELAYPRIORITY = false;

/**
 * Filter for transactions that were recently rejected by
 * AcceptToMemoryPool. These are not rerequested until the chain tip
 * changes, at which point the entire filter is reset. Protected by
 * cs_recentRejects
 *
 * Without this filter we'd be re-requesting txs from each of our peers,
 * increasing bandwidth consumption considerably. For instance, with 100
 * peers, half of which relay a tx we don't accept, that might be a 50x
 * bandwidth increase. A flooding attacker attempting to roll-over the
 * filter using minimum-sized, 60byte, transactions might manage to send
 * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
 * two minute window to send invs to us.
 *
 * Decreasing the false positive rate is fairly cheap, so we pick one in a
 * million to make it highly unlikely for users to have issues with this
 * filter.
 *
 * Memory used: 1.7MB
 */

// A consistent moment in the data state
class Snapshot
{
public:
    CSharedCriticalSection cs_snapshot;
    uint64_t tipHeight;
    uint64_t tipMedianTimePast;
    int64_t adjustedTime;
    CBlockIndex *tip; // CBlockIndexes are never deleted once created (even if the tip changes) so we can use this ptr
    CCoinsViewCache *coins;
    CCoinsViewMemPool *cvMempool;

    void Load(void);

    Snapshot() : coins(nullptr), cvMempool(nullptr) {}
    ~Snapshot()
    {
        if (cvMempool)
            delete cvMempool;
    }
};

extern Snapshot txHandlerSnap;

// Tracks data about a transaction that hasn't yet been processed
class CTxInputData
{
public:
    CTransactionRef tx;
    NodeId nodeId; // hold the id so I don't keep a ref to the node
    bool whitelisted;
    std::string nodeName;

    CTxInputData() : nodeId(-1), whitelisted(false), nodeName("none") {}
};

// Tracks data about transactions that are ready to be committed to the mempool
class CTxCommitData
{
public:
    CTxMemPoolEntry entry;
    uint256 hash;
};

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


// Controls all transaction admission threads
extern CThreadCorral txProcessingCorral;
// Corral bits
enum
{
    CORRAL_TX_PROCESSING = 1,
    CORRAL_TX_COMMITMENT = 2,
    CORRAL_TX_PAUSE = 3,
};

// Transaction mempool admission globals

// maximum transaction mempool admission threads
extern CTweak<unsigned int> numTxAdmissionThreads;

// restrict transaction inputs to 1 for long unconfirmed chains
extern CTweak<bool> restrictInputs;

extern CRollingFastFilter<4 * 1024 * 1024> recentRejects;
extern CRollingFastFilter<4 * 1024 * 1024> txRecentlyInBlock;

// Finds transactions that may conflict with other pending transactions
extern CFastFilter<4 * 1024 * 1024> incomingConflicts;

// Transactions that are available to be added to the mempool, and protection
// Guarded by csTxInQ
extern CCriticalSection csTxInQ;
extern CCond cvTxInQ;
extern std::queue<CTxInputData> txInQ;

// Transactions that cannot be processed in this round (may potentially conflict with other tx)
// Guarded by csTxInQ
extern std::queue<CTxInputData> txDeferQ;

// Transactions that arrive when the chain is not syncd can be place here at times when we've received
// the block announcement but havn't yet downloaded the block and updated the tip. In this case there can
// be txns that are perfectly valid yet are flagged as being non-final or has too many ancestors.
// Guarded by csTxInQ
extern std::queue<CTxInputData> txWaitNextBlockQ;

// Transactions that are validated and can be committed to the mempool, and protection
extern CWaitableCriticalSection csCommitQ;
extern CConditionVariable cvCommitQ;
extern std::map<uint256, CTxCommitData> *txCommitQ;

// returns a transaction ref, if it exists in the commitQ
CTransactionRef CommitQGet(uint256 hash);

/** Start the transaction mempool admission threads */
void StartTxAdmission();
/** Stop the transaction mempool admission threads (assumes that ShutdownRequested() will return true) */
void StopTxAdmission();
/** Wait for the currently enqueued transactions to be flushed.  If new tx keep coming in, you may wait a while */
void FlushTxAdmission();

/// Put the tx on the tx admission queue for processing
void EnqueueTxForAdmission(CTxInputData &txd);

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit = false,
    bool fRejectAbsurdFee = false,
    TransactionClass allowedTx = TransactionClass::DEFAULT);

/** (try to) add transaction to memory pool **/
bool ParallelAcceptToMemoryPool(Snapshot &ss,
    CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx,
    std::vector<COutPoint> &vCoinsToUncache,
    bool *isRespend,
    CValidationDebugger *debugger = nullptr,
    CTxProperties *txProps = nullptr);

/** Checks the size of the mempool and trims it if needed */
void LimitMempoolSize(CTxMemPool &pool, size_t limit, unsigned long age);

// Return > 0 if its likely that we have already dealt with this transaction. inv MUST be MSG_TX type.
unsigned int TxAlreadyHave(const CInv &inv);

/**
 * Commit all accepted tx into the mempool.  Corral with CORRAL_TX_PAUSE before calling to stop
 * threads from adding new tx into the q.
 */
void CommitTxToMempool();

/** Run the transaction admission thread */
void ThreadTxAdmission();

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransactionRef tx, int flags = -1, const Snapshot *ss = nullptr);

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
bool CheckSequenceLocks(const CTransactionRef tx,
    int flags,
    LockPoints *lp = nullptr,
    bool useExistingLockPoints = false,
    const Snapshot *ss = nullptr);

// This needs to be held whenever the chain state changes (block added or chain rewind) so that
// transactions are not processed during chain state updates and so once the chain state is updated we can
// grab a new read-only snapshot of this state in txHandlerSnap.
class TxAdmissionPause
{
public:
    TxAdmissionPause()
    {
        txProcessingCorral.Enter(CORRAL_TX_PAUSE);
        CommitTxToMempool();
    }

    ~TxAdmissionPause()
    {
        txHandlerSnap.Load(); // Load the new block into the transaction processor's state snapshot
        txProcessingCorral.Exit(CORRAL_TX_PAUSE);
    }
};

#endif
