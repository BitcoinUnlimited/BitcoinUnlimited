// Copyright (c) 2018 The Bitcoin Unlimited developers
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
#include "main.h" // for cs_main
#include "net.h"
#include "respend/respenddetector.h"
#include "timedata.h"
#include "txorphanpool.h"
#include "unlimited.h"
#include "util.h"
#include "utiltime.h"
#include "validationinterface.h"
#include <map>
#include <string>
#include <vector>

#include <boost/thread/thread.hpp>

using namespace std;


Snapshot txHandlerSnap;

void ThreadCommitToMempool();
void ThreadTxAdmission();
void ProcessOrphans(std::vector<uint256> &vWorkQueue);

bool CheckFinalTx(const CTransaction &tx, int flags, const Snapshot &ss);
bool CheckSequenceLocks(const CTransaction &tx,
    int flags,
    LockPoints *lp,
    bool useExistingLockPoints,
    const Snapshot &ss);


void StartTxAdmission(boost::thread_group &threadGroup)
{
    txHandlerSnap.Load(); // Get an initial view for the transaction processors

    // Start incoming transaction processing threads
    for (unsigned int i = 0; i < numTxAdmissionThreads.Value(); i++)
    {
        threadGroup.create_thread(boost::bind(&TraceThreads<void (*)()>, strprintf("tx%d", i), &ThreadTxAdmission));
    }

    // Start tx commitment thread
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "txcommit", &ThreadCommitToMempool));
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
            } while (!txCommitQ.empty());
        }

        { // block everything and check
            CORRAL(txProcessingCorral, CORRAL_TX_PAUSE);
            {
                LOCK(csTxInQ);
                empty = txInQ.empty() & txDeferQ.empty();
            }
            {
                boost::unique_lock<boost::mutex> lock(csCommitQ);
                empty &= txCommitQ.empty();
            }
        }
    }
}

// Put the tx on the tx admission queue for processing
void EnqueueTxForAdmission(CTxInputData &txd)
{
    LOCK(csTxInQ);
    bool conflict = false;
    for (auto inp : txd.tx->vin)
    {
        uint256 hash = inp.prevout.hash;
        unsigned char *first = hash.begin();
        *first ^= (unsigned char)(inp.prevout.n & 255);
        if (!incomingConflicts.checkAndSet(hash))
        {
            conflict = true;
            break;
        }
    }
    if (!conflict)
    {
        // LOG(MEMPOOL, "Enqueue for processing %x\n", txd.tx->GetHash().ToString());
        txInQ.push(txd); // add this transaction onto the processing queue
        cvTxInQ.notify_one();
    }
    else
    {
        // LOG(MEMPOOL, "Deferred %x\n", txd.tx->GetHash().ToString());
        txDeferQ.push(txd);
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
        if (mempool.exists(inv.hash))
            return 3;
        if (orphanpool.AlreadyHaveOrphan(inv.hash))
            return 4;
        return false;
    }
    }
    DbgAssert(0, return false); // this fn should only be called if CInv is a tx
}

