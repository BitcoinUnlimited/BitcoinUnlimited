// Copyright (c) 2016-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "parallel.h"

#include "blockrelay/blockrelay_common.h"
#include "blockrelay/compactblock.h"
#include "blockrelay/graphene.h"
#include "blockstorage/blockstorage.h"
#include "chainparams.h"
#include "dosman.h"
#include "net.h"
#include "pow.h"
#include "requestManager.h"
#include "script/sigcache.h"
#include "timedata.h"
#include "txorphanpool.h"
#include "unlimited.h"
#include "util.h"
#include "utiltime.h"
#include "validation/validation.h"
#include <map>
#include <string>
#include <vector>

#include <boost/thread/thread.hpp>

using namespace std;

// see at doc/bu-parallel-validation.md to get the details
static const unsigned int nScriptCheckQueues = 4;

std::unique_ptr<CParallelValidation> PV;

bool ShutdownRequested();
static void HandleBlockMessageThread(CNodeRef noderef, const string strCommand, CBlockRef pblock, const CInv inv);

static void AddScriptCheckThreads(int i, CCheckQueue<CScriptCheck> *pqueue)
{
    ostringstream tName;
    tName << "scriptchk" << i;
    RenameThread(tName.str().c_str());
    pqueue->Thread();
}

bool CScriptCheck::operator()()
{
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    CachingTransactionSignatureChecker checker(ptxTo, nIn, amount, nFlags, cacheStore);
    ScriptMachineResourceTracker smRes;
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, maxOps, checker, &error, &smRes))
    {
        LOGA("Script Error: %s\n", ScriptErrorString(error));
        return false;
    }
    if (resourceTracker)
    {
        resourceTracker->Update(ptxTo->GetHash(), checker.GetNumSigops(), checker.GetBytesHashed());
        resourceTracker->UpdateConsensusSigChecks(smRes.consensusSigCheckCount);
    }
    if (nFlags & SCRIPT_VERIFY_INPUT_SIGCHECKS)
    {
        auto lenScriptSig = scriptSig.size();
        // May 2020 transaction input standardness rule
        // if < 2 scriptsig len is allowed to be 0 (len formula goes negative)
        if ((smRes.consensusSigCheckCount > 1) && ((smRes.consensusSigCheckCount * 43) - 60 > lenScriptSig))
        {
            error = SIGCHECKS_LIMIT_EXCEEDED;
            LOGA("Sigchecks limit exceeded, with %d sigchecks: min script length (%d) > satisfier script len (%d)",
                smRes.consensusSigCheckCount, (smRes.consensusSigCheckCount * 43) - 60, lenScriptSig);
            return false;
        }
    }
    return true;
}

CParallelValidation::CParallelValidation() : nThreads(0), semThreadCount(nScriptCheckQueues)
{
    // There are nScriptCheckQueues which are used to validate blocks in parallel. Each block
    // that validates will use one script check queue which must *not* be shared with any other
    // validating block. Furthermore, each script check queue has a number of threads which it
    // controls and which do the actual validating of scripts.

    // Determine the number of threads to use for each check queue.
    //
    //-par=0 means autodetect number of cores.
    nThreads = GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (nThreads <= 0)
    {
        nThreads += GetNumCores();
    }

    // Must always assign at least one thread in case GetNumCores() fails.
    if (nThreads <= 0)
    {
        nThreads = 1;
    }
    else if (nThreads > MAX_SCRIPTCHECK_THREADS)
    {
        nThreads = MAX_SCRIPTCHECK_THREADS;
    }

    // Create each script check queue with all associated threads.
    LOGA("Launching %d ScriptQueues each using %d threads for script verification\n", nScriptCheckQueues, nThreads);
    while (QueueCount() < nScriptCheckQueues)
    {
        auto queue = new CCheckQueue<CScriptCheck>(128);
        for (unsigned int i = 1; i <= nThreads; i++)
        {
            threadGroup.create_thread(boost::bind(&AddScriptCheckThreads, i + 1, queue));
        }
        vQueues.push_back(queue);
    }

    // Must always have at least one scriptcheck thread running or we'll
    // end up not validating signatures when new blocks are mined.
    DbgAssert(nThreads >= 1, nThreads = 1);
}

