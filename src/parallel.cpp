// Copyright (c) 2016 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "parallel.h"
#include "chainparams.h"
#include "pow.h"
#include "timedata.h"
#include "main.h"
#include "unlimited.h"
#include "util.h"
#include "utiltime.h"
#include <map>
#include <string>
#include <vector>

#include <boost/thread/thread.hpp>

uint8_t NUM_SCRIPTCHECKQUEUES = 0;

static CCheckQueue<CScriptCheck> scriptcheckqueue1(128);
static CCheckQueue<CScriptCheck> scriptcheckqueue2(128);
static CCheckQueue<CScriptCheck> scriptcheckqueue3(128);
static CCheckQueue<CScriptCheck> scriptcheckqueue4(128);


using namespace std;


void AddScriptCheckThreads(int i, CCheckQueue<CScriptCheck>* pqueue)
{
    string tName = "bitcoin-scriptchk" + i;
    RenameThread(tName.c_str());
    pqueue->Thread();
}

void AddAllScriptCheckQueuesAndThreads(int nScriptCheckThreads, boost::thread_group* threadGroup)
{
    vector<CCheckQueue<CScriptCheck>* > vScriptCheckQueue;
    vScriptCheckQueue.push_back(&scriptcheckqueue1);
    vScriptCheckQueue.push_back(&scriptcheckqueue2);
    vScriptCheckQueue.push_back(&scriptcheckqueue3);
    vScriptCheckQueue.push_back(&scriptcheckqueue4);

    int i = 1;
    BOOST_FOREACH(CCheckQueue<CScriptCheck>* pqueue, vScriptCheckQueue)
    {
        allScriptCheckQueues.Add(pqueue);
        for (int j = 0; j < nScriptCheckThreads; j++)
            threadGroup->create_thread(boost::bind(&AddScriptCheckThreads, i, pqueue));
        i++;
    }
}

CParallelValidation::CParallelValidation()
{
    mapBlockValidationThreads.clear();
}

bool CParallelValidation::Initialize(const boost::thread::id this_id, const CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    if (chainActive.Tip()->nChainWork > pindex->nChainWork) {
        LogPrintf("returning because chainactive tip is now ahead of chainwork for this block\n");
        return false;
    }


    LOCK(cs_blockvalidationthread);
    CHandleBlockMsgThreads * pValidationThread = &mapBlockValidationThreads[this_id];
    pValidationThread->hash = pindex->GetBlockHash();

    // We need to place a Quit here because we do not want to assign a script queue to a thread of activity
    // if another thread has just won the race and has sent an fQuit.
    if (pValidationThread->fQuit) {
        LogPrint("parallel", "fQuit 0 called - Stopping validation of %s and returning\n", 
                              pValidationThread->hash.ToString());
        return false;
    }

    // Check whether a thread is aleady validating this very same block.  It can happen at times when a block arrives
    // while a previous blocks is still validating or just finishing it's validation and grabs the next block to validate.
    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter = mapBlockValidationThreads.begin();
    while (iter != mapBlockValidationThreads.end()) {
        if ((*iter).second.hash == pindex->GetBlockHash() && (*iter).first != this_id) {
            return false;
        }
        iter++;
    }

    // Assign the nSequenceId for the block being validated in this thread. cs_main must be locked for lookup on pindex.
    if (pindex->nSequenceId > 0)
        pValidationThread->nSequenceId = pindex->nSequenceId;
    
    return true;
}

