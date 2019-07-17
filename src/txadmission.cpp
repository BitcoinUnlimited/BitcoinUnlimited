// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#include "chainparams.h"
#include "txadmission.h"
#include "blockstorage/blockstorage.h"
#include "connmgr.h"
#include "consensus/tx_verify.h"
#include "dosman.h"
#include "fastfilter.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "parallel.h"
#include "requestManager.h"
#include "respend/respenddetector.h"
#include "timedata.h"
#include "txorphanpool.h"
#include "unlimited.h"
#include "util.h"
#include "utiltime.h"
#include "validation/validation.h"
#include "validationinterface.h"
#include <map>
#include <string>
#include <vector>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/thread/thread.hpp>

using namespace std;


Snapshot txHandlerSnap;

void ThreadCommitToMempool();
void ThreadTxAdmission();
void ProcessOrphans(std::vector<uint256> &vWorkQueue);

CTransactionRef CommitQGet(uint256 hash)
{
    boost::unique_lock<boost::mutex> lock(csCommitQ);
    std::map<uint256, CTxCommitData>::iterator it = txCommitQ->find(hash);
    if (it == txCommitQ->end())
        return nullptr;
    return it->second.entry.GetSharedTx();
}

static inline uint256 IncomingConflictHash(const COutPoint &prevout)
{
    uint256 hash = prevout.hash;
    uint32_t *first = (uint32_t *)hash.begin();
    *first ^= (uint32_t)(prevout.n & 65535);
    first += 2;
    *first ^= (uint32_t)(prevout.n & 65535);
    first += 2;
    *first ^= (uint32_t)(prevout.n & 65535);
    first += 2;
    *first ^= (uint32_t)(prevout.n & 65535);

    return hash;
}

void StartTxAdmission(thread_group &threadGroup)
{
    if (txCommitQ == nullptr)
        txCommitQ = new std::map<uint256, CTxCommitData>();

    txHandlerSnap.Load(); // Get an initial view for the transaction processors

    // Start incoming transaction processing threads
    for (unsigned int i = 0; i < numTxAdmissionThreads.Value(); i++)
    {
        threadGroup.create_thread(&ThreadTxAdmission);
    }

    // Start tx commitment thread
    threadGroup.create_thread(&ThreadCommitToMempool);
}

void StopTxAdmission()
{
    cvTxInQ.notify_all();
    cvCommitQ.notify_all();
}

void FlushTxAdmission()
{
    bool empty = false;

    while (!empty)
    {
        do // give the tx processing threads a chance to run
        {
            {
                LOCK(csTxInQ);
                empty = txInQ.empty() & txDeferQ.empty();
            }
            if (!empty)
                MilliSleep(100);
        } while (!empty);

        {
            boost::unique_lock<boost::mutex> lock(csCommitQ);
            do // wait for the commit thread to commit everything
            {
                cvCommitQ.timed_wait(lock, boost::posix_time::milliseconds(100));
            } while (!txCommitQ->empty());
        }

        { // block everything and check
            CORRAL(txProcessingCorral, CORRAL_TX_PAUSE);
            {
                LOCK(csTxInQ);
                empty = txInQ.empty() & txDeferQ.empty();
            }
            {
                boost::unique_lock<boost::mutex> lock(csCommitQ);
                empty &= txCommitQ->empty();
            }
        }
    }
}

// Put the tx on the tx admission queue for processing
void EnqueueTxForAdmission(CTxInputData &txd)
{
    LOCK(csTxInQ);
    bool conflict = false;
    for (auto &inp : txd.tx->vin)
    {
        uint256 hash = IncomingConflictHash(inp.prevout);
        if (!incomingConflicts.checkAndSet(hash))
        {
            conflict = true;
            break;
        }
    }

    // If there is no conflict then the transaction is ready for validation and can be placed in the processing
    // queue. However, if there is a conflict then this could be a double spend, so defer the transaction until the
    // transaction it conflicts with has been fully processed.
    if (!conflict)
    {
        // LOG(MEMPOOL, "Enqueue for processing %x\n", txd.tx->GetHash().ToString());
        txInQ.push(txd); // add this transaction onto the processing queue.
        cvTxInQ.notify_one();
    }
    else
    {
        LOG(MEMPOOL, "Fastfilter collision, deferred %x\n", txd.tx->GetHash().ToString());
        txDeferQ.push(txd);

        // By notifying the commitQ, the deferred queue can be processed right way which helps
        // to forward double spends as quickly as possible.
        cvCommitQ.notify_one();
    }
}


