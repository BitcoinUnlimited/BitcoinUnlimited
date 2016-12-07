// Copyright (c) 2016 The Bitcoin Unlimited developers
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

using namespace std;

extern CCriticalSection cs_blockvalidationthread;

static CCheckQueue<CScriptCheck> scriptcheckqueue1(128);
static CCheckQueue<CScriptCheck> scriptcheckqueue2(128);
static CCheckQueue<CScriptCheck> scriptcheckqueue3(128);
static CCheckQueue<CScriptCheck> scriptcheckqueue4(128);


void ThreadScriptCheck1();
void ThreadScriptCheck2();
void ThreadScriptCheck3();
void ThreadScriptCheck4();

void AddAllScriptCheckQueues();



/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction 
 */
class CScriptCheck
{
private:
    CScript scriptPubKey;
    const CTransaction *ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;

public:
    CScriptCheck(): ptxTo(0), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR) {}
    CScriptCheck(const CCoins& txFromIn, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn) :
        scriptPubKey(txFromIn.vout[txToIn.vin[nInIn].prevout.n].scriptPubKey),
        ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR) { }

    bool operator()();

    void swap(CScriptCheck &check) {
        scriptPubKey.swap(check.scriptPubKey);
        std::swap(ptxTo, check.ptxTo);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
    }

    ScriptError GetScriptError() const { return error; }
};

/**
 * Hold all script check queues in one vector along with their associated mutex
 * When we use a queue we must always have a distinct mutex for it.  This way during IBD we
 * can repeately lock a queue without needing to worry about scheduling threads.  They'll just
 * wait until they're free to continue.  During parallel validation of new blocks we'll only
 * have the maximum number of 4 queues/blocks validating so locking won't be needed for scheduling in that case.
 */
class CAllScriptCheckQueues
{
private:
    class CScriptCheckQueue
    {
        public:
            CCheckQueue<CScriptCheck>* scriptcheckqueue;
            bool InUse;
            boost::shared_ptr<boost::mutex> scriptcheck_mutex;

            CScriptCheckQueue(CCheckQueue<CScriptCheck>* pqueueIn) : InUse(false), scriptcheck_mutex(new boost::mutex)
            {
                scriptcheckqueue = pqueueIn;
            }
    };
    std::vector<CScriptCheckQueue> vScriptCheckQueues;
    boost::shared_ptr<boost::mutex> dummy_mutex;

public:
    CAllScriptCheckQueues() : dummy_mutex(new boost::mutex) {}

    void Add(CCheckQueue<CScriptCheck>* pqueueIn)
    {
        vScriptCheckQueues.push_back(CScriptCheckQueue(pqueueIn));
    }

    /* Returns a pointer to an available or selected scriptcheckqueue.
     * 1) during IBD each queue is selected in order.  There is no need to check if the queue is busy or not.
     * 2) for new block validation there is a more complex selection process and also the ability to terminate long
     *    running threads in the case where there are more requests for validation than queues.
     */
    CCheckQueue<CScriptCheck>* GetScriptCheckQueue(boost::shared_ptr<boost::mutex> mutex)
    {
        // find the scriptcheckqueue that is associated with this mutex
        for (unsigned int i = 0; i < vScriptCheckQueues.size(); i++) {
            if (vScriptCheckQueues[i].scriptcheck_mutex == mutex) {
                LogPrint("parallel", "next script check queue selected is %d\n", i);
                return vScriptCheckQueues[i].scriptcheckqueue;
            }
        }
        return  NULL;
    }

    boost::shared_ptr<boost::mutex> GetScriptCheckMutex()
    {
        // for newly mined block validation, return the first queue not in use.
        if (IsChainNearlySyncd() && vScriptCheckQueues.size() > 0) {
            for (unsigned int i = 0; i < vScriptCheckQueues.size(); i++) {
                if (vScriptCheckQueues[i].scriptcheckqueue->IsIdle()) {
                    LogPrint("parallel", "next mutex not in use is %d\n", i);
                    return vScriptCheckQueues[i].scriptcheck_mutex;
                }
                else 
                    assert("Could not select Queue since none were idle");
            }
        }
        // for IBD return the next queue. It doesn't matter if it's in use or not.
        else if (vScriptCheckQueues.size() > 0){
            static unsigned int nextQueue = 0;
            nextQueue++;

            if (nextQueue >= vScriptCheckQueues.size())
                nextQueue = 0;
            LogPrint("parallel", "next mutex selected is %d\n", nextQueue);
            return vScriptCheckQueues[nextQueue].scriptcheck_mutex;
        }
        return dummy_mutex;
    }
};
extern CAllScriptCheckQueues allScriptCheckQueues; // Singleton class

class CParallelValidation
{
public:
    struct CHandleBlockMsgThreads {
        boost::thread* tRef;
        CCheckQueue<CScriptCheck>* pScriptQueue;
        uint256 hash;
        uint256 hashPrevBlock;
        uint32_t nSequenceId;
        int64_t nStartTime;
        uint64_t nBlockSize;
        bool fQuit;
    };
    map<boost::thread::id, CHandleBlockMsgThreads> mapBlockValidationThreads GUARDED_BY(cs_blockvalidationthread);


public:
    CParallelValidation();

    /* Initialize a PV thread */
    bool Initialize(const boost::thread::id this_id, const CBlockIndex* pindex, CCheckQueue<CScriptCheck>* pScriptQueue);

    /* Cleanup PV threads after one has finished and won the validation race */
    void Cleanup(const CBlock& block, CBlockIndex* pindex);

    /* Terminate All currently running Block Validation threads */
    void StopAllValidationThreads();
    void StopAllValidationThreads(const boost::thread::id this_id);
    void WaitForAllValidationThreadsToStop();

    /* Has parallel block validation been turned on via the config settings */
    bool Enabled();

    /* Clear thread data from mapBlockValidationThreads */
    void Erase();

    /* Was the fQuit flag set to true which causes the PV thread to exit */
    bool QuitReceived(const boost::thread::id this_id);

};
extern CParallelValidation PV;  // Singleton class



#endif // BITCOIN_PARALLEL_H
