// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validatetx.h"

#include "blockrelay/blockrelay_common.h"
#include "blockstorage/blockstorage.h"
#include "blockstorage/sequential_files.h"
#include "checkpoints.h"
#include "connmgr.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "dosman.h"
#include "expedited.h"
#include "init.h"
#include "requestManager.h"
#include "sync.h"
#include "timedata.h"
#include "txadmission.h"
#include "txorphanpool.h"
#include "ui_interface.h"
#include "validationinterface.h"

#include "txmempool.h"

#include "validation.h"

#include <boost/scope_exit.hpp>

UniValue CheckInputsBetter(const CTransactionRef &ptx,
    CValidationState &state,
    const CCoinsViewCache &inputs,
    bool fScriptChecks,
    unsigned int flags,
    unsigned int maxOps,
    bool cacheStore,
    ValidationResourceTracker *resourceTracker,
    std::vector<CScriptCheck> *pvChecks,
    unsigned char *sighashType)
{
    UniValue inputsCheckResult(UniValue::VOBJ);
    if (ptx->IsCoinBase())
    {
        return inputsCheckResult;
    }

    bool allPassed = true;

    if (!Consensus::CheckTxInputs(ptx, state, inputs))
    {
        inputsCheckResult.pushKV("valid", false);
        return inputsCheckResult;
    }

    if (pvChecks)
    {
        pvChecks->reserve(ptx->vin.size());
    }

    UniValue inputsCheckList(UniValue::VARR);

    for (unsigned int i = 0; i < ptx->vin.size(); i++)
    {
        const COutPoint &prevout = ptx->vin[i].prevout;
        const CScript &scriptSig = ptx->vin[i].scriptSig;
        CoinAccessor coin(inputs, prevout);

        if (coin->IsSpent())
        {
            allPassed = false;
            inputsCheckResult.pushKV("valid", false);
            return inputsCheckResult;
        }
        const CScript scriptPubKey = coin->out.scriptPubKey;
        const CAmount amount = coin->out.nValue;
        UniValue inputResult(UniValue::VOBJ);

        inputResult.pushKV("prevtx", prevout.hash.ToString());
        inputResult.pushKV("n", std::to_string(prevout.n));
        inputResult.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));
        inputResult.pushKV("scriptSig", HexStr(scriptSig.begin(), scriptSig.end()));
        inputResult.pushKV("amount", amount);
        bool inputVerified = true;


        // Verify signature
        CScriptCheck check(resourceTracker, scriptPubKey, amount, *ptx, i, flags, maxOps, cacheStore);
        if (pvChecks)
        {
            pvChecks->push_back(CScriptCheck());
            check.swap(pvChecks->back());
        }
        else if (!check())
        {
            // Compute flags without the optional standardness flags.
            // This differs from MANDATORY_SCRIPT_VERIFY_FLAGS as it contains
            // additional upgrade flags (see ParallelAcceptToMemoryPool variable
            // featureFlags).
            // Even though it is not a mandatory flag,SCRIPT_ALLOW_SEGWIT_RECOVERY
            // is strictly more permissive than the set of standard flags.
            // It therefore needs to be added in order to check if we need to penalize
            // the peer that sent us the transaction or not.
            uint32_t mandatoryFlags = (flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS) | SCRIPT_ALLOW_SEGWIT_RECOVERY;
            if (flags != mandatoryFlags)
            {
                // Check whether the failure was caused by a
                // non-mandatory script verification check, such as
                // non-standard DER encodings or non-null dummy
                // arguments; if so, don't trigger DoS protection to
                // avoid splitting the network between upgraded and
                // non-upgraded nodes.
                CScriptCheck check2(nullptr, scriptPubKey, amount, *ptx, i, mandatoryFlags, maxOps, cacheStore);
                if (check2())
                {
                    inputResult.pushKV("error",
                        strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    inputVerified = false;
                    allPassed = false;
                }
            }

            // We also, regardless, need to check whether the transaction would
            // be valid on the other side of the upgrade, so as to avoid
            // splitting the network between upgraded and non-upgraded nodes.
            // Note that this will create strange error messages like
            // "upgrade-conditional-script-failure (Non-canonical DER ...)"
            // -- the ptx was refused entry due to STRICTENC, a mandatory flag,
            // but after the upgrade the signature would have been interpreted
            // as valid Schnorr and thus STRICTENC would not happen.
            CScriptCheck check3(
                nullptr, scriptPubKey, amount, *ptx, i, mandatoryFlags ^ SCRIPT_ENABLE_SCHNORR, maxOps, cacheStore);
            if (check3())
            {
                inputResult.pushKV("error",
                    strprintf("upgrade-conditional-script-failure (%s)", ScriptErrorString(check.GetScriptError())));
            }

            inputVerified = false;
            allPassed = false;
            inputResult.pushKV(
                "error", strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
        }
        inputResult.pushKV("valid", inputVerified);
        inputsCheckList.push_back(inputResult);
        if (sighashType)
            *sighashType = check.sighashType;
    }
    inputsCheckResult.pushKV("valid", allPassed);
    inputsCheckResult.pushKV("inputs", inputsCheckList);
    return inputsCheckResult;
}