unsigned int TxAlreadyHave(const CInv &inv)
{
    switch (inv.type)
    {
    case MSG_TX:
    {
        if (txRecentlyInBlock.contains(inv.hash))
            return 1;
        if (recentRejects.contains(inv.hash))
            return 2;
        {
            boost::unique_lock<boost::mutex> lock(csCommitQ);
            const auto &elem = txCommitQ->find(inv.hash);
            if (elem != txCommitQ->end())
            {
                return 5;
            }
        }
        if (mempool.exists(inv.hash))
            return 3;
        if (orphanpool.AlreadyHaveOrphan(inv.hash))
            return 4;
        return 0;
    }
    }
    DbgAssert(0, return false); // this fn should only be called if CInv is a tx
}

void ThreadCommitToMempool()
{
    while (shutdown_threads.load() == false)
    {
        {
            boost::unique_lock<boost::mutex> lock(csCommitQ);
            do
            {
                cvCommitQ.timed_wait(lock, boost::posix_time::milliseconds(2000));
                if (shutdown_threads.load() == true)
                {
                    return;
                }
            } while (txCommitQ->empty() && txDeferQ.empty());
        }

        {
            if (shutdown_threads.load() == true)
            {
                return;
            }

            CORRAL(txProcessingCorral, CORRAL_TX_COMMITMENT);
            {
                CommitTxToMempool();
                LOG(MEMPOOL, "MemoryPool sz %u txn, %u kB\n", mempool.size(), mempool.DynamicMemoryUsage() / 1000);
                LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                    GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

                CValidationState state;
                FlushStateToDisk(state, FLUSH_STATE_PERIODIC);

                // The flush to disk above is only periodic therefore we need to check if we need to trim
                // any excess from the cache.
                if (pcoinsTip->DynamicMemoryUsage() > (size_t)nCoinCacheMaxSize)
                    pcoinsTip->Trim(nCoinCacheMaxSize * .95);
            }

            mempool.check(pcoinsTip);
        }
    }
}

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

void CommitTxToMempool()
{
    std::vector<uint256> vWhatChanged;
    std::map<uint256, CTxCommitData> *q;
    {
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        LOG(MEMPOOL, "txadmission committing %d tx\n", txCommitQ->size());
        q = txCommitQ;
        txCommitQ = new std::map<uint256, CTxCommitData>();
    }

    // These transactions have already been validated so store them directly into the mempool.
    for (auto &it : *q)
    {
        CTxCommitData &data = it.second;
        mempool.addUnchecked(it.first, data.entry, !IsInitialBlockDownload());
        vWhatChanged.push_back(data.hash);
        // Update txn per second only when a txn is valid and accepted to the mempool
        mempool.UpdateTransactionsPerSecond();

        // Indicate that this tx was fully processed/accepted and can now be removed from the
        // request manager.
        CInv inv(MSG_TX, data.hash);
        requester.Received(inv, nullptr);
    }

#ifdef ENABLE_WALLET
    for (auto &it : *q)
    {
        CTxCommitData &data = it.second;
        SyncWithWallets(data.entry.GetSharedTx(), nullptr, -1);
    }
#endif
    q->clear();
    delete q;


    std::map<uint256, CTxInputData> mapWasDeferred;
    {
        LOCK(csTxInQ);
        // Clear the filter of incoming conflicts, and put all queued tx on the deferred queue since they've been
        // deferred
        LOG(MEMPOOL, "txadmission incoming filter reset.  Current txInQ size: %d\n", txInQ.size());
        incomingConflicts.reset();
        while (!txInQ.empty())
        {
            txDeferQ.push(txInQ.front());
            txInQ.pop();
        }
        // If the chain is now syncd and there are txns in the wait queue then add these also to the deferred queue.
        // The wait queue is not very active and it will typically have just 1 or 2 txns in it, if any at all.
        while (IsChainSyncd() && !txWaitNextBlockQ.empty())
        {
            txDeferQ.push(txWaitNextBlockQ.front());
            txWaitNextBlockQ.pop();
        }

        // Move the previously deferred txns into active processing.

        // We MUST push the first item in the defer queue to the input queue without checking it against incoming
        // conflicts.  This is fine because the first insert into an empty incomingConflicts must succeed.
        // A transaction's inputs could cause a false positive match against each other.  By pushing the first
        // deferred tx without checking, we can still use the efficient fastfilter checkAndSet function for most queue
        // filter checking but mop up the extremely rare tx whose inputs have false positive matches here.
        if (!txDeferQ.empty())
        {
            const CTxInputData &first = txDeferQ.front();

            for (const auto &inp : first.tx->vin)
            {
                uint256 hash = IncomingConflictHash(inp.prevout);
                incomingConflicts.insert(hash);
            }
            txInQ.push(first);
            cvTxInQ.notify_one();
            txDeferQ.pop();
        }

        // Use a map to store the txns so that we end up removing duplicates which could have arrived
        // from re-requests.
        LOG(MEMPOOL, "popping txdeferQ, size %d\n", txDeferQ.size());
        // this could be a lot more efficient
        while (!txDeferQ.empty())
        {
            const uint256 &hash = txDeferQ.front().tx->GetHash();
            mapWasDeferred.emplace(hash, txDeferQ.front());

            txDeferQ.pop();
        }
    }

    if (!mapWasDeferred.empty())
        LOG(MEMPOOL, "%d tx were deferred\n", mapWasDeferred.size());

    for (auto &it : mapWasDeferred)
    {
        LOG(MEMPOOL, "attempt enqueue deferred %s\n", it.first.ToString());
        EnqueueTxForAdmission(it.second);
    }
    ProcessOrphans(vWhatChanged);
}


