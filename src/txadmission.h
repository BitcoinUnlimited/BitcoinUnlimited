// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "fastfilter.h"
#include "net.h"
#include "txmempool.h"
#include <queue>

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
    CCriticalSection cs;
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

extern CRollingFastFilter<4 * 1024 * 1024> recentRejects;
extern CRollingFastFilter<4 * 1024 * 1024> txRecentlyInBlock;

// Finds transactions that may conflict with other pending transactions
extern CFastFilter<4 * 1024 * 1024> incomingConflicts;

// Transactions that are available to be added to the mempool, and protection
extern CCriticalSection csTxInQ;
extern CCond cvTxInQ;
extern std::queue<CTxInputData> txInQ;

// Transactions that cannot be processed in this round (may potentially conflict with other tx)
extern std::queue<CTxInputData> txDeferQ;

// Transactions that are validated and can be committed to the mempool, and protection
extern CWaitableCriticalSection csCommitQ;
extern CConditionVariable cvCommitQ;
extern std::map<uint256, CTxCommitData> txCommitQ;

/** Start the transaction mempool admission threads */
void StartTxAdmission(boost::thread_group &threadGroup);
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
    bool *isRespend);

/** Checks the size of the mempool and trims it if needed */
void LimitMempoolSize(CTxMemPool &pool, size_t limit, unsigned long age);

// Return > 0 if its likely that we have already dealt with this transaction. inv MUST be MSG_TX type.
unsigned int TxAlreadyHave(const CInv &inv);

// Commit all accepted tx into the mempool.  Corral with CORRAL_TX_PAUSE before calling to stop
// threads from adding new tx into the q.
void CommitTxToMempool();


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