UniValue ValidateTransaction(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx,
    std::vector<COutPoint> &coins_to_uncache)
{
    const uint256 txid = ptx->GetHash();
    UniValue transactionAssessment(UniValue::VOBJ);
    transactionAssessment.pushKV("txid", txid.ToString());
    transactionAssessment.pushKV("txhash", txid.ToString());

    bool minable = true;
    bool futureMinable = true;
    bool standard = true;

    UniValue errorList(UniValue::VARR);

    unsigned int nSigOps = 0;
    ValidationResourceTracker resourceTracker;
    unsigned int nSize = 0;
    if (pfMissingInputs)
        *pfMissingInputs = false;

    const CChainParams &chainparams = Params();

    if (!CheckTransaction(ptx, state))
    {
        if (state.GetDebugMessage() == "")
            state.SetDebugMessage("CheckTransaction failed");
        errorList.push_back(state.GetRejectReason());
        state = CValidationState();
    }

    // Coinbase is only valid in a block, not as a loose transaction
    if (ptx->IsCoinBase())
    {
        errorList.push_back("Coinbase is only valid in a block, not as a loose transaction");
        minable = false;
        futureMinable = false;
    }

    // Reject nonstandard transactions if so configured.
    // (-testnet/-regtest allow nonstandard, and explicit submission via RPC)
    std::string reason;
    bool fRequireStandard = chainparams.RequireStandard();

    if (allowedTx == TransactionClass::STANDARD)
    {
        fRequireStandard = true;
    }
    else if (allowedTx == TransactionClass::NONSTANDARD)
    {
        fRequireStandard = false;
    }
    if (fRequireStandard && !IsStandardTx(ptx, reason))
    {
        errorList.push_back(reason);
    }

    const uint32_t cds_flag =
        IsNov2018Activated(chainparams.GetConsensus(), chainActive.Tip()) ? SCRIPT_ENABLE_CHECKDATASIG : 0;
    const uint32_t schnorrflag =
        IsMay2019Enabled(chainparams.GetConsensus(), chainActive.Tip()) ? SCRIPT_ENABLE_SCHNORR : 0;

    const uint32_t segwit_flag = IsMay2019Enabled(chainparams.GetConsensus(), chainActive.Tip()) && !fRequireStandard ?
                                     SCRIPT_ALLOW_SEGWIT_RECOVERY :
                                     0;

    const uint32_t featureFlags = cds_flag | schnorrflag | segwit_flag;
    const uint32_t flags = STANDARD_SCRIPT_VERIFY_FLAGS | featureFlags;

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(ptx, STANDARD_LOCKTIME_VERIFY_FLAGS))
    {
        errorList.push_back("non-final");
        minable = false;
    }

    // Make sure ptx size is acceptable after Nov 15, 2018 fork
    if (IsNov2018Activated(chainparams.GetConsensus(), chainActive.Tip()))
    {
        if (ptx->GetTxSize() < MIN_TX_SIZE)
        {
            errorList.push_back("txn-undersize");
            minable = false;
        }
    }

    if (pool.exists(txid))
    {
        errorList.push_back("txn-already-in-mempool");
    }

    bool txnMempoolConflict = false;
    {
        READLOCK(pool.cs); // protect pool.mapNextTx
        // Check for conflicts with in-memory transactions
        for (const CTxIn &txin : ptx->vin)
        {
            auto itConflicting = pool.mapNextTx.find(txin.prevout);
            if (itConflicting != pool.mapNextTx.end())
            {
                minable = false;
                futureMinable = false;
                txnMempoolConflict = true;
                errorList.push_back(
                    "tx-mempool-conflict: " + txin.prevout.hash.ToString() + ":" + std::to_string(txin.prevout.n));
            }
        }
    }
    if (txnMempoolConflict)
    {
        errorList.push_back("txn-mempool-conflict");
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        LockPoints lp;
        {
            READLOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
            view.SetBackend(viewMemPool);
            bool txnAlreadyKnown = false;
            // do all inputs exist?
            if (pfMissingInputs)
            {
                *pfMissingInputs = false;
                for (const CTxIn &txin : ptx->vin)
                {
                    if (!pcoinsTip->HaveCoinInCache(txin.prevout))
                    {
                        coins_to_uncache.push_back(txin.prevout);
                    }

                    if (!view.HaveCoin(txin.prevout))
                    {
                        // Are inputs missing because we already have the ptx?
                        for (size_t out = 0; out < ptx->vout.size(); out++)
                        {
                            // Optimistically just do efficient check of cache for
                            // outputs.
                            if (pcoinsTip->HaveCoinInCache(COutPoint(txid, out)))
                            {
                                // return state.Invalid(false, REJECT_DUPLICATE,
                                //                     "txn-already-known");
                                // errorList.push_back("txn-already-known");
                                txnAlreadyKnown = true;
                            }
                        }
                        minable = false;
                        futureMinable = false;
                        errorList.push_back("input-does-not-exist: " + txin.prevout.hash.ToString() + ":" +
                                            std::to_string(txin.prevout.n));
                    }
                }
                if (txnAlreadyKnown)
                {
                    errorList.push_back("txn-already-known");
                }
                if (*pfMissingInputs)
                {
                    errorList.push_back("inputs-does-not-exist");
                }
            }
            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(*ptx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);

            // Only accept BIP68 sequence locked transactions that can be mined in the next
            // block; we don't want our mempool filled up with transactions that can't
            // be mined yet.
            // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
            // CoinsViewCache instead of create its own
            bool validLP = TestLockPointValidity(&lp);
            if (!CheckSequenceLocks(ptx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp, validLP))
            {
                errorList.push_back("non-BIP68-final");
            }
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(ptx, view))
        {
            errorList.push_back("bad-txns-nonstandard-inputs");
            standard = false;
        }

        nSigOps = GetLegacySigOpCount(ptx, STANDARD_CHECKDATASIG_VERIFY_FLAGS);
        nSigOps += GetP2SHSigOpCount(ptx, view, STANDARD_CHECKDATASIG_VERIFY_FLAGS);

        CAmount nValueOut = ptx->GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        double nPriorityDummy = 0;
        pool.ApplyDeltas(txid, nPriorityDummy, nModifiedFees);

        CAmount inChainInputValue;
        double dPriority = view.GetPriority(*ptx, chainActive.Height(), inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        for (const CTxIn &txin : ptx->vin)
        {
            CoinAccessor coin(view, txin.prevout);
            if (coin->IsCoinBase())
            {
                fSpendsCoinbase = true;
                break;
            }
        }

        CTxCommitData eData; // TODO awkward construction in these 4 lines
        CTxMemPoolEntry entryTemp(ptx, nFees, GetTime(), dPriority, chainActive.Height(), pool.HasNoInputsOf(*ptx),
            inChainInputValue, fSpendsCoinbase, nSigOps, lp);
        eData.entry = entryTemp;
        CTxMemPoolEntry &entry(eData.entry);
        nSize = entry.GetTxSize();

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine.
        if (nSigOps > MAX_TX_SIGOPS)
        {
            errorList.push_back("bad-txns-too-many-sigops");
            minable = false;
        }
        CAmount mempoolRejectFee =
            pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);

        if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee)
        {
            errorList.push_back("mempool min fee not met");
            standard = false;
        }
        else if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nModifiedFees < ::minRelayTxFee.GetFee(nSize) &&
                 !AllowFree(entry.GetPriority(chainActive.Height() + 1)))
        {
            errorList.push_back("insufficient-priority");
            errorList.push_back("insufficient-fee: need " + std::to_string(minRelayTxFee.GetFee(nSize)) + " was only " +
                                std::to_string(nModifiedFees));
            errorList.push_back("minimum-fee: " + std::to_string(minRelayTxFee.GetFee(nSize)));
            standard = false;
        }
        transactionAssessment.pushKV("size", (int)nSize);
        transactionAssessment.pushKV("txfee", nModifiedFees);
        transactionAssessment.pushKV("txfeeneeded", minRelayTxFee.GetFee(nSize));

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
            if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize))
            {
                static double dFreeCount = 0;

                dFreeCount *= std::pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));

                if ((dFreeCount + nSize) >=
                    (nFreeLimit * 10 * 1000 * nLargestBlockSeen / BLOCKSTREAM_CORE_MAX_BLOCK_SIZE))
                {
                    errorList.push_back("rate limited free transaction");
                    standard = false;
                }
                dFreeCount += nSize;
            }
            nLastTime = nNow;
        }
        if (fRejectAbsurdFee && nFees > std::max((int64_t)100L * nSize, maxTxFee.Value()) * 100)
        {
            errorList.push_back("absurdly-high-fee");
            standard = false;
        }
        eData.hash = txid;
        // Calculate in-mempool ancestors, up to a limit.
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
        std::string errString;

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        unsigned char sighashType = 0;
        UniValue inputsCheckResult = CheckInputs(
            ptx, state, view, true, flags, maxScriptOps.Value(), true, &resourceTracker, nullptr, &sighashType);
        transactionAssessment.pushKV("inputscheck", inputsCheckResult);
        if (inputsCheckResult["valid"].isFalse())
        {
            errorList.push_back("input-script-failed");
            minable = false;
            futureMinable = false;
        }
        entry.UpdateRuntimeSigOps(resourceTracker.GetSigOps(), resourceTracker.GetSighashBytes());

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        unsigned char sighashType2 = 0;
        UniValue inputsCheckResult2 = CheckInputsBetter(ptx, state, view, true,
            MANDATORY_SCRIPT_VERIFY_FLAGS | featureFlags, maxScriptOps.Value(), true, nullptr, nullptr, &sighashType2);
        transactionAssessment.pushKV("inputscheck2", inputsCheckResult2);
        if (inputsCheckResult2["valid"].isFalse())
        {
            errorList.push_back("CheckInputs failed against mandatory but not standard flags");
            minable = false;
            futureMinable = false;
        }


        entry.sighashType = sighashType | sighashType2;

        // This code denies old style ptx from entering the mempool as soon as we fork
        if (!IsTxUAHFOnly(entry))
        {
            errorList.push_back("txn-uses-old-sighash-algorithm");
        }

        {
            READLOCK(pool.cs);
            CTxMemPool::setEntries setAncestors;
            // note we could resolve ancestors to hashes and return those if that saves time in the txc thread
            if (!pool._CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize,
                    nLimitDescendants, nLimitDescendantSize, errString))
            {
                errorList.push_back("too-long-mempool-chain");
                minable = false;
            }
        }
    }
    transactionAssessment.pushKV("minable", minable);
    transactionAssessment.pushKV("futureMinable", futureMinable);
    transactionAssessment.pushKV("standard", standard);
    //    errorList.shrink_to_fit();
    transactionAssessment.pushKV("errors", errorList);

    return transactionAssessment;
}

UniValue VerifyTransactionWithMemoryPool(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx)
{
    std::vector<COutPoint> vCoinsToUncache;
    UniValue res = ValidateTransaction(pool, state, ptx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit,
        fRejectAbsurdFee, allowedTx, vCoinsToUncache);
    if (res["minable"].isFalse())
    {
        for (const COutPoint &outpoint : vCoinsToUncache)
        {
            pcoinsTip->Uncache(outpoint);
        }
    }
    // After we've (potentially) uncached entries, ensure our coins cache is
    // still within its size limits
    CValidationState stateDummy;
    FlushStateToDisk(stateDummy, FLUSH_STATE_PERIODIC);
    return res;
}