void ThreadTxAdmission()
{
    // Process at most this many transactions before letting the commit thread take over
    const int maxTxPerRound = 200;

    while (shutdown_threads.load() == false)
    {
        bool acceptedSomething = false;
        if (shutdown_threads.load() == true)
        {
            return;
        }

        bool fMissingInputs = false;
        CValidationState state;
        // Snapshot ss;
        CTxInputData txd;

        {
            CCriticalBlock lock(csTxInQ, "csTxInQ", __FILE__, __LINE__);
            while (txInQ.empty() && shutdown_threads.load() == false)
            {
                if (shutdown_threads.load() == true)
                {
                    return;
                }
                cvTxInQ.wait(csTxInQ);
            }
            if (shutdown_threads.load() == true)
            {
                return;
            }
        }

        {
            CORRAL(txProcessingCorral, CORRAL_TX_PROCESSING);

            for (unsigned int txPerRoundCount = 0; txPerRoundCount < maxTxPerRound; txPerRoundCount++)
            {
                // tx must be popped within the TX_PROCESSING corral or the state break between processing
                // and commitment will not be clean
                {
                    CCriticalBlock lock(csTxInQ, "csTxInQ", __FILE__, __LINE__);
                    if (txInQ.empty())
                    {
                        // speed up tx chunk processing when there is nothing else to do
                        if (acceptedSomething)
                            cvCommitQ.notify_all();
                        break;
                    }

                    // Make a copy so we can pop and release
                    txd = txInQ.front();
                    txInQ.pop();
                }

                CTransactionRef &tx = txd.tx;
                CInv inv(MSG_TX, tx->GetHash());

                if (!TxAlreadyHave(inv))
                {
                    std::vector<COutPoint> vCoinsToUncache;
                    bool isRespend = false;
                    if (ParallelAcceptToMemoryPool(txHandlerSnap, mempool, state, tx, true, &fMissingInputs, false,
                            false, TransactionClass::DEFAULT, vCoinsToUncache, &isRespend))
                    {
                        acceptedSomething = true;
                        RelayTransaction(tx);

                        // LOG(MEMPOOL, "Accepted tx: peer=%s: accepted %s onto Q\n", txd.nodeName,
                        //     tx->GetHash().ToString());
                    }
                    else if (state.GetRejectCode() == REJECT_WAITING)
                    {
                        // If the chain is not sync'd entirely then we'll defer this tx until
                        // the new block is processed.
                        LOCK(csTxInQ);
                        if (txWaitNextBlockQ.size() <= (10 * excessiveBlockSize / 1000000))
                        {
                            txWaitNextBlockQ.push(txd);
                            LOG(MEMPOOL, "Tx %s is waiting on next block, reason:%s\n", tx->GetHash().ToString(),
                                state.GetRejectReason());
                        }
                        else
                            LOG(MEMPOOL, "WaitNexBlockQueue is full - tx:%s reason:%s\n", tx->GetHash().ToString(),
                                state.GetRejectReason());
                    }
                    else
                    {
                        LOG(MEMPOOL, "Rejected tx: %s(%d) %s: %s. peer %s  hash %s \n", state.GetRejectReason(),
                            state.GetRejectCode(), fMissingInputs ? "orphan" : "", state.GetDebugMessage(),
                            txd.nodeName, tx->GetHash().ToString());

                        if (fMissingInputs)
                        {
                            WRITELOCK(orphanpool.cs);
                            orphanpool.AddOrphanTx(tx, txd.nodeId);

                            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
                            static const unsigned int nMaxOrphanTx = (unsigned int)std::max(
                                (int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
                            static const uint64_t nMaxOrphanPoolSize = (uint64_t)std::max(
                                (int64_t)0, (GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000 / 10));
                            unsigned int nEvicted = orphanpool.LimitOrphanTxSize(nMaxOrphanTx, nMaxOrphanPoolSize);
                            if (nEvicted > 0)
                                LOG(MEMPOOL, "mapOrphan overflow, removed %u tx\n", nEvicted);
                        }
                        else
                        {
                            recentRejects.insert(tx->GetHash());

                            if (txd.whitelisted && GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY))
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
                                    LOGA("Force relaying tx %s from whitelisted peer=%s\n", tx->GetHash().ToString(),
                                        txd.nodeName);
                                    RelayTransaction(tx);
                                }
                                else
                                {
                                    LOGA("Not relaying invalid transaction %s from whitelisted peer=%s (%s)\n",
                                        tx->GetHash().ToString(), txd.nodeName, FormatStateMessage(state));
                                }
                            }
                            // If the problem wasn't that the tx is an orphan, then uncache the inputs since we likely
                            // won't
                            // need them again.
                            for (const COutPoint &remove : vCoinsToUncache)
                                pcoinsTip->Uncache(remove);
                        }

                        // Mark tx as received if invalid or an orphan. If it's a valid Tx we mark it received
                        // only when it's finally accepted into the mempool.
                        requester.Received(inv, nullptr);
                    }

                    int nDoS = 0;
                    if (state.IsInvalid(nDoS) && state.GetRejectCode() != REJECT_WAITING)
                    {
                        LOG(MEMPOOL, "%s from peer=%s was not accepted: %s\n", tx->GetHash().ToString(), txd.nodeName,
                            FormatStateMessage(state));
                        if (state.GetRejectCode() <
                            REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
                        {
                            CNodeRef from = connmgr->FindNodeFromId(txd.nodeId);
                            if (from)
                            {
                                std::string strCommand = NetMsgType::TX;
                                from->PushMessage(NetMsgType::REJECT, strCommand, (unsigned char)state.GetRejectCode(),
                                    state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
                                if (nDoS > 0)
                                {
                                    dosMan.Misbehaving(from.get(), nDoS);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}


bool AcceptToMemoryPool(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &tx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx)
{
    std::vector<COutPoint> vCoinsToUncache;

    bool res = false;

    CORRAL(txProcessingCorral, CORRAL_TX_PAUSE);
    CommitTxToMempool();

    bool isRespend = false;
    bool missingInputs = false;
    res = ParallelAcceptToMemoryPool(txHandlerSnap, pool, state, tx, fLimitFree, &missingInputs, fOverrideMempoolLimit,
        fRejectAbsurdFee, allowedTx, vCoinsToUncache, &isRespend);
    if (res)
    {
        RelayTransaction(tx);
    }

    // Uncache any coins for txns that failed to enter the mempool but were NOT orphan txns
    if (isRespend || (!res && !missingInputs))
    {
        for (const COutPoint &remove : vCoinsToUncache)
            pcoinsTip->Uncache(remove);
    }

    if (pfMissingInputs)
        *pfMissingInputs = missingInputs;

    if (res)
    {
        CommitTxToMempool();
        LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
            GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
    }
    return res;
}

bool ParallelAcceptToMemoryPool(Snapshot &ss,
    CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &tx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx,
    std::vector<COutPoint> &vCoinsToUncache,
    bool *isRespend)
{
    if (isRespend)
        *isRespend = false;
    unsigned int nSigOps = 0;
    ValidationResourceTracker resourceTracker;
    unsigned int nSize = 0;
    uint64_t start = GetStopwatch();
    if (pfMissingInputs)
        *pfMissingInputs = false;

    const CChainParams &chainparams = Params();

    if (!CheckTransaction(tx, state))
    {
        if (state.GetDebugMessage() == "")
            state.SetDebugMessage("CheckTransaction failed");
        return false;
    }

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx->IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    // Reject nonstandard transactions if so configured.
    // (-testnet/-regtest allow nonstandard, and explicit submission via RPC)
    std::string reason;
    bool fRequireStandard = chainparams.RequireStandard();

    if (allowedTx == TransactionClass::STANDARD)
        fRequireStandard = true;
    else if (allowedTx == TransactionClass::NONSTANDARD)
        fRequireStandard = false;
    if (fRequireStandard && !IsStandardTx(tx, reason))
    {
        state.SetDebugMessage("IsStandardTx failed");
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);
    }

    const uint32_t cds_flag =
        (IsNov2018Activated(chainparams.GetConsensus(), chainActive.Tip())) ? SCRIPT_VERIFY_CHECKDATASIG_SIGOPS : 0;
    const uint32_t schnorrflag =
        (IsMay2019Activated(chainparams.GetConsensus(), chainActive.Tip())) ? SCRIPT_ENABLE_SCHNORR : 0;
    const uint32_t segwit_flag =
        (IsMay2019Activated(chainparams.GetConsensus(), chainActive.Tip()) && !fRequireStandard) ?
            SCRIPT_ALLOW_SEGWIT_RECOVERY :
            0;

    const uint32_t featureFlags = cds_flag | schnorrflag | segwit_flag;
    const uint32_t flags = STANDARD_SCRIPT_VERIFY_FLAGS | featureFlags;

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &ss))
    {
        if (!IsChainSyncd() && IsChainNearlySyncd())
            return state.DoS(0, false, REJECT_WAITING, "non-final");
        else
            return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");
    }

    // Make sure tx size is acceptable after Nov 15, 2018 fork
    if (IsNov2018Activated(chainparams.GetConsensus(), chainActive.Tip()))
    {
        if (tx->GetTxSize() < MIN_TX_SIZE)
            return state.DoS(0, false, REJECT_INVALID, "txn-undersize");
    }

    // is it already in the memory pool?
    uint256 hash = tx->GetHash();
    if (pool.exists(hash))
        return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool");

    // Check for conflicts with in-memory transactions and triggers actions at
    // end of scope (relay tx, sync wallet, etc)
    respend::RespendDetector respend(pool, tx);
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
            CCoinsViewMemPool &viewMemPool(*ss.cvMempool);
            view.SetBackend(viewMemPool);

            // do all inputs exist?
            if (pfMissingInputs)
            {
                *pfMissingInputs = false;
                for (const CTxIn &txin : tx->vin)
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
                    bool fSpent = false;
                    bool fMissingOrSpent = false;
                    if (!ss.coins->HaveCoinInCache(txin.prevout, fSpent))
                    {
                        vCoinsToUncache.push_back(txin.prevout);
                        if (!view.GetCoinFromDB(txin.prevout))
                        {
                            fMissingOrSpent = true;
                        }
                    }
                    if (fSpent || fMissingOrSpent)
                    {
                        *pfMissingInputs = true;
                        break; // There is no point checking any more once one fails, for orphans we will recheck
                    }
                }
                if (*pfMissingInputs == true)
                {
                    state.SetDebugMessage("Inputs are missing");
                    return false; // state.Invalid(false, REJECT_MISSING_INPUTS, "bad-txns-missing-inputs", "Inputs
                    // unavailable in ParallelAcceptToMemoryPool", false);
                }
            }

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(*tx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);

            // Only accept BIP68 sequence locked transactions that can be mined in the next
            // block; we don't want our mempool filled up with transactions that can't
            // be mined yet.
            // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
            // CoinsViewCache instead of create its own
            if (!CheckSequenceLocks(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp, false, &ss))
                return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        nSigOps = GetLegacySigOpCount(tx, STANDARD_SCRIPT_VERIFY_FLAGS);
        nSigOps += GetP2SHSigOpCount(tx, view, STANDARD_SCRIPT_VERIFY_FLAGS);

        CAmount nValueOut = tx->GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        double nPriorityDummy = 0;
        pool.ApplyDeltas(hash, nPriorityDummy, nModifiedFees);

        CAmount inChainInputValue;
        double dPriority = view.GetPriority(*tx, chainActive.Height(), inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        for (const CTxIn &txin : tx->vin)
        {
            CoinAccessor coin(view, txin.prevout);
            if (coin->IsCoinBase())
            {
                fSpendsCoinbase = true;
                break;
            }
        }

        // Create a commit data entry
        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainActive.Height(), pool.HasNoInputsOf(*tx),
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
            LOG(MEMPOOL, "Txn fee %lld (%d - %d), priority fee delta was %lld\n", nFees, nValueIn, nValueOut,
                nModifiedFees - nFees);
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
        }

        // BU - Xtreme Thinblocks Auto Mempool Limiter - begin section
        /* Continuously rate-limit free (really, very-low-fee) transactions
         * This mitigates 'penny-flooding' -- sending thousands of free transactions just to
         * be annoying or make others' transactions take longer to confirm. */
        // maximum nMinRelay in satoshi per byte
        static const int nLimitFreeRelay = GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY);
        // In case nLimitFreeRelay is defined less than the DEFAULT_MIN_LIMITFREERELAY we have to use the lower value
        static const int nMinLimitFreeRelay = std::min((int)DEFAULT_MIN_LIMITFREERELAY, nLimitFreeRelay);


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

                // Gradually choke off the nFreeLimit as well but leave at least nMinLimitFreeRelay
                // So that some free transactions can still get through
                nFreeLimit = std::min(
                    nFreeLimit, ((double)nLimitFreeRelay - ((double)(nLimitFreeRelay - nMinLimitFreeRelay) *
                                                               (double)(poolBytes - nLargestBlockSeen) /
                                                               (nLargestBlockSeen * (MAX_BLOCK_SIZE_MULTIPLIER - 1)))));
                if (nFreeLimit < nMinLimitFreeRelay)
                    nFreeLimit = nMinLimitFreeRelay;
            }
            else
            {
                nMinRelay = _dMaxLimiterTxFee;
                nFreeLimit = nMinLimitFreeRelay;
            }

            minRelayTxFee = CFeeRate(nMinRelay * 1000);
            LOG(MEMPOOL, "MempoolBytes:%d  LimitFreeRelay:%.5g  nMinRelay:%.4g  FeesSatoshiPerByte:%.4g  TxBytes:%d  "
                         "TxFees:%d\n",
                poolBytes, nFreeLimit, nMinRelay, ((double)nFees) / nSize, nSize, nFees);
            if ((fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize)) ||
                (nLimitFreeRelay == 0 && nFees < ::minRelayTxFee.GetFee(nSize)))
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
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
        std::string errString;

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        unsigned char sighashType = 0;
        if (!CheckInputs(
                tx, state, view, true, flags, maxScriptOps.Value(), true, &resourceTracker, nullptr, &sighashType))
        {
            LOG(MEMPOOL, "CheckInputs failed for tx: %s\n", hash.ToString());
            if (state.GetDebugMessage() == "")
                state.SetDebugMessage("CheckInputs failed");
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
        if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS | featureFlags, maxScriptOps.Value(),
                true, nullptr, nullptr, &sighashType2))
        {
            if (state.GetDebugMessage() == "")
                state.SetDebugMessage("CheckInputs failed against mandatory but not standard flags");

            return error(
                "%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                __func__, hash.ToString(), FormatStateMessage(state));
        }

        entry.sighashType = sighashType | sighashType2;

        // This code denies old style tx from entering the mempool as soon as we fork
        if (!IsTxUAHFOnly(entry))
        {
            return state.Invalid(false, REJECT_WRONG_FORK, "txn-uses-old-sighash-algorithm");
        }

        respend.SetValid(true);
        if (respend.IsRespend())
        {
            return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");
        }

        {
            READLOCK(pool.cs);
            CTxMemPool::setEntries setAncestors;
            // note we could resolve ancestors to hashes and return those if that saves time in the txc thread
            if (!pool._CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize,
                    nLimitDescendants, nLimitDescendantSize, errString))
            {
                // If the chain is not sync'd entirely then we'll defer this tx until the new block is processed.
                if (!IsChainSyncd() && IsChainNearlySyncd())
                    return state.DoS(0, false, REJECT_WAITING, "too-long-mempool-chain");
                else
                    return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
            }
        }

        // Add entry to the commit queue
        {
            CTxCommitData eData;
            eData.entry = std::move(entry);
            eData.hash = hash;

            boost::unique_lock<boost::mutex> lock(csCommitQ);
            (*txCommitQ).emplace(eData.hash, eData);
        }
    }
    uint64_t interval = (GetStopwatch() - start) / 1000;
    // typically too much logging, but useful when optimizing tx validation
    LOG(BENCH, "ValidateTransaction, time: %d, tx: %s, len: %d, sigops: %llu (legacy: %u), sighash: %llu, Vin: "
               "%llu, Vout: %llu\n",
        interval, tx->GetHash().ToString(), nSize, resourceTracker.GetSigOps(), (unsigned int)nSigOps,
        resourceTracker.GetSighashBytes(), tx->vin.size(), tx->vout.size());
    nTxValidationTime << interval;

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


void ProcessOrphans(std::vector<uint256> &vWorkQueue)
{
    // Recursively process any orphan transactions that depended on this one.
    // NOTE: you must not return early since EraseOrphansByTime() must always be checked
    std::map<uint256, CTxInputData> mapEnqueue;
    {
        READLOCK(orphanpool.cs);
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
                std::map<uint256, CTxOrphanPool::COrphanTx>::iterator iter =
                    orphanpool.mapOrphanTransactions.find(orphanHash);
                DbgAssert(iter != orphanpool.mapOrphanTransactions.end(), fOk = false);
                if (!fOk)
                    continue;

                // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                // anyone relaying LegitTxX banned)
                CValidationState stateDummy;

                if (setMisbehaving.count(iter->second.fromPeer))
                    continue;
                {
                    CTxInputData txd;
                    txd.tx = iter->second.ptx;
                    txd.nodeId = iter->second.fromPeer;
                    txd.nodeName = "orphan";
                    LOG(MEMPOOL, "Resubmitting orphan tx: %s\n", orphanHash.ToString());
                    mapEnqueue.emplace(std::move(orphanHash), std::move(txd));
                }
            }
        }
    }

    // First delete the orphans before enqueuing them otherwise we may end up putting them
    // in the queue twice.
    {
        WRITELOCK(orphanpool.cs);
        for (auto it = mapEnqueue.begin(); it != mapEnqueue.end(); it++)
        {
            // If the orphan was not erased then it must already have been erased/enqueued by another thread
            // so do not enqueue this orphan again.
            if (!orphanpool.EraseOrphanTx(it->first))
                it = mapEnqueue.erase(it);
        }
        orphanpool.EraseOrphansByTime();
    }
    for (auto &it : mapEnqueue)
        EnqueueTxForAdmission(it.second);
}