CParallelValidation::~CParallelValidation()
{
    for (auto queue : vQueues)
        queue->Shutdown();
    threadGroup.join_all();
    for (auto queue : vQueues)
        delete queue;
}

unsigned int CParallelValidation::QueueCount()
{
    // Only modified in constructor so no lock currently needed
    return vQueues.size();
}

bool CParallelValidation::Initialize(const boost::thread::id this_id, const CBlockIndex *pindex, const bool fParallel)
{
    AssertLockHeld(cs_main);

    if (fParallel)
    {
        // If the chain tip has passed this block by, its an orphan.  It cannot be connected to the active chain, so
        // return.
        if (chainActive.Tip()->nChainWork > pindex->nChainWork)
        {
            LOGA("returning because chainactive tip is now ahead of chainwork for this block\n");
            return false;
        }

        LOCK(cs_blockvalidationthread);
        assert(mapBlockValidationThreads.count(this_id) > 0);
        CHandleBlockMsgThreads *pValidationThread = &mapBlockValidationThreads[this_id];
        pValidationThread->hash = pindex->GetBlockHash();

        // We need to place a Quit here because we do not want to assign a script queue to a thread of activity
        // if another thread has just won the race and has sent an fQuit.
        if (pValidationThread->fQuit)
        {
            LOG(PARALLEL, "fQuit 0 called - Stopping validation of %s and returning\n",
                pValidationThread->hash.ToString());
            return false;
        }

        // Check whether a thread is aleady validating this very same block.  It can happen at times when a block
        // arrives while a previous blocks is still validating or just finishing it's validation and grabs the next
        // block to validate.
        map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter =
            mapBlockValidationThreads.begin();
        while (iter != mapBlockValidationThreads.end())
        {
            if ((*iter).second.hash == pindex->GetBlockHash() && (*iter).second.fIsValidating &&
                !(*iter).second.fQuit && (*iter).first != this_id)
            {
                LOG(PARALLEL, "Returning because another thread is already validating this block\n");
                return false;
            }
            iter++;
        }

        // Assign the nSequenceId for the block being validated in this thread. cs_main must be locked for lookup on
        // pindex.
        {
            READLOCK(cs_mapBlockIndex);
            if (pindex->nSequenceId > 0)
                pValidationThread->nSequenceId = pindex->nSequenceId;
        }
        pValidationThread->fIsValidating = true;
    }

    return true;
}

void CParallelValidation::Cleanup(const CBlock &block, CBlockIndex *pindex)
{
    // Swap the block index sequence id's such that the winning block has the lowest id and all other id's
    // are still in their same order relative to each other.
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

        WRITELOCK(cs_mapBlockIndex);
        std::vector<std::pair<uint32_t, uint256> >::reverse_iterator riter = vSequenceId.rbegin();
        while (riter != vSequenceId.rend())
        {
            // Swap the nSequenceId so that we end up with the lowest index for the winning block.  This is so
            // later if we need to look up pindexMostWork it will be pointing to this winning block.
            if (pindex->nSequenceId > (*riter).first)
            {
                uint32_t nId = pindex->nSequenceId;
                if (nId == 0)
                    nId = 1;
                if ((*riter).first == 0)
                    (*riter).first = 1;
                LOG(PARALLEL, "swapping sequence id for block %s before %d after %d\n", block.GetHash().ToString(),
                    pindex->nSequenceId, (*riter).first);
                pindex->nSequenceId = (*riter).first;
                (*riter).first = nId;

                BlockMap::iterator it = mapBlockIndex.find((*riter).second);
                if (it != mapBlockIndex.end())
                    it->second->nSequenceId = nId;
            }
            riter++;
        }
    }
}

void CParallelValidation::QuitCompetingThreads(const uint256 &prevBlockHash)
{
    // Kill other competing threads but not this one.
    LOCK(cs_blockvalidationthread);
    {
        boost::thread::id this_id(boost::this_thread::get_id()); // get this thread's id

        map<boost::thread::id, CHandleBlockMsgThreads>::iterator iter = mapBlockValidationThreads.begin();
        while (iter != mapBlockValidationThreads.end())
        {
            map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi =
                iter++; // increment to avoid iterator becoming
            // Interrupt threads:  We want to stop any threads that have lost the validation race. We have to compare
            //                     at the previous block hashes to make the determination.  If they match then it must
            //                     be a parallel block validation that was happening.
            if ((*mi).first != this_id && (*mi).second.hashPrevBlock == prevBlockHash)
            {
                Quit(mi);
                LOG(PARALLEL, "Interruping a PV thread with blockhash %s and previous blockhash %s\n",
                    (*mi).second.hash.ToString(), prevBlockHash.ToString());
            }
        }
    }
}