void CParallelValidation::Cleanup(const CBlock& block, CBlockIndex* pindex)
{
    // First swap the block index sequence id's such that the winning block has the lowest id and all other id's
    // are still in their same order relative to each other.
    // Then terminate all other threads that match our previous blockhash, and cleanup map before updating the tip.  
    // This is in the case where we're doing IBD and we receive two of the same blocks, one a re-request.  
    // Also, this handles an attack vector where someone blasts us with many of the same block.
    LOCK(cs_blockvalidationthread);
    {
        boost::thread::id this_id(boost::this_thread::get_id()); // get this thread's id

        // Create a vector sorted by nSequenceId so that we can iterate through in desc order and adjust the
        // nSequenceId values according to which block won the validation race.
        std::vector<std::pair<uint32_t, uint256> > vSequenceId;
        map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
        while (mi != mapBlockValidationThreads.end())
        {
            if ((*mi).first != this_id && (*mi).second.hashPrevBlock == block.GetBlockHeader().hashPrevBlock)
                vSequenceId.push_back(make_pair((*mi).second.nSequenceId, (*mi).second.hash));
            mi++;
        }
        std::sort(vSequenceId.begin(), vSequenceId.end());

        std::vector<std::pair<uint32_t, uint256> >::reverse_iterator riter = vSequenceId.rbegin();
        while (riter != vSequenceId.rend())
        {
            // Swap the nSequenceId so that we end up with the lowest index for the winning block.  This is so
            // later if we need to look up pindexMostWork it will be pointing to this winning block.
            if (pindex->nSequenceId > (*riter).first) {
                uint32_t nId = pindex->nSequenceId;
                if (nId == 0) nId = 1;
                if ((*riter).first == 0) (*riter).first = 1;
                    LogPrint("parallel", "swapping sequence id for block %s before %d after %d\n", 
                              block.GetHash().ToString(), pindex->nSequenceId, (*riter).first);
                pindex->nSequenceId = (*riter).first;
                (*riter).first = nId;

                BlockMap::iterator it = mapBlockIndex.find((*riter).second);
                if (it != mapBlockIndex.end())
                    it->second->nSequenceId = nId;
            }
            riter++;
        }

        map<boost::thread::id, CHandleBlockMsgThreads>::iterator iter = mapBlockValidationThreads.begin();
        while (iter != mapBlockValidationThreads.end())
        {
            map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = iter++; // increment to avoid iterator becoming 
            // Interrupt threads:  We want to stop any threads that have lost the validation race. We have to compare
            //                     at the previous block hashes to make the determination.  If they match then it must
            //                     be a parallel block validation that was happening.
            if ((*mi).first != this_id && (*mi).second.hashPrevBlock == block.GetBlockHeader().hashPrevBlock) {
                if ((*mi).second.pScriptQueue != NULL) {
                    LogPrint("parallel", "Terminating script queue with blockhash %s and previous blockhash %s\n", 
                              (*mi).second.hash.ToString(), block.GetBlockHeader().hashPrevBlock.ToString());
                    // Send Quit to any other scriptcheckques that were running a parallel validation for the same block.
                    // NOTE: the scriptcheckqueue may or may not have finished, but sending a quit here ensures
                    // that it breaks from its processing loop in the event that it is still at the control.Wait() step.
                    // This allows us to end any long running block validations and allow a smaller block to begin 
                    // processing when/if all the queues have been jammed by large blocks during an attack.
                    LogPrint("parallel", "Sending Quit() to scriptcheckqueue\n");
                    (*mi).second.pScriptQueue->Quit();
                }
                (*mi).second.fQuit = true; // quit the thread
                LogPrint("parallel", "interrupting a thread with blockhash %s and previous blockhash %s\n", 
                          (*mi).second.hash.ToString(), block.GetBlockHeader().hashPrevBlock.ToString());
            }
            // Clear the scriptqueue before returning so that we can grab it again if we have another block to process
            // using this same thread.
            else if ((*mi).first == this_id)
                mapBlockValidationThreads[this_id].pScriptQueue = NULL;
        }
    }
}

bool CParallelValidation::IsAlreadyValidating(const NodeId nodeid)
{
    // Don't allow a second thinblock to validate if this node is already in the process of validating a block.
    boost::thread::id this_id(boost::this_thread::get_id());
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter = mapBlockValidationThreads.begin();
    while (iter != mapBlockValidationThreads.end()) {
        if ((*iter).second.nodeid == nodeid) {
            return true;
        }
        iter++;
    }
    return false;
}

void CParallelValidation::StopAllValidationThreads()
{
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
    while (mi != mapBlockValidationThreads.end())
    {
        if ((*mi).second.pScriptQueue != NULL) {
            (*mi).second.pScriptQueue->Quit(); // interrupt any running script threads
        }
        (*mi).second.fQuit = true; // quit the PV thread
        mi++;
    }
}

