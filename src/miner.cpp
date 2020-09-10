// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "hashwrapper.h"
#include "main.h"
#include "net.h"
#include "parallel.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "unlimited.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation/validation.h"
#include "validationinterface.h"

#include <algorithm>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <queue>
#include <thread>

// Track timing information for Score and Package mining.
std::atomic<int64_t> nTotalPackage{0};
std::atomic<int64_t> nTotalScore{0};

/** Maximum number of failed attempts to insert a package into a block */
static const unsigned int MAX_PACKAGE_FAILURES = 5;
extern CTweak<unsigned int> xvalTweak;

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;


class ScoreCompare
{
public:
    ScoreCompare() {}
    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b) const
    {
        return CompareTxMemPoolEntryByScore()(*b, *a); // Convert to less than
    }
};

int64_t UpdateTime(CBlockHeader *pblock, const Consensus::Params &consensusParams, const CBlockIndex *pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

BlockAssembler::BlockAssembler(const CChainParams &_chainparams)
    : chainparams(_chainparams), nBlockSize(0), nBlockTx(0), nBlockSigOps(0), nFees(0), nHeight(0), nLockTimeCutoff(0),
      lastFewTxs(0), blockFinished(false)
{
    // Largest block you're willing to create:
    nBlockMaxSize = maxGeneratedBlock;
    // Core:
    // nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    // Limit to between 1K and MAX_BLOCK_SIZE-1K for sanity:
    // nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE-1000), nBlockMaxSize));

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);
}

void BlockAssembler::resetBlock(const CScript &scriptPubKeyIn, int64_t coinbaseSize)
{
    inBlock.clear();

    nBlockSize = reserveBlockSize(scriptPubKeyIn, coinbaseSize); // Core: 1000
    nBlockSigOps = 100; // Reserve 100 sigops for miners to use in their coinbase transaction

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;

    lastFewTxs = 0;
    blockFinished = false;
}

uint64_t BlockAssembler::reserveBlockSize(const CScript &scriptPubKeyIn, int64_t coinbaseSize)
{
    CBlockHeader h;
    uint64_t nHeaderSize, nCoinbaseSize, nCoinbaseReserve;

    // BU add the proper block size quantity to the actual size
    nHeaderSize = ::GetSerializeSize(h, SER_NETWORK, PROTOCOL_VERSION);
    assert(nHeaderSize == 80); // BU always 80 bytes
    nHeaderSize += 5; // tx count varint - 5 bytes is enough for 4 billion txs; 3 bytes for 65535 txs


    // This serializes with output value, a fixed-length 8 byte field, of zero and height, a serialized CScript
    // signed integer taking up 4 bytes for heights 32768-8388607 (around the year 2167) after which it will use 5
    nCoinbaseSize = ::GetSerializeSize(coinbaseTx(scriptPubKeyIn, 400000, 0), SER_NETWORK, PROTOCOL_VERSION);

    if (coinbaseSize >= 0) // Explicit size of coinbase has been requested
    {
        nCoinbaseReserve = (uint64_t)coinbaseSize;
    }
    else
    {
        nCoinbaseReserve = coinbaseReserve.Value();
    }

    // BU Miners take the block we give them, wipe away our coinbase and add their own.
    // So if their reserve choice is bigger then our coinbase then use that.
    nCoinbaseSize = std::max(nCoinbaseSize, nCoinbaseReserve);


    return nHeaderSize + nCoinbaseSize;
}
CTransactionRef BlockAssembler::coinbaseTx(const CScript &scriptPubKeyIn, int _nHeight, CAmount nValue)
{
    CMutableTransaction tx;

    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = scriptPubKeyIn;
    tx.vout[0].nValue = nValue;
    tx.vin[0].scriptSig = CScript() << _nHeight << OP_0;

    // BU005 add block size settings to the coinbase
    std::string cbmsg = FormatCoinbaseMessage(BUComments, minerComment);
    const char *cbcstr = cbmsg.c_str();
    vector<unsigned char> vec(cbcstr, cbcstr + cbmsg.size());
    {
        LOCK(cs_coinbaseFlags);
        COINBASE_FLAGS = CScript() << vec;
        // Chop off any extra data in the COINBASE_FLAGS so the sig does not exceed the max.
        // we can do this because the coinbase is not a "real" script...
        if (tx.vin[0].scriptSig.size() + COINBASE_FLAGS.size() > MAX_COINBASE_SCRIPTSIG_SIZE)
        {
            COINBASE_FLAGS.resize(MAX_COINBASE_SCRIPTSIG_SIZE - tx.vin[0].scriptSig.size());
        }

        tx.vin[0].scriptSig = tx.vin[0].scriptSig + COINBASE_FLAGS;
    }

    // Make sure the coinbase is big enough.
    uint64_t nCoinbaseSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    if (nCoinbaseSize < MIN_TX_SIZE && IsNov2018Activated(Params().GetConsensus(), chainActive.Tip()))
    {
        tx.vin[0].scriptSig << std::vector<uint8_t>(MIN_TX_SIZE - nCoinbaseSize - 1);
    }

    return MakeTransactionRef(std::move(tx));
}

