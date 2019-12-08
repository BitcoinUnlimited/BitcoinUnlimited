// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "policy/fees.h"
#include "policy/policy.h"

#include "amount.h"
#include "main.h" // for minRelayTxFee
#include "primitives/transaction.h"
#include "streams.h"
#include "txmempool.h"
#include "unlimited.h"
#include "util.h"
#include "utilmoneystr.h"


void TxConfirmStats::Initialize(std::vector<double> &defaultBuckets,
    unsigned int maxConfirms,
    double _decay,
    std::string _dataTypeString)
{
    if (_decay <= 0.00 || _decay >= 1.00)
    {
        throw std::runtime_error("Decay must be between 0 and 1 (non-inclusive)");
    }
    decay = _decay;
    dataTypeString = _dataTypeString;
    for (unsigned int i = 0; i < defaultBuckets.size(); i++)
    {
        buckets.push_back(defaultBuckets[i]);
        bucketMap[defaultBuckets[i]] = i;
    }
    confAvg.resize(maxConfirms);
    curBlockConf.resize(maxConfirms);
    unconfTxs.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++)
    {
        confAvg[i].resize(buckets.size());
        curBlockConf[i].resize(buckets.size());
        unconfTxs[i].resize(buckets.size());
    }

    oldUnconfTxs.resize(buckets.size());
    curBlockTxCt.resize(buckets.size());
    txCtAvg.resize(buckets.size());
    curBlockVal.resize(buckets.size());
    avg.resize(buckets.size());
}

// Zero out the data for the current block
void TxConfirmStats::ClearCurrent(unsigned int nBlockHeight)
{
    for (unsigned int j = 0; j < buckets.size(); j++)
    {
        oldUnconfTxs[j] += unconfTxs[nBlockHeight % unconfTxs.size()][j];
        unconfTxs[nBlockHeight % unconfTxs.size()][j] = 0;
        for (unsigned int i = 0; i < curBlockConf.size(); i++)
            curBlockConf[i][j] = 0;
        curBlockTxCt[j] = 0;
        curBlockVal[j] = 0;
    }
}


void TxConfirmStats::Record(int blocksToConfirm, double val)
{
    // blocksToConfirm is 1-based
    if (blocksToConfirm < 1)
        return;
    unsigned int bucketindex = bucketMap.lower_bound(val)->second;
    for (size_t i = blocksToConfirm; i <= curBlockConf.size(); i++)
    {
        curBlockConf[i - 1][bucketindex]++;
    }
    curBlockTxCt[bucketindex]++;
    curBlockVal[bucketindex] += val;
}

void TxConfirmStats::UpdateMovingAverages()
{
    for (unsigned int j = 0; j < buckets.size(); j++)
    {
        for (unsigned int i = 0; i < confAvg.size(); i++)
        {
            confAvg[i][j] = confAvg[i][j] * decay + curBlockConf[i][j];
        }
        avg[j] = avg[j] * decay + curBlockVal[j];
        txCtAvg[j] = (txCtAvg[j] * decay) + curBlockTxCt[j];
    }
}