void CParallelValidation::StopAllValidationThreads(const boost::thread::id this_id)
{
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
    while (mi != mapBlockValidationThreads.end())
    {
        if ((*mi).first != this_id) // we don't want to kill our own thread
        { 
            if ((*mi).second.pScriptQueue != NULL)
                (*mi).second.pScriptQueue->Quit(); // quit any active script queue threads
            (*mi).second.fQuit = true; // quit the PV thread
        }
        mi++;
    }
}

void CParallelValidation::WaitForAllValidationThreadsToStop()
{
    // Wait for threads to finish and cleanup
    while (true) {
        // We must unlock before sleeping so that any blockvalidation threads 
        // that are quitting can grab the lock and cleanup
        {
        LOCK(cs_blockvalidationthread);
        if (mapBlockValidationThreads.size() == 0)
            break;
        }
        MilliSleep(100);
    }
}

bool CParallelValidation::Enabled()
{
    return GetBoolArg("-parallel", true);
}

void CParallelValidation::Erase()
{
    boost::thread::id this_id(boost::this_thread::get_id()); 
    LOCK(cs_blockvalidationthread);
    if (mapBlockValidationThreads.count(this_id))
        mapBlockValidationThreads.erase(this_id);
}

bool CParallelValidation::QuitReceived(const boost::thread::id this_id)
{
    LOCK(cs_blockvalidationthread);
    if (mapBlockValidationThreads[this_id].fQuit) {
        LogPrint("parallel", "fQuit called - Stopping validation of this block and returning\n");
        return true;
    }
    return false;
}

bool CParallelValidation::ChainWorkHasChanged(const arith_uint256& nStartingChainWork) // requires cs_main
{
    LOCK(cs_main);
    if (chainActive.Tip()->nChainWork != nStartingChainWork)
    {
        LogPrint("parallel", "Quitting - Chain Work %s is not the same as the starting Chain Work %s\n",
                              chainActive.Tip()->nChainWork.ToString(), nStartingChainWork.ToString());
        return true;
    }
    return false;
}

void CParallelValidation::SetLocks()
{
    // We must maintain locking order with cs_main, therefore we must make sure the scoped
    // scriptcheck lock is unlocked prior to locking cs_main.  That is because in the case
    // where we are not running in parallel the cs_main lock is aquired before the scoped
    // lock and we do not want to then do the reverse here and aquire cs_main "after" the scoped
    // lock is held, thereby reversing the locking order and creating a possible deadlock.
    // Therefore to remedy the situation we simply unlock the scriptlock and do not enter into
    // any reversal of the locking order.
    cs_main.lock();

    boost::thread::id this_id(boost::this_thread::get_id()); 
    LOCK(cs_blockvalidationthread);
    {
         if (mapBlockValidationThreads.count(this_id))
             mapBlockValidationThreads[this_id].pScriptQueue = NULL;
    }
}

