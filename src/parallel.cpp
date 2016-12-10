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

using namespace std;


void ThreadScriptCheck1() {
    RenameThread("bitcoin-scriptch");
    scriptcheckqueue1.Thread();
}
void ThreadScriptCheck2() {
    RenameThread("bitcoin-scriptch2");
    scriptcheckqueue2.Thread();
}
void ThreadScriptCheck3() {
    RenameThread("bitcoin-scriptch3");
    scriptcheckqueue3.Thread();
}
void ThreadScriptCheck4() {
    RenameThread("bitcoin-scriptch4");
    scriptcheckqueue4.Thread();
}

void AddAllScriptCheckQueues()
{
    allScriptCheckQueues.Add(&scriptcheckqueue1);
    allScriptCheckQueues.Add(&scriptcheckqueue2);
    allScriptCheckQueues.Add(&scriptcheckqueue3);
    allScriptCheckQueues.Add(&scriptcheckqueue4);
}

CParallelValidation::CParallelValidation()
{
    mapBlockValidationThreads.clear();
}

bool CParallelValidation::Initialize(const boost::thread::id this_id, const CBlockIndex* pindex, CCheckQueue<CScriptCheck>* pScriptQueue)
{

    ENTER_CRITICAL_SECTION(cs_blockvalidationthread);
    CHandleBlockMsgThreads * pValidationThread = &mapBlockValidationThreads[this_id];

    // We need to place a Quit here because we do not want to assign a script queue to a thread of activity
    // if another thread has just won the race and has sent an fQuit.
    if (pValidationThread->fQuit) {
        LogPrint("parallel", "fQuit 0 called - Stopping validation of %s and returning\n", 
                              pValidationThread->hash.ToString());
        LEAVE_CRITICAL_SECTION(cs_blockvalidationthread); // must unlock before locking cs_main or may deadlock.
        cs_main.lock(); // must lock before returning.
        return false;
    }

    // Now that we have a scriptqueue we can add it to the tracking map so we can call Quit() on it later if needed.
    pValidationThread->pScriptQueue = pScriptQueue;
    LEAVE_CRITICAL_SECTION(cs_blockvalidationthread); // must unlock before re-aquire cs_main below or may deadlock.

    // Re-aquire cs_main
    cs_main.lock();
    // Assign the nSequenceId for the block being validated in this thread. cs_main must be locked for lookup on pindex.
    LOCK(cs_blockvalidationthread);
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
        }
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
    if (chainActive.Tip()->nChainWork != nStartingChainWork)
    {
        LogPrint("parallel", "Quitting - Chain Work %s is not the same as the starting Chain Work %s\n",
                              chainActive.Tip()->nChainWork.ToString(), nStartingChainWork.ToString());
        return true;
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