// returns -1 on error conditions
CAmount TxConfirmStats::EstimateMedianVal(int confTarget,
    double sufficientTxVal,
    double successBreakPoint,
    unsigned int nBlockHeight)
{
    // bucket calculations are doubles, but the minTxFee is an CAmount, this loss of precision is
    // intentional and ok as it will only cut off fractions of a satoshi
    CAmount minTxFee = minRelayTxFee.GetFeePerK(); // sats per 1000 bytes

    // Counters for a bucket (or range of buckets)
    double nConf = 0; // Number of tx's confirmed within the confTarget
    double totalNum = 0; // Total number of tx's that were ever confirmed
    int32_t extraNum = 0; // Number of tx's still in mempool for confTarget or longer

    int32_t maxbucketindex = buckets.size() - 1;

    // We want a sub-vector of buckets to be our range from which we select a bucket for our fee
    // so instead of copying each bucket into a new vector we can simulate a sub-vector that is
    // between bucketFront and bucketBack (naming from std::vector.front and std::vector.back)
    int32_t selectedBucket = -1;

    uint32_t bins = unconfTxs.size();

    // Start counting from highest fee transactions
    for (int32_t bucket = maxbucketindex; bucket >= 0 && bucket <= maxbucketindex; bucket = bucket - 1)
    {
        // add the moving average number of confirmed tx's for the conf target in bucket
        nConf += confAvg[confTarget - 1][bucket];
        // add the moving average number of transactions in bucket to the total number of transactions
        totalNum += txCtAvg[bucket];
        for (uint32_t confct = confTarget; confct < GetMaxConfirms(); confct++)
        {
            // add number of unconfirmed transactions for a conf target in the given bucket (less than MAX_CONFIRMS)
            extraNum += unconfTxs[(nBlockHeight - confct) % bins][bucket];
        }
        // add num txs still unconfirmed after MAX_CONFIRMS in the given bucket
        extraNum += oldUnconfTxs[bucket];

        // if we have no pending confirmations for this bucket we can continue, we do this because the decay rate
        // can skew the data for a bucket making it seem like the bucket has a lower than 100%
        // confirmation rate when in reality the bucket has had no pending transactions in it for a while
        if (extraNum == 0)
        {
            continue;
        }

        // check for enough data points
        if (totalNum >= sufficientTxVal / (1 - decay))
        {
            // find the rate at which transactions in this bucket are being confirmed
            double curPct = nConf / (totalNum + extraNum);
            if (curPct < successBreakPoint)
            {
                selectedBucket = bucket;
                break;
            }
            nConf = 0;
            totalNum = 0;
            extraNum = 0;
        }
    }
    // if our confirm rate for any bucket is never less than 80% selectedBucket will
    // be -1 at the end of the loop.
    // so we return mintxfee
    if (selectedBucket <= 0)
    {
        return minTxFee;
    }

    CAmount median = -1;

    // check if the historical moving average of txs in this bucket is 0
    if (txCtAvg[selectedBucket] != 0)
    {
        // if it is not, we are in the right bucket
        median = avg[selectedBucket] / txCtAvg[selectedBucket];
    }
    else
    {
        return minTxFee;
    }
    // if we didnt error but somehow got a value less than the mintxfee return the mintxfee
    if (median > 0 && median < minTxFee)
    {
        median = minTxFee;
    }

    LOG(ESTIMATEFEE, "%3d: For conf success > %4.2f need >: %12.5g from bucket %8g  Cur Bucket "
                     "stats %6.2f%%  %8.1f/(%.1f+%d mempool)\n",
        confTarget, successBreakPoint, median, buckets[selectedBucket], 100 * nConf / (totalNum + extraNum), nConf,
        totalNum, extraNum);


    return median;
}

void TxConfirmStats::Write(CAutoFile &fileout)
{
    fileout << decay;
    fileout << buckets;
    fileout << avg;
    fileout << txCtAvg;
    fileout << confAvg;
}

void TxConfirmStats::Read(CAutoFile &filein)
{
    // Read data file into temporary variables and do some very basic sanity checking
    std::vector<double> fileBuckets;
    std::vector<double> fileAvg;
    std::vector<std::vector<double> > fileConfAvg;
    std::vector<double> fileTxCtAvg;
    double fileDecay;
    size_t maxConfirms;
    size_t numBuckets;

    filein >> fileDecay;
    if (fileDecay <= 0 || fileDecay >= 1)
        throw std::runtime_error("Corrupt estimates file. Decay must be between 0 and 1 (non-inclusive)");
    filein >> fileBuckets;
    numBuckets = fileBuckets.size();
    if (numBuckets <= 1 || numBuckets > 1000)
        throw std::runtime_error("Corrupt estimates file. Must have between 2 and 1000 fee buckets");
    filein >> fileAvg;
    if (fileAvg.size() != numBuckets)
        throw std::runtime_error("Corrupt estimates file. Mismatch in fee average bucket count");
    filein >> fileTxCtAvg;
    if (fileTxCtAvg.size() != numBuckets)
        throw std::runtime_error("Corrupt estimates file. Mismatch in tx count bucket count");
    filein >> fileConfAvg;
    maxConfirms = fileConfAvg.size();
    if (maxConfirms <= 0 || maxConfirms > 6 * 24 * 7) // one week
        throw std::runtime_error(
            "Corrupt estimates file.  Must maintain estimates for between 1 and 1008 (one week) confirms");
    for (unsigned int i = 0; i < maxConfirms; i++)
    {
        if (fileConfAvg[i].size() != numBuckets)
            throw std::runtime_error("Corrupt estimates file. Mismatch in fee conf average bucket count");
    }
    // Now that we've processed the entire fee estimate data file and not
    // thrown any errors, we can copy it to our data structures
    decay = fileDecay;
    buckets = fileBuckets;
    avg = fileAvg;
    confAvg = fileConfAvg;
    txCtAvg = fileTxCtAvg;
    bucketMap.clear();

    // Resize the current block variables which aren't stored in the data file
    // to match the number of confirms and buckets
    curBlockConf.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++)
    {
        curBlockConf[i].resize(buckets.size());
    }
    curBlockTxCt.resize(buckets.size());
    curBlockVal.resize(buckets.size());

    unconfTxs.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++)
    {
        unconfTxs[i].resize(buckets.size());
    }
    oldUnconfTxs.resize(buckets.size());

    for (unsigned int i = 0; i < buckets.size(); i++)
        bucketMap[buckets[i]] = i;

    LOG(ESTIMATEFEE, "Reading estimates: %u %s buckets counting confirms up to %u blocks\n", numBuckets, dataTypeString,
        maxConfirms);
}