struct NumericallyLessTxHashComparator
{
public:
    bool operator()(const CTxMemPoolEntry *a, const CTxMemPoolEntry *b) const
    {
        return a->GetTx().GetHash() < b->GetTx().GetHash();
    }
};

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript &scriptPubKeyIn, int64_t coinbaseSize)
{
    resetBlock(scriptPubKeyIn, coinbaseSize);

    // The constructed block template
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());

    CBlock *pblock = &pblocktemplate->block;

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    LOCK(cs_main);
    CBlockIndex *pindexPrev = chainActive.Tip();
    assert(pindexPrev); // can't make a new block if we don't even have the genesis block

    may2020Enabled = IsMay2020Activated(Params().GetConsensus(), pindexPrev);

    if (may2020Enabled)
    {
        maxSigOpsAllowed = maxSigChecks.Value();
    }


    {
        READLOCK(mempool.cs_txmempool);
        nHeight = pindexPrev->nHeight + 1;

        pblock->nTime = GetAdjustedTime();
        pblock->nVersion = UnlimitedComputeBlockVersion(pindexPrev, chainparams.GetConsensus(), pblock->nTime);
        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (chainparams.MineBlocksOnDemand())
            pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();
        nLockTimeCutoff =
            (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : pblock->GetBlockTime();

        bool canonical = fCanonicalTxsOrder;
        // On BCH always allow overwite of fCanonicalTxsOrder but not for regtest
        if (IsNov2018Activated(Params().GetConsensus(), chainActive.Tip()))
        {
            if (chainparams.NetworkIDString() != "regtest")
            {
                canonical = true;
            }
        }
        else
        {
            if (chainparams.NetworkIDString() != "regtest")
            {
                canonical = false;
            }
        }

        std::vector<const CTxMemPoolEntry *> vtxe;
        addPriorityTxs(&vtxe);

        // Mine by package (CPFP) or by score.
        if (miningCPFP.Value() == true)
        {
            int64_t nStartPackage = GetStopwatchMicros();
            addPackageTxs(&vtxe, canonical);
            nTotalPackage += GetStopwatchMicros() - nStartPackage;
        }
        else
        {
            int64_t nStartScore = GetStopwatchMicros();
            addScoreTxs(&vtxe);
            nTotalScore += GetStopwatchMicros() - nStartScore;
        }

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        LOGA("CreateNewBlock: total size %llu txs: %llu of %llu fees: %lld sigops %u\n", nBlockSize, nBlockTx,
            mempool._size(), nFees, nBlockSigOps);


        // sort tx if there are any and the feature is enabled
        if (canonical)
        {
            std::sort(vtxe.begin(), vtxe.end(), NumericallyLessTxHashComparator());
        }

        for (auto &txe : vtxe)
        {
            pblocktemplate->block.vtx.push_back(txe->GetSharedTx());
            pblocktemplate->vTxFees.push_back(txe->GetFee());
            pblocktemplate->vTxSigOps.push_back(txe->GetSigOpCount());
        }

        // Create coinbase transaction.
        pblock->vtx[0] =
            coinbaseTx(scriptPubKeyIn, nHeight, nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus()));
        pblocktemplate->vTxFees[0] = -nFees;

        // Fill in header
        pblock->hashPrevBlock = pindexPrev->GetBlockHash();
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
        pblock->nNonce = 0;
        if (!may2020Enabled)
            pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0], STANDARD_SCRIPT_VERIFY_FLAGS);
        else // coinbase May2020 Sigchecks is always 0 since no scripts executed in coinbase tx.
            pblocktemplate->vTxSigOps[0] = 0;
    }

    // All the transactions in this block are from the mempool and therefore we can use XVal to speed
    // up the testing of the block validity. Set XVal flag for new blocks to true unless otherwise
    // configured.
    pblock->fXVal = xvalTweak.Value();

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false))
    {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    if (pblock->fExcessive)
    {
        throw std::runtime_error(strprintf("%s: Excessive block generated: %s", __func__, FormatStateMessage(state)));
    }

    return pblocktemplate;
}