void Snapshot::Load(void)
{
    LOCK(cs);
    tipHeight = chainActive.Height();
    tip = chainActive.Tip();
    if (tip)
    {
        tipMedianTimePast = tip->GetMedianTimePast();
    }
    else
    {
        tipMedianTimePast = 0; // MTP does not matter, we are in IBD
    }
    adjustedTime = GetAdjustedTime();
    coins = pcoinsTip; // TODO pcoinsTip can change
    if (cvMempool)
        delete cvMempool;

    READLOCK(mempool.cs);
    // ss.coins contains the UTXO set for the tip in ss
    cvMempool = new CCoinsViewMemPool(coins, mempool);
}

bool CheckSequenceLocks(const CTransactionRef &tx,
    int flags,
    LockPoints *lp,
    bool useExistingLockPoints,
    const Snapshot *ss)
{
    if (ss == nullptr)
        AssertLockHeld(cs_main);
    AssertLockHeld(mempool.cs);

    CBlockIndex *tip = (ss != nullptr) ? ss->tip : chainActive.Tip();
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
        CCoinsViewMemPool tmpView(pcoinsTip, mempool);
        CCoinsViewMemPool &viewMemPool = (ss != nullptr) ? *ss->cvMempool : tmpView;
        std::vector<int> prevheights;
        prevheights.resize(tx->vin.size());
        for (size_t txinIndex = 0; txinIndex < tx->vin.size(); txinIndex++)
        {
            const CTxIn &txin = tx->vin[txinIndex];
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

bool CheckFinalTx(const CTransactionRef &tx, int flags, const Snapshot *ss)
{
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
    const int nBlockHeight = (ss != nullptr) ? ss->tipHeight + 1 : chainActive.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nMedianTimePast = (ss != nullptr) ? ss->tipMedianTimePast : chainActive.Tip()->GetMedianTimePast();
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}