unsigned int TxConfirmStats::NewTx(unsigned int nBlockHeight, double val)
{
    unsigned int bucketindex = bucketMap.lower_bound(val)->second;
    unsigned int blockIndex = nBlockHeight % unconfTxs.size();
    unconfTxs[blockIndex][bucketindex]++;
    LOG(ESTIMATEFEE, "adding to %s", dataTypeString);
    return bucketindex;
}

void TxConfirmStats::removeTx(unsigned int entryHeight, unsigned int nBestSeenHeight, unsigned int bucketindex)
{
    // nBestSeenHeight is not updated yet for the new block
    int blocksAgo = nBestSeenHeight - entryHeight;
    if (nBestSeenHeight == 0) // the BlockPolicyEstimator hasn't seen any blocks yet
        blocksAgo = 0;
    if (blocksAgo < 0)
    {
        LOG(ESTIMATEFEE, "Blockpolicy error, blocks ago is negative for mempool tx\n");
        return; // This can't happen because we call this with our best seen height, no entries can have higher
    }

    if (blocksAgo >= (int)unconfTxs.size())
    {
        if (oldUnconfTxs[bucketindex] > 0)
            oldUnconfTxs[bucketindex]--;
        else
            LOG(ESTIMATEFEE, "Blockpolicy error, mempool tx removed from >25 blocks,bucketIndex=%u already\n",
                bucketindex);
    }
    else
    {
        unsigned int blockIndex = entryHeight % unconfTxs.size();
        if (unconfTxs[blockIndex][bucketindex] > 0)
            unconfTxs[blockIndex][bucketindex]--;
        else
            LOG(ESTIMATEFEE, "Blockpolicy error, mempool tx removed from blockIndex=%u,bucketIndex=%u already\n",
                blockIndex, bucketindex);
    }
}

void CBlockPolicyEstimator::removeTx(uint256 hash)
{
    std::map<uint256, TxStatsInfo>::iterator pos = mapMemPoolTxs.find(hash);
    if (pos == mapMemPoolTxs.end())
    {
        LOG(ESTIMATEFEE, "Blockpolicy error mempool tx %s not found for removeTx\n", hash.ToString().c_str());
        return;
    }
    TxConfirmStats *stats = pos->second.stats;
    unsigned int entryHeight = pos->second.blockHeight;
    unsigned int bucketIndex = pos->second.bucketIndex;

    if (stats != nullptr)
        stats->removeTx(entryHeight, nBestSeenHeight, bucketIndex);
    mapMemPoolTxs.erase(hash);
}

CBlockPolicyEstimator::CBlockPolicyEstimator(const CFeeRate &_minRelayFee) : nBestSeenHeight(0)
{
    minTrackedFee = _minRelayFee < CFeeRate(MIN_FEERATE) ? CFeeRate(MIN_FEERATE) : _minRelayFee;
    std::vector<double> vfeelist;
    for (double bucketBoundary = minTrackedFee.GetFeePerK(); bucketBoundary <= MAX_FEERATE;
         bucketBoundary *= FEE_SPACING)
    {
        vfeelist.push_back(bucketBoundary);
    }
    vfeelist.push_back(INF_FEERATE);
    feeStats.Initialize(vfeelist, MAX_BLOCK_CONFIRMS, DEFAULT_DECAY, "FeeRate");

    feeUnlikely = CFeeRate(0);
    feeLikely = CFeeRate(INF_FEERATE);
}

void CBlockPolicyEstimator::processTransaction(const CTxMemPoolEntry &entry, bool fCurrentEstimate)
{
    unsigned int txHeight = entry.GetHeight();
    uint256 hash = entry.GetTx().GetHash();
    if (mapMemPoolTxs[hash].stats != nullptr)
    {
        LOG(ESTIMATEFEE, "Blockpolicy error mempool tx %s already being tracked\n", hash.ToString().c_str());
        return;
    }

    if (txHeight < nBestSeenHeight)
    {
        // Ignore side chains and re-orgs; assuming they are random they don't
        // affect the estimate.  We'll potentially double count transactions in 1-block reorgs.
        return;
    }

    // Only want to be updating estimates when our blockchain is synced,
    // otherwise we'll miscalculate how many blocks its taking to get included.
    if (!fCurrentEstimate)
        return;

    if (!entry.WasClearAtEntry())
    {
        // This transaction depends on other transactions in the mempool to
        // be included in a block before it will be able to be included, so
        // we shouldn't include it in our calculations
        return;
    }

    // Fees are stored and reported as BCH-per-kb:
    CFeeRate feeRate(entry.GetFee(), entry.GetTxSize());

    mapMemPoolTxs[hash].blockHeight = txHeight;

    LOG(ESTIMATEFEE, "Blockpolicy mempool tx %s ", hash.ToString().substr(0, 10));
    mapMemPoolTxs[hash].stats = &feeStats;
    mapMemPoolTxs[hash].bucketIndex = feeStats.NewTx(txHeight, (double)feeRate.GetFeePerK());
    LOG(ESTIMATEFEE, "\n");
}