void ThreadCommitToMempool()
{
    while (!ShutdownRequested())
    {
        {
            boost::unique_lock<boost::mutex> lock(csCommitQ);
            do
            {
                cvCommitQ.timed_wait(lock, boost::posix_time::milliseconds(2000));
            } while (txCommitQ.empty() && txDeferQ.empty());
        }

        {
            boost::this_thread::interruption_point();

            CORRAL(txProcessingCorral, CORRAL_TX_COMMITMENT);
            LOCK(cs_main);
            CommitTxToMempool();
            mempool.check(pcoinsTip);
            LOG(MEMPOOL, "MemoryPool sz %u txn, %u kB\n", mempool.size(), mempool.DynamicMemoryUsage() / 1000);
            // BU - Xtreme Thinblocks - trim the orphan pool by entry time and do not allow it to be overidden.
            LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

            CValidationState state;
            FlushStateToDisk(state, FLUSH_STATE_PERIODIC);
            // The flush to disk above is only periodic therefore we need to continuously trim any excess from the
            // cache.
            pcoinsTip->Trim(nCoinCacheMaxSize);

            // move the previously deferred txs into active processing
            std::queue<CTxInputData> wasDeferred;
            {
                LOCK(csTxInQ);
                // this could be a lot more efficient
                while (!txDeferQ.empty())
                {
                    wasDeferred.push(txDeferQ.front());
                    txDeferQ.pop();
                }
            }
            if (!wasDeferred.empty())
                LOG(MEMPOOL, "%d tx were deferred\n", wasDeferred.size());

            while (!wasDeferred.empty())
            {
                // LOG(MEMPOOL, "attempt enqueue deferred %s\n", wasDeferred.front().tx->GetHash().ToString());
                EnqueueTxForAdmission(wasDeferred.front());
                wasDeferred.pop();
            }
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
    std::vector<uint256> whatChanged;
    LOCK(cs_main); // cs_main must lock before csCommitQ
    {
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        for (auto &it : txCommitQ)
        {
            CTxCommitData &data = it.second;
            // Store transaction in memory
            mempool.addUnchecked(it.first, data.entry, !IsInitialBlockDownload());
#ifdef ENABLE_WALLET
            SyncWithWallets(data.entry.GetSharedTx(), nullptr, -1);
#endif
            whatChanged.push_back(data.hash);

            // Update txn per second only when a txn is valid and accepted to the mempool
            mempool.UpdateTransactionsPerSecond();
        }
        txCommitQ.clear();
    }

    {
        LOCK(csTxInQ);
        // Clear the filter of incoming conflicts, and put all queued tx on the deferred queue since they've been
        // deferred
        incomingConflicts.reset();
        while (!txInQ.empty())
        {
            txDeferQ.push(txInQ.front());
            txInQ.pop();
        }
        LOG(MEMPOOL, "Reset incoming filter\n");
    }
    ProcessOrphans(whatChanged);
}


void ThreadTxAdmission()
{
    while (!ShutdownRequested())
    {
        boost::this_thread::interruption_point();

        bool fMissingInputs = false;
        CValidationState state;
        // Snapshot ss;
        CTxInputData txd;

        {
            CCriticalBlock lock(csTxInQ, "csTxInQ", __FILE__, __LINE__);
            while (txInQ.empty() && !ShutdownRequested())
            {
                cvTxInQ.wait(csTxInQ);
                boost::this_thread::interruption_point();
            }
            if (ShutdownRequested())
                break;
        }

        {
            CORRAL(txProcessingCorral, CORRAL_TX_PROCESSING);

            { // tx must be popped within the TX_PROCESSING corral or the state break between processing
                // and commitment will not be clean
                CCriticalBlock lock(csTxInQ, "csTxInQ", __FILE__, __LINE__);
                if (txInQ.empty())
                    continue; // abort back into wait loop if another thread got my tx
                txd = txInQ.front(); // make copy so I can pop & release
                txInQ.pop();
            }

            CTransactionRef &tx = txd.tx;
            CInv inv(MSG_TX, tx->GetHash());

            std::vector<COutPoint> vCoinsToUncache;
            {
                // Check for recently rejected (and do other quick existence checks)
                bool have = TxAlreadyHave(inv);
                if (have)
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
                            LOGA("Not relaying invalid transaction %s from whitelisted peer=%d (%s)\n",
                                tx->GetHash().ToString(), txd.nodeName, FormatStateMessage(state));
                        }
                    }
                    continue;
                }
            }

            bool isRespend = false;
            if (ParallelAcceptToMemoryPool(txHandlerSnap, mempool, state, tx, true, &fMissingInputs, false, false,
                    TransactionClass::DEFAULT, vCoinsToUncache, &isRespend))
            {
                RelayTransaction(tx);

                // LOG(MEMPOOL, "AcceptToMemoryPool: peer=%s: accepted %s onto Q\n", txd.nodeName,
                //    tx->GetHash().ToString());
            }
            else
            {
                if (fMissingInputs)
                {
                    // If we've forked and this is probably not a valid tx, then skip adding it to the orphan pool
                    if (!chainActive.Tip()->IsforkActiveOnNextBlock(miningForkTime.Value()) ||
                        IsTxProbablyNewSigHash(*tx))
                    {
                        LOCK(orphanpool.cs); // WRITELOCK
                        orphanpool.AddOrphanTx(tx, txd.nodeId);

                        // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
                        static unsigned int nMaxOrphanTx =
                            (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
                        static uint64_t nMaxOrphanPoolSize = (uint64_t)std::max(
                            (int64_t)0, (GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000 / 10));
                        unsigned int nEvicted = orphanpool.LimitOrphanTxSize(nMaxOrphanTx, nMaxOrphanPoolSize);
                        if (nEvicted > 0)
                            LOG(MEMPOOL, "mapOrphan overflow, removed %u tx\n", nEvicted);
                    }
                    else
                    {
                        LOG(MEMPOOL, "rejected orphan as likely contains old sighash");
                    }
                }
                else // If the problem wasn't that the tx is an orphan, then uncache the inputs since we likely won't
                // need them again.
                {
                    for (const COutPoint &remove : vCoinsToUncache)
                        pcoinsTip->Uncache(remove);
                }
            }
            int nDoS = 0;
            if (state.IsInvalid(nDoS))
            {
                LOG(MEMPOOL, "%s from peer=%s was not accepted: %s\n", tx->GetHash().ToString(), txd.nodeName,
                    FormatStateMessage(state));
                if (state.GetRejectCode() < REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
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

    // After the May, 15 hard fork, we start accepting larger op_return.
    const CChainParams &chainparams = Params();
    const bool hasMay152018 = IsMay152018Enabled(chainparams.GetConsensus(), chainActive.Tip());

    // LOG(MEMPOOL, "Mempool: Considering Tx %s\n", tx->GetHash().ToString());

    if (!CheckTransaction(*tx, state))
        return false;

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
    if (fRequireStandard && !IsStandardTx(*tx, reason))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);

    // Don't relay version 2 transactions until CSV is active, and we can be
    // sure that such transactions will be mined (unless we're on
    // -testnet/-regtest).
    if (fRequireStandard && tx->nVersion >= 2 &&
        VersionBitsTipState(chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV) != THRESHOLD_ACTIVE)
    {
        return state.DoS(0, false, REJECT_NONSTANDARD, "premature-version2-tx");
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(*tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

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
                    if (!ss.coins->HaveCoinInCache(txin.prevout))
                    {
                        vCoinsToUncache.push_back(txin.prevout);
                    }

                    if (!view.HaveCoin(txin.prevout))
                    {
                        // fMissingInputs and not state.IsInvalid() is used to detect this condition, don't set
                        // state.Invalid()
                        *pfMissingInputs = true;
                        break; // There is no point checking any more once one fails, for orphans we will recheck
                    }
                }
                if (*pfMissingInputs == true)
                {
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
            if (!CheckSequenceLocks(*tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp, false, ss))
                return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(*tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        nSigOps = GetLegacySigOpCount(*tx);
        nSigOps += GetP2SHSigOpCount(*tx, view);

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

        CTxCommitData eData; // TODO awkward construction in these 4 lines
        CTxMemPoolEntry entryTemp(tx, nFees, GetTime(), dPriority, chainActive.Height(), pool.HasNoInputsOf(*tx),
            inChainInputValue, fSpendsCoinbase, nSigOps, lp);
        eData.entry = entryTemp;
        CTxMemPoolEntry &entry(eData.entry);
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
            // nFreeLimit = nFreeLimit + ((double)(DEFAULT_LIMIT_FREE_RELAY - nFreeLimit) / pow(1.0 - 1.0/86400,
            // (double)(nNow - nLastTime)));
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

                // Gradually choke off the nFreeLimit as well but leave at least DEFAULT_MIN_LIMITFREERELAY
                // So that some free transactions can still get through
                nFreeLimit = std::min(
                    nFreeLimit, ((double)nLimitFreeRelay - ((double)(nLimitFreeRelay - DEFAULT_MIN_LIMITFREERELAY) *
                                                               (double)(poolBytes - nLargestBlockSeen) /
                                                               (nLargestBlockSeen * (MAX_BLOCK_SIZE_MULTIPLIER - 1)))));
                if (nFreeLimit < DEFAULT_MIN_LIMITFREERELAY)
                    nFreeLimit = DEFAULT_MIN_LIMITFREERELAY;
            }
            else
            {
                nMinRelay = _dMaxLimiterTxFee;
                nFreeLimit = DEFAULT_MIN_LIMITFREERELAY;
            }

            minRelayTxFee = CFeeRate(nMinRelay * 1000);
            LOG(MEMPOOL, "MempoolBytes:%d  LimitFreeRelay:%.5g  nMinRelay:%.4g  FeesSatoshiPerByte:%.4g  TxBytes:%d  "
                         "TxFees:%d\n",
                poolBytes, nFreeLimit, nMinRelay, ((double)nFees) / nSize, nSize, nFees);
            if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize))
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

        eData.hash = hash;
        // Calculate in-mempool ancestors, up to a limit.
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
        std::string errString;

        // Set extraFlags as a set of flags that needs to be activated.
        uint32_t extraFlags = 0;
        if (hasMay152018)
        {
            extraFlags |= SCRIPT_ENABLE_MAY152018_OPCODES;
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        unsigned char sighashType = 0;
        if (!CheckInputs(*tx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS | extraFlags, true, &resourceTracker,
                nullptr, &sighashType))
        {
            LOG(MEMPOOL, "CheckInputs failed for tx: %s\n", tx->GetHash().ToString().c_str());
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
        if (!CheckInputs(*tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS | extraFlags, true, nullptr, nullptr,
                &sighashType2))
        {
            return error(
                "%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                __func__, hash.ToString(), FormatStateMessage(state));
        }

        entry.sighashType = sighashType | sighashType2;

        // This code denies old style tx from entering the mempool as soon as we fork
        if (IsUAHFforkActiveOnNextBlock(chainActive.Tip()->nHeight) && !IsTxUAHFOnly(entry))
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
                return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
            }
        }

        {
            boost::unique_lock<boost::mutex> lock(csCommitQ);
            txCommitQ[eData.hash] = eData;
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
    std::vector<uint256> vEraseQueue;

    // Recursively process any orphan transactions that depended on this one
    {
        LOCK(orphanpool.cs); // TODO READLOCK()
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
                DbgAssert(orphanpool.mapOrphanTransactions.count(orphanHash), fOk = false);
                if (!fOk)
                    continue;

                const CTransactionRef &orphanTx = orphanpool.mapOrphanTransactions[orphanHash].ptx;
                NodeId fromPeer = orphanpool.mapOrphanTransactions[orphanHash].fromPeer;
                // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                // anyone relaying LegitTxX banned)
                CValidationState stateDummy;

                if (setMisbehaving.count(fromPeer))
                    continue;
                {
                    CTxInputData txd;
                    txd.tx = orphanTx;
                    txd.nodeId = fromPeer;
                    txd.nodeName = "orphan";
                    txd.whitelisted = false;
                    LOG(MEMPOOL, "Resubmitting orphan tx: %s\n", orphanTx->GetHash().ToString().c_str());
                    EnqueueTxForAdmission(txd);
                }
                vEraseQueue.push_back(orphanHash);
            }
        }
    }

    {
        LOCK(orphanpool.cs); // TODO WRITELOCK
        for (auto hash : vEraseQueue)
            orphanpool.EraseOrphanTx(hash);
        //  BU: Xtreme thinblocks - purge orphans that are too old
        orphanpool.EraseOrphansByTime();
    }
}


void Snapshot::Load(void)
{
    LOCK(cs);
    tipHeight = chainActive.Height();
    tip = chainActive.Tip();
    tipMedianTimePast = tip->GetMedianTimePast();
    adjustedTime = GetAdjustedTime();
    coins = pcoinsTip; // TODO pcoinsTip can change
    if (cvMempool)
        delete cvMempool;

    READLOCK(mempool.cs);
    // ss.coins contains the UTXO set for the tip in ss
    cvMempool = new CCoinsViewMemPool(coins, mempool);
}

bool CheckSequenceLocks(const CTransaction &tx,
    int flags,
    LockPoints *lp,
    bool useExistingLockPoints,
    const Snapshot &ss)
{
    AssertLockHeld(mempool.cs);

    CBlockIndex *tip = ss.tip;
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
        CCoinsViewMemPool &viewMemPool(*ss.cvMempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++)
        {
            const CTxIn &txin = tx.vin[txinIndex];
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

bool CheckFinalTx(const CTransaction &tx, int flags, const Snapshot &ss)
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
    const int nBlockHeight = ss.tipHeight + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST) ? ss.tipMedianTimePast : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}