bool BlockAssembler::isStillDependent(CTxMemPool::txiter iter)
{
    for (CTxMemPool::txiter parent : mempool.GetMemPoolParents(iter))
    {
        if (!inBlock.count(parent))
        {
            return true;
        }
    }
    return false;
}

bool BlockAssembler::TestPackageSigOps(uint64_t packageSize, unsigned int packageSigOps)
{
    if (!may2020Enabled) // if may2020 is enabled, its a constant
    {
        maxSigOpsAllowed = GetMaxBlockSigOpsCount(nBlockSize + packageSize);
    }

    // Note that the may2020 rule should be > so this assembles a block with 1 less sigcheck than possible
    if (nBlockSigOps + packageSigOps >= maxSigOpsAllowed)
        return false;
    return true;
}

// Block size and sigops have already been tested.  Check that all transactions
// are final.
bool BlockAssembler::TestPackageFinality(const CTxMemPool::setEntries &package)
{
    for (const CTxMemPool::txiter it : package)
    {
        if (!IsFinalTx(it->GetSharedTx(), nHeight, nLockTimeCutoff))
            return false;
    }
    return true;
}

// Return true if incremental tx or txs in the block with the given size and sigop count would be
// valid, and false otherwise.  If false, blockFinished and lastFewTxs are updated if appropriate.
bool BlockAssembler::IsIncrementallyGood(uint64_t nExtraSize, unsigned int nExtraSigOps)
{
    if (nBlockSize + nExtraSize > nBlockMaxSize)
    {
        // If the block is so close to full that no more txs will fit
        // or if we've tried more than 50 times to fill remaining space
        // then flag that the block is finished
        if (nBlockSize > nBlockMaxSize - 100 || lastFewTxs > 50)
        {
            blockFinished = true;
            return false;
        }
        // Once we're within 1000 bytes of a full block, only look at 50 more txs
        // to try to fill the remaining space.
        if (nBlockSize > nBlockMaxSize - 1000)
        {
            lastFewTxs++;
        }
        return false;
    }

    if (!may2020Enabled)
    {
        // Enforce the "old" sigops for <= 1MB blocks
        if (nBlockSize + nExtraSize <= BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
        {
            // BU: be conservative about what is generated
            if (nBlockSigOps + nExtraSigOps >= MAX_BLOCK_SIGOPS_PER_MB)
            {
                // BU: so a block that is near the sigops limit might be shorter than it could be if
                // the high sigops tx was backed out and other tx added.
                if (nBlockSigOps > MAX_BLOCK_SIGOPS_PER_MB - 2)
                    blockFinished = true;
                return false;
            }
        }
        else
        {
            if (nBlockSigOps + nExtraSigOps > GetMaxBlockSigOpsCount(nBlockSize))
            {
                if (nBlockSigOps > GetMaxBlockSigOpsCount(nBlockSize) - 2)
                    // very close to the limit, so the block is finished.  So a block that is near the sigops limit
                    // might be shorter than it could be if the high sigops tx was backed out and other tx added.
                    blockFinished = true;
                return false;
            }
        }
    }
    else // may2020
    {
        if (nBlockSigOps + nExtraSigOps > maxSigOpsAllowed)
        {
            if (nBlockSigOps > maxSigOpsAllowed - 2)
                // very close to the limit, so the block is finished.  So a block that is near the sigops limit
                // might be shorter than it could be if the high sigops tx was backed out and other tx added.
                blockFinished = true;
            return false;
        }
    }

    return true;
}

bool BlockAssembler::TestForBlock(CTxMemPool::txiter iter)
{
    if (!IsIncrementallyGood(iter->GetTxSize(), iter->GetSigOpCount()))
        return false;

    // Must check that lock times are still valid
    // This can be removed once MTP is always enforced
    // as long as reorgs keep the mempool consistent.
    if (!IsFinalTx(iter->GetSharedTx(), nHeight, nLockTimeCutoff))
        return false;

    // On BCH if Nov 15th 2019 has been activaterd make sure tx size
    // is greater or equal than 100 bytes
    if (IsNov2018Activated(Params().GetConsensus(), chainActive.Tip()))
    {
        if (iter->GetTxSize() < MIN_TX_SIZE)
            return false;
    }

    return true;
}

void BlockAssembler::AddToBlock(std::vector<const CTxMemPoolEntry *> *vtxe, CTxMemPool::txiter iter)
{
    const CTxMemPoolEntry &tmp = *iter;
    vtxe->push_back(&tmp);
    nBlockSize += iter->GetTxSize();
    ++nBlockTx;
    nBlockSigOps += iter->GetSigOpCount();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority)
    {
        double dPriority = iter->GetPriority(nHeight);
        CAmount dummy;
        mempool._ApplyDeltas(iter->GetTx().GetHash(), dPriority, dummy);
        LOGA("priority %.1f fee %s txid %s\n", dPriority,
            CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString().c_str(),
            iter->GetTx().GetHash().ToString().c_str());
    }
}