bool CParallelValidation::IsAlreadyValidating(const NodeId nodeid, const uint256 blockhash)
{
    // Don't allow a second thinblock to validate if this node is already in the process of validating a block.
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter =
        mapBlockValidationThreads.begin();
    while (iter != mapBlockValidationThreads.end())
    {
        if ((*iter).second.nodeid == nodeid && (*iter).second.hash == blockhash)
        {
            return true;
        }
        iter++;
    }
    return false;
}

void CParallelValidation::StopAllValidationThreads(const boost::thread::id this_id)
{
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
    while (mi != mapBlockValidationThreads.end())
    {
        if ((*mi).first != this_id) // we don't want to kill our own thread
        {
            Quit(mi);
        }
        mi++;
    }
}

void CParallelValidation::StopAllValidationThreads(const uint32_t nChainWork)
{
    boost::thread::id this_id(boost::this_thread::get_id());

    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
    while (mi != mapBlockValidationThreads.end())
    {
        // Kill any threads that have less than or equal to our own chain work we are working on.  We use
        // this method when we're mining our own block.  In that event we want to give priority to our own
        // block rather than any competing block or chain.
        if (((*mi).first != this_id) && (*mi).second.nChainWork <= nChainWork &&
            (*mi).second.nMostWorkOurFork <= nChainWork)
        {
            Quit(mi);
        }
        mi++;
    }
}

