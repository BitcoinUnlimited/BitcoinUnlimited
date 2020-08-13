// Copyright (c) 2017-2017 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tx_verify.h"

#include "consensus.h"
#include "main.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "unlimited.h"
#include "validation.h"

// TODO remove the following dependencies
#include "chain.h"
#include "coins.h"
#include "utilmoneystr.h"

#include <boost/scope_exit.hpp>


bool IsFinalTx(const CTransactionRef tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx->nLockTime == 0)
        return true;
    if ((int64_t)tx->nLockTime < ((int64_t)tx->nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const CTxIn &txin : tx->vin)
    {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransactionRef tx,
    int flags,
    std::vector<int> *prevHeights,
    const CBlockIndex &block)
{
    assert(prevHeights->size() == tx->vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx->nVersion) >= 2 && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68)
    {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx->vin.size(); txinIndex++)
    {
        const CTxIn &txin = tx->vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG)
        {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG)
        {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight - 1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK)
                                                                << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) -
                                              1);
        }
        else
        {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex &block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransactionRef tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

// BU: This code is completely inaccurate if its used to determine the approximate time of transaction
// validation!!!  The sigop count in the output transactions are irrelevant, and the sigop count of the
// previous outputs are the most relevant, but not actually checked.
// The purpose of this is to limit the outputs of transactions so that other transactions' "prevout"
// is reasonably sized.
unsigned int GetLegacySigOpCount(const CTransactionRef tx, const uint32_t flags)
{
    unsigned int nSigOps = 0;
    for (const auto &txin : tx->vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(flags, false);
    }
    for (const auto &txout : tx->vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(flags, false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransactionRef tx, const CCoinsViewCache &inputs, const uint32_t flags)
{
    if ((flags & SCRIPT_VERIFY_P2SH) == 0 || tx->IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    {
        for (unsigned int i = 0; i < tx->vin.size(); i++)
        {
            CoinAccessor coin(inputs, tx->vin[i].prevout);
            if (coin && coin->out.scriptPubKey.IsPayToScriptHash())
                nSigOps += coin->out.scriptPubKey.GetSigOpCount(flags, tx->vin[i].scriptSig);
        }
    }
    return nSigOps;
}

bool ContextualCheckTransaction(const CTransactionRef tx,
    CValidationState &state,
    CBlockIndex *const pindexPrev,
    const CChainParams &params)
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;
    auto consensusParams = params.GetConsensus();

    if (IsMay2020Activated(consensusParams, nHeight) == false)
    {
        // Check that the transaction doesn't have an excessive number of sigops
        unsigned int nSigOps = GetLegacySigOpCount(tx, STANDARD_SCRIPT_VERIFY_FLAGS);
        if (nSigOps > MAX_TX_SIGOPS_COUNT)
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-too-many-sigops");
    }

    // Make sure tx size is equal or higher to 100 bytes if we are on the BCH chain and Nov 15th 2018 activated
    if (IsNov2018Activated(consensusParams, nHeight))
    {
        if (tx->GetTxSize() < MIN_TX_SIZE)
        {
            return state.DoS(
                10, error("%s: contains transactions that are too small", __func__), REJECT_INVALID, "txn-undersize");
        }
    }


    return true;
}

bool CheckTransaction(const CTransactionRef tx, CValidationState &state)
{
    // Basic checks that don't depend on any context
    if (tx->vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx->vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");

    // Sigops moved to ContextualCheckTransaction because the consensus rule goes away after may2020 fork

    // Size limits
    // BU: size limits removed
    // if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
    //    return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const CTxOut &txout : tx->vout)
    {
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs
    std::set<COutPoint> vInOutPoints;
    for (const CTxIn &txin : tx->vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        vInOutPoints.insert(txin.prevout);
    }

    if (tx->IsCoinBase())
    {
        // BU convert 100 to a constant so we can use it during generation
        if (tx->vin[0].scriptSig.size() < 2 || tx->vin[0].scriptSig.size() > MAX_COINBASE_SCRIPTSIG_SIZE)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    }
    else
    {
        for (const CTxIn &txin : tx->vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

/**
 * Return the spend height, which is one more than the inputs.GetBestBlock().
 * While checking, GetBestBlock() refers to the parent block. (protected by cs_main)
 * This is also true for mempool checks.
 */
static int GetSpendHeight(const CCoinsViewCache &inputs)
{
    READLOCK(cs_mapBlockIndex);
    BlockMap::iterator i = mapBlockIndex.find(inputs.GetBestBlock());
    if (i != mapBlockIndex.end())
    {
        CBlockIndex *pindexPrev = i->second;
        if (pindexPrev)
            return pindexPrev->nHeight + 1;
        else
        {
            throw std::runtime_error("GetSpendHeight(): mapBlockIndex contains null block");
        }
    }
    throw std::runtime_error("GetSpendHeight(): best block does not exist");
}

bool Consensus::CheckTxInputs(const CTransactionRef tx, CValidationState &state, const CCoinsViewCache &inputs)
{
    // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
    // for an attacker to attempt to split the network.
    if (!inputs.HaveInputs(*tx))
        return state.Invalid(false, 0, "", "Inputs unavailable");

    CAmount nValueIn = 0;
    CAmount nFees = 0;
    int nSpendHeight = -1;
    {
        for (unsigned int i = 0; i < tx->vin.size(); i++)
        {
            const COutPoint &prevout = tx->vin[i].prevout;
            Coin coin;
            inputs.GetCoin(prevout, coin); // Make a copy so I don't hold the utxo lock
            assert(!coin.IsSpent());

            // If prev is coinbase, check that it's matured
            if (coin.IsCoinBase())
            {
                // Copy these values here because once we unlock and re-lock cs_utxo we can't count on "coin"
                // still being valid.
                CAmount nCoinOutValue = coin.out.nValue;
                int nCoinHeight = coin.nHeight;

                // If there are multiple coinbase spends we still only need to get the spend height once.
                if (nSpendHeight == -1)
                {
                    nSpendHeight = GetSpendHeight(inputs);
                }
                if (nSpendHeight - nCoinHeight < COINBASE_MATURITY)
                    return state.Invalid(false, REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                        strprintf("tried to spend coinbase at depth %d", nSpendHeight - nCoinHeight));

                // Check for negative or overflow input values.  We use nCoinOutValue which was copied before
                // we released cs_utxo, because we can't be certain the value didn't change during the time
                // cs_utxo was unlocked.
                nValueIn += nCoinOutValue;
                if (!MoneyRange(nCoinOutValue) || !MoneyRange(nValueIn))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
            }
            else
            {
                // Check for negative or overflow input values
                nValueIn += coin.out.nValue;
                if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
            }
        }
    }

    if (nValueIn < tx->GetValueOut())
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
            strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(tx->GetValueOut())));

    // Tally transaction fees
    CAmount nTxFee = nValueIn - tx->GetValueOut();
    if (nTxFee < 0)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
    nFees += nTxFee;
    if (!MoneyRange(nFees))
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    return true;
}

uint64_t GetTransactionSigOpCount(const CTransactionRef ptx, const CCoinsViewCache &coins, const uint32_t flags)
{
    return GetLegacySigOpCount(ptx, flags) + GetP2SHSigOpCount(ptx, coins, flags);
}