void BlockAssembler::addScoreTxs(std::vector<const CTxMemPoolEntry *> *vtxe)
{
    std::priority_queue<CTxMemPool::txiter, std::vector<CTxMemPool::txiter>, ScoreCompare> clearedTxs;
    CTxMemPool::setEntries waitSet;
    CTxMemPool::indexed_transaction_set::index<mining_score>::type::iterator mi =
        mempool.mapTx.get<mining_score>().begin();
    CTxMemPool::txiter iter;
    while (!blockFinished && (mi != mempool.mapTx.get<mining_score>().end() || !clearedTxs.empty()))
    {
        // If no txs that were previously postponed are available to try
        // again, then try the next highest score tx
        if (clearedTxs.empty())
        {
            iter = mempool.mapTx.project<0>(mi);
            mi++;
        }
        // If a previously postponed tx is available to try again, then it
        // has higher score than all untried so far txs
        else
        {
            iter = clearedTxs.top();
            clearedTxs.pop();
        }

        // If tx already in block then skip
        if (inBlock.count(iter))
        {
            continue;
        }


        // If tx is dependent on other mempool txs which haven't yet been included
        // then put it in the waitSet
        if (isStillDependent(iter))
        {
            waitSet.insert(iter);
            continue;
        }

        // If this tx fits in the block add it, otherwise keep looping
        if (TestForBlock(iter))
        {
            AddToBlock(vtxe, iter);

            // This tx was successfully added, so
            // add transactions that depend on this one to the priority queue to try again
            for (CTxMemPool::txiter child : mempool.GetMemPoolChildren(iter))
            {
                if (waitSet.count(child))
                {
                    clearedTxs.push(child);
                    waitSet.erase(child);
                }
            }
        }
    }
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries &package, std::vector<CTxMemPool::txiter> &sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
//
// This is accomplished by considering a group of ancestors as a single transaction. We can call these
// transactions, Ancestor Grouped Transactions (AGT). This approach to grouping allows us to process
// packages orders of magnitude faster than other methods of package mining since we no longer have
// to continuously update the descendant state as we mine part of an unconfirmed chain.
//
// There is a theoretical flaw in this approach which could happen when a block is almost full. We
// could for instance end up including a lower fee transaction as part of an ancestor group when
// in fact it would be better, in terms of fees, to include some other single transaction. This
// would result in slightly less fees (perhaps a few hundred satoshis) rewarded to the miner. However,
// this situation is not likely to be seen for two reasons. One, long unconfirmed chains are typically
// having transactions with all the same fees and Two, the typical child pays for parent scenario has only
// two transactions with the child having the higher fee. And neither of these two types of packages could
// cause any loss of fees with this mining algorithm, when the block is nearly full.
//
// The mining algorithm is surprisingly simple and centers around parsing though the mempools ancestor_score
// index and adding the AGT's into the new block. There is however a pathological case which has to be
// accounted for where a child transaction has less fees per KB than its parent which causes child transactions
// to show up later as we parse though the ancestor index. In this case we then have to recalculate the
// ancestor sigops and package size which can be time consuming given we have to parse through the ancestor
// tree each time. However we get around that by shortcutting the process by parsing through only the portion
// of the tree that is currently not in the block. This shortcutting happens in _CalculateMempoolAncestors()
// where we pass in the inBlock vector of already added transactions. Even so, if we didn't do this shortcutting
// the current algo is still much better than the older method which needed to update calculations for the
// entire descendant tree after each package was added to the block.

void BlockAssembler::addPackageTxs(std::vector<const CTxMemPoolEntry *> *vtxe, bool fCanonical)
{
    AssertLockHeld(mempool.cs_txmempool);

    CTxMemPool::txiter iter;
    uint64_t nPackageFailures = 0;
    for (auto mi = mempool.mapTx.get<ancestor_score>().begin(); mi != mempool.mapTx.get<ancestor_score>().end(); mi++)
    {
        iter = mempool.mapTx.project<0>(mi);

        // Skip txns we know are in the block
        if (inBlock.count(iter))
        {
            continue;
        }

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        // mempool uses same field for sigops and sigchecks
        unsigned int packageSigOps = iter->GetSigOpCountWithAncestors();

        // Get any unconfirmed ancestors of this txn
        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool._CalculateMemPoolAncestors(
            *iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, &inBlock, false);

        // Include in the package the current txn we're working with
        ancestors.insert(iter);

        // Recalculate sigops and package size, only if there were txns already in the block for
        // this set of ancestors
        if (iter->GetCountWithAncestors() > ancestors.size())
        {
            packageSize = 0;
            packageSigOps = 0;
            for (auto &it : ancestors)
            {
                packageSize += it->GetTxSize();
                packageSigOps += it->GetSigOpCount();
            }
        }
        if (packageFees < ::minRelayTxFee.GetFee(packageSize) && nBlockSize >= nBlockMinSize)
        {
            // Everything else we might consider has a lower fee rate so no need to continue
            return;
        }

        // Test if package fits in the block
        if (nBlockSize + packageSize > nBlockMaxSize)
        {
            if (nBlockSize > nBlockMaxSize * .50)
            {
                nPackageFailures++;
            }

            // If we keep failing then the block must be almost full so bail out here.
            if (nPackageFailures >= MAX_PACKAGE_FAILURES)
                return;
            else
                continue;
        }

        // Test that the package does not exceed sigops limits
        if (!TestPackageSigOps(packageSize, packageSigOps))
        {
            continue;
        }
        // Test if all tx's are Final
        if (!TestPackageFinality(ancestors))
        {
            continue;
        }

        // The Package can now be added to the block.
        if (fCanonical)
        {
            for (auto &it : ancestors)
            {
                AddToBlock(vtxe, it);
            }
        }
        else
        {
            // Sort the entries in a valid order if we are not doing CTOR
            vector<CTxMemPool::txiter> sortedEntries;
            SortForBlock(ancestors, sortedEntries);
            for (size_t i = 0; i < sortedEntries.size(); ++i)
            {
                AddToBlock(vtxe, sortedEntries[i]);
            }
        }
    }
}

void BlockAssembler::addPriorityTxs(std::vector<const CTxMemPoolEntry *> *vtxe)
{
    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    uint64_t nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    if (nBlockPrioritySize == 0)
    {
        return;
    }

    // This vector will be sorted into a priority queue:
    vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    vecPriority.reserve(mempool.mapTx.size());
    for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
    {
        double dPriority = mi->GetPriority(nHeight);
        CAmount dummy;
        mempool._ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
        vecPriority.push_back(TxCoinAgePriority(dPriority, mi));
    }
    std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);

    CTxMemPool::txiter iter;
    while (!vecPriority.empty() && !blockFinished)
    { // add a tx from priority queue to fill the blockprioritysize
        iter = vecPriority.front().second;
        actualPriority = vecPriority.front().first;
        std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
        vecPriority.pop_back();

        // If tx already in block, skip
        if (inBlock.count(iter))
        {
            DbgAssert(false, ); // shouldn't happen for priority txs
            continue;
        }

        // If tx is dependent on other mempool txs which haven't yet been included
        // then put it in the waitSet
        if (isStillDependent(iter))
        {
            waitPriMap.insert(std::make_pair(iter, actualPriority));
            continue;
        }

        // If this tx fits in the block add it, otherwise keep looping
        if (TestForBlock(iter))
        {
            AddToBlock(vtxe, iter);

            // If now that this txs is added we've surpassed our desired priority size
            // or have dropped below the AllowFreeThreshold, then we're done adding priority txs
            if (nBlockSize >= nBlockPrioritySize || !AllowFree(actualPriority))
            {
                return;
            }

            // This tx was successfully added, so
            // add transactions that depend on this one to the priority queue to try again
            for (CTxMemPool::txiter child : mempool.GetMemPoolChildren(iter))
            {
                waitPriIter wpiter = waitPriMap.find(child);
                if (wpiter != waitPriMap.end())
                {
                    vecPriority.push_back(TxCoinAgePriority(wpiter->second, child));
                    std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                    waitPriMap.erase(wpiter);
                }
            }
        }
    }
}