//  HandleBlockMessage launches a HandleBlockMessageThread.  And HandleBlockMessageThread processes each block and updates 
//  the UTXO if the block has been accepted and the tip updated. We cleanup and release the semaphore after the thread has finished.
void CParallelValidation::HandleBlockMessage(CNode *pfrom, const string &strCommand, const CBlock &block, const CInv &inv)
{
    uint64_t nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    uint8_t nScriptCheckQueues = allScriptCheckQueues.Size();

    /** Initialize Semaphores used to limit the total number of concurrent validation threads. */
    if (semPV == NULL)
        semPV = new CSemaphore(nScriptCheckQueues);
  
    // NOTE: You must not have a cs_main lock before you aquire the semaphore grant or you can end up deadlocking
    //AssertLockNotHeld(cs_main); TODO: need to create this

    // Aquire semaphore grant
    if (IsChainNearlySyncd()) {
        if (!semPV->try_wait()) {

            /** The following functionality is for the case when ALL thread queues and grants are in use, meaning somehow an attacker
             *  has been able to craft blocks or sustain an attack in such a way as to use up every availabe thread queue.
             *  When/If that should occur, we must assume we are under a sustained attack and we will have to make a determination
             *  as to which of the currently running threads we should terminate based on the following critera:
             *     1) If all queues are in use and another block arrives, then terminate the running thread validating the largest block
             */

            {
                LOCK(cs_blockvalidationthread);
                if (mapBlockValidationThreads.size() >= nScriptCheckQueues)
                {
                    uint64_t nLargestBlockSize = 0;
                    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator miLargestBlock;
                    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter = mapBlockValidationThreads.begin();
                    while (iter != mapBlockValidationThreads.end())
                    {
                        // Find largest block where the previous block hash matches. Meaning this is a new block and it's a competitor to to your block.
                        map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator mi = iter++;
                        if ((*mi).second.hashPrevBlock == block.GetBlockHeader().hashPrevBlock) {
                            if ((*mi).second.nBlockSize > nLargestBlockSize) {
                                nLargestBlockSize = (*mi).second.nBlockSize;
                                miLargestBlock = mi;
                            }
                        }
                    }

                    // if your block is the biggest or of equal size to the biggest then reject it.
                    if (nLargestBlockSize <= nBlockSize) {
                        LogPrint("parallel", "Block validation terminated - Too many blocks currently being validated: %s\n", block.GetHash().ToString());
                        return; // new block is rejected and does not enter PV
                    }
                    else { // terminate the chosen PV thread
                        (*miLargestBlock).second.pScriptQueue->Quit(); // terminate the script queue threads
                        LogPrint("parallel", "Sending Quit() to scriptcheckqueue\n");
                        (*miLargestBlock).second.fQuit = true; // terminate the PV thread
                        LogPrint("parallel", "Too many blocks being validated, interrupting thread with blockhash %s and previous blockhash %s\n", 
                               (*miLargestBlock).second.hash.ToString(), (*miLargestBlock).second.hashPrevBlock.ToString());
                    }
                }
                else
                    assert("No grant possible, but no validation threads are running!");

            } // We must not hold the lock here because we could be waiting for a grant, below.

            // wait for semaphore grant
            semPV->wait();
        }
    }
    else { // for IBD just wait for the next available
        semPV->wait();
    }

    boost::thread * thread = new boost::thread(boost::bind(&HandleBlockMessageThread, pfrom, strCommand, block, inv));
    {

        LOCK(cs_blockvalidationthread);
        CParallelValidation::CHandleBlockMsgThreads * pValidationThread = &mapBlockValidationThreads[thread->get_id()];
        pValidationThread->tRef = thread;
        pValidationThread->pScriptQueue = NULL;
        pValidationThread->hash = inv.hash;
        pValidationThread->hashPrevBlock = block.GetBlockHeader().hashPrevBlock;
        pValidationThread->nSequenceId = INT_MAX;
        pValidationThread->nStartTime = GetTimeMillis();
        pValidationThread->nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        pValidationThread->fQuit = false;
        pValidationThread->nodeid = pfrom->id;
        LogPrint("parallel", "Launching validation for %s with number of block validation threads running: %d\n", 
                              block.GetHash().ToString(), mapBlockValidationThreads.size());

        thread->detach();
   }
}
void HandleBlockMessageThread(CNode *pfrom, const string &strCommand, const CBlock &block, const CInv &inv)
{

    int64_t startTime = GetTimeMicros();
    CValidationState state;
    uint64_t nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);

    // Process all blocks from whitelisted peers, even if not requested,
    // unless we're still syncing with the network.
    // Such an unrequested block may still be processed, subject to the
    // conditions in AcceptBlock().
    bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
    const CChainParams& chainparams = Params();
    pfrom->firstBlock += 1;
    ProcessNewBlock(state, chainparams, pfrom, &block, forceProcessing, NULL, PV.Enabled());
    int nDoS;
    if (state.IsInvalid(nDoS)) {
        LogPrintf("Invalid block due to %s\n", state.GetRejectReason().c_str());
        if (!strCommand.empty()) {
            pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                                state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), nDoS);
            }
	}
    }
    else {
        LargestBlockSeen(nSizeBlock); // update largest block seen

        double nValidationTime = (double)(GetTimeMicros() - startTime) / 1000000.0;
        if (strCommand != NetMsgType::BLOCK) {
            LogPrint("thin", "Processed ThinBlock %s in %.2f seconds\n", inv.hash.ToString(), (double)(GetTimeMicros() - startTime) / 1000000.0);
            thindata.UpdateValidationTime(nValidationTime);
        }
        else
            LogPrint("thin", "Processed Regular Block %s in %.2f seconds\n", inv.hash.ToString(), (double)(GetTimeMicros() - startTime) / 1000000.0);
    }

    // When we request a thinblock we may get back a regular block if it is smaller than a thinblock
    // Therefore we have to remove the thinblock in flight if it exists and we also need to check that 
    // the block didn't arrive from some other peer.  This code ALSO cleans up the thin block that
    // was passed to us (&block), so do not use it after this.
    {
        int nTotalThinBlocksInFlight = 0;
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (pnode->mapThinBlocksInFlight.count(inv.hash)) {
                    pnode->mapThinBlocksInFlight.erase(inv.hash); 
                    pnode->thinBlockWaitingForTxns = -1;
                    pnode->thinBlock.SetNull();
                }
                if (pnode->mapThinBlocksInFlight.size() > 0)
                    nTotalThinBlocksInFlight++;
            }
        }

        // When we no longer have any thinblocks in flight then clear the set
        // just to make sure we don't somehow get growth over time.
        if (nTotalThinBlocksInFlight == 0) {
            LOCK(cs_xval);
            setPreVerifiedTxHash.clear();
            setUnVerifiedOrphanTxHash.clear();
        }
    }

    // Clear the thinblock timer used for preferential download
    thindata.ClearThinBlockTimer(inv.hash);

    // Erase any txns in the block from the orphan cache as they are no longer needed
    if (IsChainNearlySyncd()) {
        LOCK(cs_orphancache);
        for (unsigned int i = 0; i < block.vtx.size(); i++)
            EraseOrphanTx(block.vtx[i].GetHash());
    }
    
    // Clear thread data - this must be done before the thread completes or else some other new
    // thread may grab the same thread id and we would end up deleting the entry for the new thread instead.
    PV.Erase();
 
    // release semaphores depending on whether this was IBD or not.  We can not use IsChainNearlySyncd()
    // because the return value will switch over when IBD is nearly finished and we may end up not releasing
    // the correct semaphore.
    {
    LOCK(cs_semPV);
    semPV->post();
    }
}