void CParallelValidation::WaitForAllValidationThreadsToStop()
{
    // Wait for threads to finish and cleanup
    while (true)
    {
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

bool CParallelValidation::Enabled() { return GetBoolArg("-parallel", true); }
void CParallelValidation::InitThread(const boost::thread::id this_id,
    const CNode *pfrom,
    CBlockRef pblock,
    const CInv &inv,
    uint64_t blockSize)
{
    const CBlockHeader &header = pblock->GetBlockHeader();

    LOCK(cs_blockvalidationthread);
    assert(mapBlockValidationThreads.count(this_id) == 0); // this id should not already be in use
    mapBlockValidationThreads.emplace(
        this_id, CHandleBlockMsgThreads{nullptr, inv.hash, header.hashPrevBlock, header.nBits, header.nBits, INT_MAX,
                     GetTimeMillis(), blockSize, false, pfrom->id, false, false});

    LOG(PARALLEL, "Launching validation for %s with number of block validation threads running: %d\n",
        pblock->GetHash().ToString(), mapBlockValidationThreads.size());
}

void CParallelValidation::Erase(const boost::thread::id this_id)
{
    LOCK(cs_blockvalidationthread);
    if (mapBlockValidationThreads.count(this_id))
        mapBlockValidationThreads.erase(this_id);
}

void CParallelValidation::Quit(std::map<boost::thread::id, CHandleBlockMsgThreads>::iterator iter)
{
    AssertLockHeld(cs_blockvalidationthread);
    LOG(PARALLEL, "Sending Quit() to PV thread and associated script validation threads\n");

    // Quit script validation threads
    if (iter->second.pScriptQueue != nullptr)
        iter->second.pScriptQueue->Quit();

    // Send signal for PV thread to exit
    iter->second.fQuit = true;
}

bool CParallelValidation::QuitReceived(const boost::thread::id this_id, const bool fParallel)
{
    if (fParallel)
    {
        LOCK(cs_blockvalidationthread);
        if (mapBlockValidationThreads.count(this_id))
        {
            if (mapBlockValidationThreads[this_id].fQuit)
            {
                LOG(PARALLEL, "fQuit called - Stopping validation of this block and returning\n");
                return true;
            }
        }
    }
    return false;
}

bool CParallelValidation::ChainWorkHasChanged(const arith_uint256 &nStartingChainWork)
{
    if (chainActive.Tip()->nChainWork != nStartingChainWork)
    {
        LOG(PARALLEL, "Quitting - Chain Work %s is not the same as the starting Chain Work %s\n",
            chainActive.Tip()->nChainWork.ToString(), nStartingChainWork.ToString());
        return true;
    }
    return false;
}

void CParallelValidation::SetLocks(const bool fParallel)
{
    if (fParallel)
    {
        boost::thread::id this_id(boost::this_thread::get_id());
        {
            LOCK(cs_blockvalidationthread);
            if (mapBlockValidationThreads.count(this_id))
                mapBlockValidationThreads[this_id].pScriptQueue = nullptr;
        }
        // cs_main must be re-locked before returning from ConnectBlock()
        ENTER_CRITICAL_SECTION(cs_main);
    }
}

void CParallelValidation::MarkReorgInProgress(const boost::thread::id this_id, const bool fReorg, const bool fParallel)
{
    if (fParallel)
    {
        LOCK(cs_blockvalidationthread);
        if (mapBlockValidationThreads.count(this_id))
            mapBlockValidationThreads[this_id].fIsReorgInProgress = fReorg;
    }
}

bool CParallelValidation::IsReorgInProgress()
{
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
    while (mi != mapBlockValidationThreads.end())
    {
        if ((*mi).second.fIsReorgInProgress)
            return true;
        mi++;
    }
    return false;
}

void CParallelValidation::UpdateMostWorkOurFork(const CBlockHeader &header)
{
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
    while (mi != mapBlockValidationThreads.end())
    {
        // check if this new header connects to this block and if so then update the nMostWorkOurFork
        if ((*mi).second.hash == header.hashPrevBlock && (*mi).second.nMostWorkOurFork < header.nBits)
            (*mi).second.nMostWorkOurFork = header.nBits;
        mi++;
    }
}

uint32_t CParallelValidation::MaxWorkChainBeingProcessed()
{
    uint32_t nMaxWork = 0;
    LOCK(cs_blockvalidationthread);
    map<boost::thread::id, CHandleBlockMsgThreads>::iterator mi = mapBlockValidationThreads.begin();
    while (mi != mapBlockValidationThreads.end())
    {
        if ((*mi).second.nChainWork > nMaxWork)
            nMaxWork = (*mi).second.nChainWork;
        mi++;
    }
    return nMaxWork;
}

void CParallelValidation::ClearOrphanCache(const CBlockRef pblock)
{
    if (!IsInitialBlockDownload())
    {
        WRITELOCK(orphanpool.cs_orphanpool);
        {
            // Erase any orphans that may have been in the previous block and arrived
            // after the previous block had already been processed.
            LOCK(cs_previousblock);
            for (uint256 &hash : vPreviousBlock)
            {
                orphanpool.EraseOrphanTx(hash);
            }
            vPreviousBlock.clear();

            // Erase orphans from the current block that were already received.
            for (auto &tx : pblock->vtx)
            {
                uint256 hash = tx->GetHash();
                vPreviousBlock.push_back(hash);
                orphanpool.EraseOrphanTx(hash);
            }
        }
    }
}


//  HandleBlockMessage launches a HandleBlockMessageThread.  And HandleBlockMessageThread processes each block and
//  updates the UTXO if the block has been accepted and the tip updated. We cleanup and release the semaphore after
//  the thread has finished.
void CParallelValidation::HandleBlockMessage(CNode *pfrom, const string &strCommand, CBlockRef pblock, const CInv &inv)
{
    // Indicate that the block was received and is about to be processed. Setting the processing flag
    // prevents us from re-requesting the block during the time it is being processed.
    requester.ProcessingBlock(pblock->GetHash(), pfrom);

    // NOTE: You must not have a cs_main lock before you aquire the semaphore grant or you can end up deadlocking
    AssertLockNotHeld(cs_main);

    // Aquire semaphore grant
    if (IsChainNearlySyncd())
    {
        if (!semThreadCount.try_wait())
        {
            /** The following functionality is for the case when ALL thread queues and grants are in use, meaning
             * somehow an attacker has been able to craft blocks or sustain an attack in such a way as to use up
             * every available script queue thread.
             *  When/If that should occur, we must assume we are under a sustained attack and we will have to make a
             * determination as to which of the currently running threads we should terminate based on the
             * following critera:
             *     1) If all queues are in use and another block arrives, then terminate the running thread
             *        which has the largest block
             */

            {
                LOCK(cs_blockvalidationthread);
                if (mapBlockValidationThreads.size() >= nScriptCheckQueues)
                {
                    uint64_t nLargestBlockSize = 0;
                    bool fCompeting = false;
                    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator miLargestBlock =
                        mapBlockValidationThreads.end();
                    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter =
                        mapBlockValidationThreads.begin();
                    while (iter != mapBlockValidationThreads.end())
                    {
                        // Find largest block where the previous block hash matches. Meaning this is a new block and
                        // it's a competitor to to your block.
                        if ((*iter).second.hashPrevBlock == pblock->GetBlockHeader().hashPrevBlock)
                        {
                            fCompeting = true;
                            if ((*iter).second.nBlockSize > nLargestBlockSize)
                            {
                                nLargestBlockSize = (*iter).second.nBlockSize;
                                miLargestBlock = iter;
                            }
                        }
                        iter++;
                    }

                    // if the new competing block is the biggest or of equal size to the biggest then reject it.
                    if (fCompeting && (nLargestBlockSize <= pblock->GetBlockSize()))
                    {
                        LOG(PARALLEL,
                            "New Block validation terminated - Too many blocks currently being validated: %s\n",
                            pblock->GetHash().ToString());
                        return;
                    }
                    // Terminate the thread with the largest block.
                    else if (miLargestBlock != mapBlockValidationThreads.end())
                    {
                        Quit(miLargestBlock); // terminate the script queue thread
                        LOG(PARALLEL, "Too many blocks being validated, interrupting thread with blockhash %s "
                                      "and previous blockhash %s\n",
                            (*miLargestBlock).second.hash.ToString(),
                            (*miLargestBlock).second.hashPrevBlock.ToString());
                    }
                }
            } // We must not hold the lock here because we could be waiting for a grant, below.

            // wait for semaphore grant
            semThreadCount.wait();
        }
    }
    else
    {
        // for IBD just wait for the next available
        semThreadCount.wait();
    }

    // Add a reference here because we are detaching a thread which may run for a long time and
    // we do not want CNode to be deleted if the node should disconnect while we are processing this block.
    //
    // We do not have to take a vNodes lock here as would usually be the case because at this point there
    // will be at least one ref already and we therefore don't have to worry about getting disconnected.
    CNodeRef noderef(pfrom);

    // only launch block validation in a separate thread if PV is enabled.
    if (PV->Enabled() && !ShutdownRequested())
    {
        boost::thread thread(boost::bind(&HandleBlockMessageThread, noderef, strCommand, pblock, inv));
        thread.detach();
    }
    else
    {
        HandleBlockMessageThread(noderef, strCommand, pblock, inv);
    }
}

void HandleBlockMessageThread(CNodeRef noderef, const string strCommand, CBlockRef pblock, const CInv inv)
{
    boost::thread::id this_id(boost::this_thread::get_id());
    CNode *pfrom = noderef.get();

    try
    {
        uint64_t nSizeBlock = pblock->GetBlockSize();
        int64_t startTime = GetStopwatchMicros();
        CValidationState state;

        // Indicate that the block was fully received. At this point we have either a block or a fully reconstructed
        // thin type block but we still need to maintain a map*BlocksInFlight entry so that we don't re-request a
        // full block from the same node while the block is processing.
        thinrelay.BlockWasReceived(pfrom, inv.hash);

        PV->InitThread(this_id, pfrom, pblock, inv, nSizeBlock); // initialize the mapBlockValidationThread entries

        // Process all blocks from whitelisted peers, even if not requested,
        // unless we're still syncing with the network.
        // Such an unrequested block may still be processed, subject to the
        // conditions in AcceptBlock().
        bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
        const CChainParams &chainparams = Params();
        if (PV->Enabled())
        {
            ProcessNewBlock(state, chainparams, pfrom, pblock.get(), forceProcessing, nullptr, true);
        }
        else
        {
            // locking cs_main here prevents any other thread from beginning starting a block validation.
            LOCK(cs_main);
            ProcessNewBlock(state, chainparams, pfrom, pblock.get(), forceProcessing, nullptr, false);
        }

        if (!state.IsInvalid())
        {
            LargestBlockSeen(nSizeBlock); // update largest block seen

            double nValidationTime = (double)(GetStopwatchMicros() - startTime) / 1000000.0;
            if ((strCommand != NetMsgType::BLOCK) &&
                (IsThinBlocksEnabled() || IsGrapheneBlockEnabled() || IsCompactBlocksEnabled()))
            {
                LOG(THIN | GRAPHENE | CMPCT, "Processed Block %s reconstructed from (%s) in %.2f seconds, peer=%s\n",
                    inv.hash.ToString(), strCommand, (double)(GetStopwatchMicros() - startTime) / 1000000.0,
                    pfrom->GetLogName());

                if (strCommand == NetMsgType::GRAPHENEBLOCK || strCommand == NetMsgType::GRAPHENETX)
                    graphenedata.UpdateValidationTime(nValidationTime);
                else if (strCommand == NetMsgType::CMPCTBLOCK || strCommand == NetMsgType::BLOCKTXN)
                    compactdata.UpdateValidationTime(nValidationTime);
                else
                    thindata.UpdateValidationTime(nValidationTime);
            }
            else
            {
                LOG(THIN | GRAPHENE | CMPCT, "Processed Regular Block %s in %.2f seconds, peer=%s\n",
                    inv.hash.ToString(), (double)(GetStopwatchMicros() - startTime) / 1000000.0, pfrom->GetLogName());
            }
        }

        // When we request a thin type block we may get back a regular block if it is smaller than
        // either of the former.  Therefore we have to remove the thintype block in flight and any
        // associated data.
        thinrelay.ClearAllBlockData(pfrom, inv.hash);

        // Increment block counter
        pfrom->firstBlock += 1;

        // Erase any txns from the orphan cache, which were in this block, and that are now no longer needed.
        PV->ClearOrphanCache(pblock);

        // If chain is nearly caught up then flush the state after a block is finished processing and the
        // performance timings have been updated.  This way we don't include the flush time in our time to
        // process the block advance the tip.
        if (IsChainNearlySyncd())
            FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
    }
    catch (const std::exception &e)
    {
        LOGA("Exception thrown in PV thread: %s\n", e.what());
    }

    // Use a separate try catch block here.  In the event that the upper block throws an exception we'll still be
    // able to clean up the semaphore, tracking map and node reference.
    try
    {
        // release the semaphore.
        PV->Post();

        // Clear thread data - this must be done before the thread completes or else some other new
        // thread may grab the same thread id and we would end up deleting the entry for the new thread instead.
        //
        // Furthermore this step must also be done as the last step of this thread.
        // otherwise, shutdown could proceed before the validation thread has entirely completed.
        PV->Erase(this_id);
    }
    catch (const std::exception &e)
    {
        LOGA("Exception thrown in PV thread while cleaning up: %s\n", e.what());
    }
}


// for newly mined block validation, return the first queue not in use.
CCheckQueue<CScriptCheck> *CParallelValidation::GetScriptCheckQueue()
{
    while (true)
    {
        {
            LOCK(cs_blockvalidationthread);

            for (unsigned int i = 0; i < vQueues.size(); i++)
            {
                auto pqueue(vQueues[i]);

                if (pqueue->IsIdle())
                {
                    bool inUse = false;
                    map<boost::thread::id, CParallelValidation::CHandleBlockMsgThreads>::iterator iter =
                        mapBlockValidationThreads.begin();
                    while (iter != mapBlockValidationThreads.end())
                    {
                        if ((*iter).second.pScriptQueue == pqueue)
                        {
                            inUse = true;
                            break;
                        }
                        iter++;
                    }
                    if (!inUse)
                    {
                        pqueue->Quit(false); // set to false because it still may be set to true from last run.

                        // Only assign a pqueue to a validation thread if a validation thread is actually running.
                        // When mining or when bitcoin is first starting there will be no validation threads so we
                        // don't want to assign a pqueue here if that is the case.
                        boost::thread::id this_id(boost::this_thread::get_id());
                        if (mapBlockValidationThreads.count(this_id))
                            mapBlockValidationThreads[this_id].pScriptQueue = pqueue;

                        LOG(PARALLEL, "next scriptqueue not in use is %d\n", i);
                        return pqueue;
                    }
                }
            }
        }

        LOG(PARALLEL, "Sleeping 50 millis\n");
        MilliSleep(50);
    }
}