void IncrementExtraNonce(CBlock *pblock, unsigned int &nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pblock->GetHeight(); // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);

    CScript script = (CScript() << nHeight << CScriptNum(nExtraNonce));
    CScript cbFlags;
    {
        LOCK(cs_coinbaseFlags);
        cbFlags = COINBASE_FLAGS;
    }
    if (script.size() + cbFlags.size() > MAX_COINBASE_SCRIPTSIG_SIZE)
    {
        cbFlags.resize(MAX_COINBASE_SCRIPTSIG_SIZE - script.size());
    }
    txCoinbase.vin[0].scriptSig = script + cbFlags;
    assert(txCoinbase.vin[0].scriptSig.size() <= MAX_COINBASE_SCRIPTSIG_SIZE);

    // On BCH if Nov15th 2018 has been activated make sure the coinbase is big enough
    uint64_t nCoinbaseSize = ::GetSerializeSize(txCoinbase, SER_NETWORK, PROTOCOL_VERSION);
    if (nCoinbaseSize < MIN_TX_SIZE && IsNov2018Activated(Params().GetConsensus(), chainActive.Tip()))
    {
        txCoinbase.vin[0].scriptSig << std::vector<uint8_t>(MIN_TX_SIZE - nCoinbaseSize - 1);
    }

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}