CCheckQueue<CScriptCheck>* CAllScriptCheckQueues::GetScriptCheckQueue()
{
    // for newly mined block validation, return the first queue not in use.
    CCheckQueue<CScriptCheck>* pqueue = NULL;
    if (Size() > 0) {
        while(true)
        {
            {
            LOCK2(PV.cs_blockvalidationthread, cs);
            for (unsigned int i = 0; i < vScriptCheckQueues.size(); i++) {
                if (vScriptCheckQueues[i]->IsIdle()) {
                    bool inUse = false;
                    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter = PV.mapBlockValidationThreads.begin();
                    while (iter != PV.mapBlockValidationThreads.end())
                    {
                        if ((*iter).second.pScriptQueue == vScriptCheckQueues[i]) {
                            inUse = true;
                            break;
                        }  
                        iter++;
                    }
                    if (!inUse) {
                        pqueue = vScriptCheckQueues[i];
                        pqueue->Quit(false); // set to false because it still may be set to true from last run.
    
                        boost::thread::id this_id(boost::this_thread::get_id());
                        if (PV.mapBlockValidationThreads.count(this_id))
                            PV.mapBlockValidationThreads[this_id].pScriptQueue = pqueue;

                        LogPrint("parallel", "next scriptqueue not in use is %d\n", i);
                        return pqueue;
                    }
                }
            }
            }
            LogPrint("parallel", "Sleeping 50 millis\n");
            MilliSleep(50);
        }
    }
    assert(pqueue != NULL);
    return pqueue;
}

