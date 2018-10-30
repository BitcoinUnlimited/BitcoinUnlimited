// Copyright (c) 2016-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PARALLEL_H
#define BITCOIN_PARALLEL_H

#include "checkqueue.h"
#include "consensus/validation.h"
#include "main.h"
#include "primitives/block.h"
#include "protocol.h"
#include "serialize.h"
#include "stat.h"
#include "uint256.h"
#include "util.h"
#include <vector>

#include <boost/thread.hpp>

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

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction
 */
class CScriptCheck
{
protected:
    ValidationResourceTracker *resourceTracker;
    CScript scriptPubKey;
    CAmount amount;
    const CTransaction *ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    unsigned int maxOps;
    bool cacheStore;
    ScriptError error;

public:
    unsigned char sighashType;
    CScriptCheck()
        : resourceTracker(nullptr), amount(0), ptxTo(0), nIn(0), nFlags(0), maxOps(0xffffffff), cacheStore(false),
          error(SCRIPT_ERR_UNKNOWN_ERROR), sighashType(0)
    {
    }

    CScriptCheck(ValidationResourceTracker *resourceTrackerIn,
        const CScript &scriptPubKeyIn,
        const CAmount amountIn,
        const CTransaction &txToIn,
        unsigned int nInIn,
        unsigned int nFlagsIn,
        unsigned int maxOpsIn,
        bool cacheIn)
        : resourceTracker(resourceTrackerIn), scriptPubKey(scriptPubKeyIn), amount(amountIn), ptxTo(&txToIn),
          nIn(nInIn), nFlags(nFlagsIn), maxOps(maxOpsIn), cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR),
          sighashType(0)
    {
    }

    bool operator()();

    void swap(CScriptCheck &check)
    {
        std::swap(resourceTracker, check.resourceTracker);
        scriptPubKey.swap(check.scriptPubKey);
        std::swap(ptxTo, check.ptxTo);
        std::swap(amount, check.amount);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
        std::swap(sighashType, check.sighashType);
        std::swap(maxOps, check.maxOps);
    }

    ScriptError GetScriptError() const { return error; }
};

class CParallelValidation
{
private:
    // txn hashes that are in the previous block
    CCriticalSection cs_previousblock;
    std::vector<uint256> vPreviousBlock;
    // Vector of script check queues
    std::vector<CCheckQueue<CScriptCheck> *> vQueues;
    // Number of threads
    unsigned int nThreads;
    // All threads currently running
    boost::thread_group threadGroup;
    // The semaphore limits the number of parallel validation threads
    CSemaphore semThreadCount;

    struct CHandleBlockMsgThreads
    {
        CCheckQueue<CScriptCheck> *pScriptQueue;
        uint256 hash;
        uint256 hashPrevBlock;
        uint32_t nChainWork; // chain work for this block.
        uint32_t nMostWorkOurFork; // most work for the chain we are on.
        uint32_t nSequenceId;
        int64_t nStartTime;
        uint64_t nBlockSize;
        bool fQuit;
        NodeId nodeid;
        bool fIsValidating; // is the block currently in connectblock() and validating inputs
        bool fIsReorgInProgress; // has a re-org to another chain been triggered.
    };
    CCriticalSection cs_blockvalidationthread;
    std::map<boost::thread::id, CHandleBlockMsgThreads> mapBlockValidationThreads GUARDED_BY(cs_blockvalidationthread);


public:
    /**
     * Construct a parallel validator.
     * @param[in] threadCount   The number of script validation threads.  If <= 1 then no separate validation threads
     *                          are created.
     * @param[in] threadGroup   The thread group threads will be created in
     */
    CParallelValidation();

    ~CParallelValidation();

    /* Initialize mapBlockValidationThreads*/
    void InitThread(const boost::thread::id this_id,
        const CNode *pfrom,
        CBlockRef pblock,
        const CInv &inv,
        uint64_t blockSize);

    /* Initialize a PV session */
    bool Initialize(const boost::thread::id this_id, const CBlockIndex *pindex, const bool fParallel);

    /* Cleanup PV threads after one has finished and won the validation race */
    void Cleanup(const CBlock &block, CBlockIndex *pindex);

    /* Send quit to competing threads */
    void QuitCompetingThreads(const uint256 &prevBlockHash);

    /* Is this block already running a validation thread? */
    bool IsAlreadyValidating(const NodeId id);

    /* Terminate all currently running Block Validation threads, except the passed thread */
    void StopAllValidationThreads(const boost::thread::id this_id = boost::thread::id());
    /* Terminate all currently running Block Validation threads whose chainWork is <= the passed parameter, except the
     * calling thread  */
    void StopAllValidationThreads(const uint32_t nChainWork);
    void WaitForAllValidationThreadsToStop();

    /* Has parallel block validation been turned on via the config settings */
    bool Enabled();

    /* Clear thread data from mapBlockValidationThreads */
    void Erase(const boost::thread::id this_id);

    /* Post the semaphore when the thread exits.  */
    void Post() { semThreadCount.post(); }
    /* Was the fQuit flag set to true which causes the PV thread to exit */
    bool QuitReceived(const boost::thread::id this_id, const bool fParallel);

    /* Used to determine if another thread has already updated the utxo and advance the chain tip */
    bool ChainWorkHasChanged(const arith_uint256 &nStartingChainWork);

    /* Set the correct locks and locking order before returning from a PV session */
    void SetLocks(const bool fParallel);

    /* Is there a re-org in progress */
    void IsReorgInProgress(const boost::thread::id this_id, const bool fReorg, const bool fParallel);
    bool IsReorgInProgress();

    /* Update the nMostWorkOurFork when a new header arrives */
    void UpdateMostWorkOurFork(const CBlockHeader &header);

    /* Update the nMostWorkOurFork when a new header arrives */
    uint32_t MaxWorkChainBeingProcessed();

    /* Clear orphans from the orphan cache that are no longer needed*/
    void ClearOrphanCache(const CBlockRef pblock);

    /* Process a block message */
    void HandleBlockMessage(CNode *pfrom, const std::string &strCommand, CBlockRef pblock, const CInv &inv);

    // The number of script validation threads
    unsigned int ThreadCount() { return nThreads; }
    // The number of script check queues
    unsigned int QueueCount();

    // For newly mined block validation, return the first queue not in use.
    CCheckQueue<CScriptCheck> *GetScriptCheckQueue();
};

extern std::unique_ptr<CParallelValidation> PV; // Singleton class

#endif // BITCOIN_PARALLEL_H