void CBlockPolicyEstimator::processBlockTx(unsigned int nBlockHeight, const CTxMemPoolEntry &entry)
{
    if (!entry.WasClearAtEntry())
    {
        // This transaction depended on other transactions in the mempool to
        // be included in a block before it was able to be included, so
        // we shouldn't include it in our calculations
        return;
    }

    // How many blocks did it take for miners to include this transaction?
    // blocksToConfirm is 1-based, so a transaction included in the earliest
    // possible block has confirmation count of 1
    int blocksToConfirm = nBlockHeight - entry.GetHeight();
    if (blocksToConfirm <= 0)
    {
        // This can't happen because we don't process transactions from a block with a height
        // lower than our greatest seen height
        LOG(ESTIMATEFEE, "Blockpolicy error Transaction had negative blocksToConfirm\n");
        return;
    }
    // Fees are stored and reported as BCH-per-kb:
    CFeeRate feeRate(entry.GetFee(), entry.GetTxSize());
    feeStats.Record(blocksToConfirm, (double)feeRate.GetFeePerK());
}

void CBlockPolicyEstimator::processBlock(unsigned int nBlockHeight,
    const CTxMemPool::setEntries &setTxnsInBlock,
    bool fCurrentEstimate)
{
    if (nBlockHeight <= nBestSeenHeight)
    {
        // Ignore side chains and re-orgs; assuming they are random
        // they don't affect the estimate.
        // And if an attacker can re-org the chain at will, then
        // you've got much bigger problems than "attacker can influence
        // transaction fees."
        return;
    }
    nBestSeenHeight = nBlockHeight;

    // Only want to be updating estimates when our blockchain is synced,
    // otherwise we'll miscalculate how many blocks its taking to get included.
    if (!fCurrentEstimate)
        return;

    // Update the dynamic cutoffs
    // a fee is "likely" the reason your tx was included in a block if >85% of such tx's
    // were confirmed in 2 blocks and is "unlikely" if <50% were confirmed in 10 blocks
    LOG(ESTIMATEFEE, "Blockpolicy recalculating dynamic cutoffs:\n");

    CAmount feeLikelyEst = feeStats.EstimateMedianVal(2, SUFFICIENT_FEETXS, MIN_SUCCESS_PCT, nBlockHeight);
    if (feeLikelyEst == -1)
        feeLikely = CFeeRate(INF_FEERATE);
    else
        feeLikely = CFeeRate(feeLikelyEst);

    // Clear the current block states
    feeStats.ClearCurrent(nBlockHeight);

    // Repopulate the current block states
    for (auto &it : setTxnsInBlock)
        processBlockTx(nBlockHeight, *it);

    // Update all exponential averages with the current block states
    feeStats.UpdateMovingAverages();

    LOG(ESTIMATEFEE, "Blockpolicy after updating estimates for %u confirmed entries, new mempool map size %u\n",
        setTxnsInBlock.size(), mapMemPoolTxs.size());
}

CFeeRate CBlockPolicyEstimator::estimateFee(int confTarget)
{
    // Return failure if trying to analyze a target we're not tracking
    if (confTarget <= 0 || (unsigned int)confTarget > feeStats.GetMaxConfirms())
        return CFeeRate(0);

    CAmount median = feeStats.EstimateMedianVal(confTarget, SUFFICIENT_FEETXS, MIN_SUCCESS_PCT, nBestSeenHeight);

    if (median < 0)
        return CFeeRate(0);

    return CFeeRate(median);
}

void CBlockPolicyEstimator::Write(CAutoFile &fileout)
{
    fileout << nBestSeenHeight;
    feeStats.Write(fileout);
}

void CBlockPolicyEstimator::Read(CAutoFile &filein)
{
    int nFileBestSeenHeight;
    filein >> nFileBestSeenHeight;
    feeStats.Read(filein);
    nBestSeenHeight = nFileBestSeenHeight;
}
