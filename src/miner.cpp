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
#include "respend/respenddetector.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "unlimited.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation/validation.h"
#include "validationinterface.h"

#include <boost/tuple/tuple.hpp>
//#include <coz.h>
#include <queue>
#include <thread>

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

    // merge with delta template's coinbase if available
    if (best_delta_template != nullptr)
    {
        // LOG(WB, "Delta template available. Constructing coinbase for new block template from delta block.\n");
        for (auto ancptr_opret : best_delta_template->coinbase()->vout)
            // _anc estor _poin_t_r _OPRET URN
            tx.vout.emplace_back(ancptr_opret);
    }
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

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript &scriptPubKeyIn, int64_t coinbaseSize)
{
    std::unique_ptr<CBlockTemplate> tmpl(nullptr);

    // if (nBlockMaxSize > BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
    tmpl = CreateNewBlock(scriptPubKeyIn, false, coinbaseSize);

    // If the block is too small we need to drop back to the 1MB ruleset
    /*if ((!tmpl) || (tmpl->block->GetBlockSize() <= BLOCKSTREAM_CORE_MAX_BLOCK_SIZE))
{
    nBlockMaxSize = BLOCKSTREAM_CORE_MAX_BLOCK_SIZE;
    tmpl = CreateNewBlock(scriptPubKeyIn, true, coinbaseSize);
    }*/

    return tmpl;
}

struct NumericallyLessTxHashComparator
{
public:
    bool operator()(const CTxMemPoolEntry *a, const CTxMemPoolEntry *b) const
    {
        return a->GetTx().GetHash() < b->GetTx().GetHash();
    }
};

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript &scriptPubKeyIn,
    bool blockstreamCoreCompatible,
    int64_t coinbaseSize)
{
    LOCK(cs_main);
    CBlockIndex *pindexPrev = chainActive.Tip();
    assert(pindexPrev); // can't make a new block if we don't even have the previous block
    const uint256 prevBlockHash = pindexPrev->GetBlockHash();

    best_delta_template =
        CDeltaBlock::isEnabled(Params(), pindexPrev) ? CDeltaBlock::bestTemplate(prevBlockHash) : nullptr;

    if (best_delta_template == nullptr)
    {
        if (CDeltaBlock::isEnabled(Params(), pindexPrev))
        {
            LOG(WB, "No matching deltablock for prev block hash %s found. Building regular block.\n",
                prevBlockHash.GetHex());
        }
        else
        {
            LOG(WB, "Not building deltablocks (feature disabled).\n");
        }
    }
    else
    {
        LOG(WB, "Created delta block template for further extension.\n");
    }

    resetBlock(scriptPubKeyIn, coinbaseSize);

    // The constructed block template
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());

    CBlock *pblock = pblocktemplate->block.get();

    // set delta block in template, if available
    pblocktemplate->delta_block = best_delta_template;

    if (best_delta_template != nullptr)
    {
        LOG(WB, "Delta template available, constructing delta block as well.\n");
    }

    // Add dummy coinbase tx as first transaction
    // pblock->add(CTransactionRef(new CTransaction()));
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    std::vector<const CTxMemPoolEntry *> vtxe;
    std::vector<std::shared_ptr<CTxMemPoolEntry> > extra_entries;

    /* Now, if this block is based off a delta block, add the
       transactions that are already in the base block to both the
       block and the delta block to keep it consistent. FIXME: this
       sort of duplicate housekeeping can certainly be optimized... */
    if (pblocktemplate->delta_block != nullptr)
    {
        LOG(WB, "Pre-adding %d transactions (and excluding the coinbase) from delta block.\n",
            best_delta_template->numTransactions() - 1);
        LOG(WB, "Current delta template delta set size: %d\n", best_delta_template->deltaSet().size());
        for (auto biter = best_delta_template->begin_past_coinbase(); biter != best_delta_template->end(); ++biter)
        {
            auto txr = *biter;
            bool done = false;
            {
                READLOCK(mempool.cs);

                CTxMemPool::txiter txiter = mempool.mapTx.find(txr->GetHash());
                const bool in_mempool = txiter != mempool.mapTx.end();

                const bool in_block_already = inBlock.count(txr->GetHash()) > 0;
                DbgAssert(!in_block_already, pblocktemplate->delta_block = nullptr; break;);

                if (in_mempool)
                {
                    AddToBlock(&vtxe, txiter);
                    done = true;
                }
            }
            if (!done)
            {
                READLOCK(mempool.cs);
                CCoinsViewMemPool view(pcoinsTip, mempool);
                CCoinsViewCache view_cache(&view);

                // FIXME: parts copied from txadmission.cpp
                /* FIXME: Do the assumptions about the mempool state here hold always? */
                // LOG(WB, "Transaction %s from delta block not in mempool yet.\n", txr->GetHash().GetHex());

                unsigned nSigOps = GetLegacySigOpCount(txr, STANDARD_SCRIPT_VERIFY_FLAGS);
                nSigOps += GetP2SHSigOpCount(txr, view_cache, STANDARD_SCRIPT_VERIFY_FLAGS);

                std::shared_ptr<CTxMemPoolEntry> entry(
                    new CTxMemPoolEntry(txr, 0, GetTime(), 0.0, 0, false, -(1UL << 63) /* dummy */,
                        false /* dummy spends coinbase*/, nSigOps, LockPoints() /* dummy lockpoints */));
                extra_entries.emplace_back(entry);
                AddToBlock(&vtxe, entry.get());
            }
            // Note: we don't need to add it to the delta block as this contains the transaction already!
            // COZ_PROGRESS_NAMED("pre-adding delta block transaction");
        }
    }
    READLOCK(mempool.cs);
    {
        nHeight = pindexPrev->nHeight + 1;

        pblock->nTime = GetAdjustedTime();
        pblock->nVersion = UnlimitedComputeBlockVersion(pindexPrev, chainparams.GetConsensus(), pblock->nTime);
        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (chainparams.MineBlocksOnDemand())
            pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

        if (pblocktemplate->delta_block != nullptr)
        {
            pblocktemplate->delta_block->nTime = pblock->nTime;
            pblocktemplate->delta_block->nVersion = pblock->nVersion;
        }
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();
        nLockTimeCutoff =
            (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : pblock->GetBlockTime();

        addPriorityTxs(&vtxe);
        addScoreTxs(&vtxe);

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        LOGA("CreateNewBlock(): total size %llu txs: %llu fees: %lld sigops %u\n", nBlockSize, nBlockTx, nFees,
            nBlockSigOps);

        bool canonical = enableCanonicalTxOrder.Value();
        // On BCH always allow overwite of enableCanonicalTxOrder but not for regtest
        if (IsNov2018Activated(Params().GetConsensus(), chainActive.Tip()))
        {
            if (chainparams.NetworkIDString() != "regtest")
            {
                canonical = true;
            }
        }

        LOG(WB, "Canonical order enabled: %d\n", canonical);
        // sort tx if there are any and the feature is enabled
        /*if (canonical)
        {
            std::sort(vtxe.begin(), vtxe.end(), NumericallyLessTxHashComparator());
            }*/

        LOG(WB, "Size of vtxe set: %d\n", vtxe.size());
        size_t i = 0;
        size_t start_delta_set =
            (pblocktemplate->delta_block != nullptr ? pblocktemplate->delta_block->numTransactions() - 1 : 0);
        LOG(WB, "Start of delta set in delta block: %d\n", start_delta_set);

        std::vector<CTransactionRef> add_to_delta;

        for (const auto &txe : vtxe)
        {
            const auto &txref = txe->GetSharedTx();
            // LOG(WB, "Adding %s, isCoinBase: %d\n", txref->GetHash().GetHex(), txref->IsCoinBase());
            // pblock->add(txref);
            if (pblocktemplate->delta_block != nullptr && i >= start_delta_set)
                add_to_delta.emplace_back(txref);
            pblocktemplate->vTxFees.push_back(txe->GetFee());
            pblocktemplate->vTxSigOps.push_back(txe->GetSigOpCount());
            i++;
            // COZ_PROGRESS_NAMED("add to regular from vector");
        }
        LOG(WB, "Adding: Added vtxe.\n");
        if (pblocktemplate->delta_block != nullptr)
        {
            std::random_shuffle(add_to_delta.begin(), add_to_delta.end());
            for (auto txref : add_to_delta)
            {
                pblocktemplate->delta_block->add(txref);
                // COZ_PROGRESS_NAMED("add to delta from vector");
            }
        }
        LOG(WB, "Adding: Added to delta block.\n");

        // if (canonical)
        // pblock->sortLTOR(true);
        // LOG(WB, "Sorted LTOR.\n");

        // Create coinbase transaction.
        // LOG(WB, "Block num txn before adding CB: %d\n", pblock->numTransactions());

        CTransactionRef final_cb =
            coinbaseTx(scriptPubKeyIn, nHeight, nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus()));
        // pblock->setCoinbase(final_cb);
        // LOG(WB, "Block num txn after adding CB (%s): %d\n", pblock->coinbase()->GetHash().GetHex(),
        // pblock->numTransactions());
        if (pblocktemplate->delta_block != nullptr)
        {
            LOG(WB, "Delta block num txn before adding CB: %d\n", pblocktemplate->delta_block->numTransactions());
            pblocktemplate->delta_block->setCoinbase(final_cb);
            LOG(WB, "Delta block num txn after adding CB: %d\n", pblocktemplate->delta_block->numTransactions());
            LOG(WB, "Delta block delta set size: %d\n", pblocktemplate->delta_block->deltaSet().size());
        }
        pblocktemplate->vTxFees[0] = -nFees;

        // Fill in header
        pblock->hashPrevBlock = pindexPrev->GetBlockHash();
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
        pblock->nNonce = 0;
        if (pblocktemplate->delta_block != nullptr)
        {
            pblocktemplate->delta_block->hashPrevBlock = pindexPrev->GetBlockHash();
            UpdateTime(pblocktemplate->delta_block.get(), chainparams.GetConsensus(), pindexPrev);
            pblocktemplate->delta_block->nBits = pblock->nBits;
            pblocktemplate->delta_block->nNonce = pblock->nNonce;
            pblocktemplate->delta_block->setAllTransactionsKnown();

            // sanity check: both blocks should have the same merkle root
            // uint256 regular_hashMerkleRoot = BlockMerkleRoot(*pblock);
            // uint256 delta_hashMerkleRoot = BlockMerkleRoot(*pblocktemplate->delta_block);

            /*
            for (auto txref : *pblock)
                LOG(WB, "Regular block txhash: %s\n", txref->GetHash().GetHex());

            for (auto txref : *pblocktemplate->delta_block)
                LOG(WB, "Delta block txhash: %s\n", txref->GetHash().GetHex());
            */
            // LOG(WB, "Miner constructed regular block max depth: %d, for size: %d\n", pblock->treeMaxDepth(),
            // pblock->numTransactions());
            LOG(WB, "Miner constructed delta block max depth: %d, for size: %d\n",
                pblocktemplate->delta_block->treeMaxDepth(), pblocktemplate->delta_block->numTransactions());

            // LOG(WB, "Delta block merkle root: %s, regular merkle root: %s\n", delta_hashMerkleRoot.GetHex(),
            // regular_hashMerkleRoot.GetHex());
            // DbgAssert(delta_hashMerkleRoot == regular_hashMerkleRoot, pblocktemplate->delta_block = nullptr;);
        }
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(final_cb, STANDARD_SCRIPT_VERIFY_FLAGS);
    }

    // All the transactions in this block are from the mempool and therefore we can use XVal to speed
    // up the testing of the block validity. Set XVal flag for new blocks to true unless otherwise
    // configured.
    pblock->fXVal = GetBoolArg("-xval", true);

    // COZ_PROGRESS_NAMED("whole block");
    CValidationState state;

    CBlock *testblock = pblock;
    if (pblocktemplate->delta_block != nullptr)
    {
        pblocktemplate->delta_block->fXVal = true;
        testblock = pblocktemplate->delta_block.get();
    }

    if (blockstreamCoreCompatible)
    {
        if (!TestConservativeBlockValidity(state, chainparams, *testblock, pindexPrev, false, false))
        {
            throw std::runtime_error(
                strprintf("%s: TestConservativeBlockValidity failed: %s", __func__, FormatStateMessage(state)));
        }
    }
    else
    {
        if (!TestBlockValidity(state, chainparams, *testblock, pindexPrev, false, false))
        {
            throw std::runtime_error(
                strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
        }
    }
    if (pblock->fExcessive)
    {
        throw std::runtime_error(strprintf("%s: Excessive block generated: %s", __func__, FormatStateMessage(state)));
    }
    // COZ_PROGRESS;
    return pblocktemplate;
}

bool BlockAssembler::isStillDependent(CTxMemPool::txiter iter)
{
    for (CTxMemPool::txiter parent : mempool.GetMemPoolParents(iter))
    {
        if (!inBlock.count(parent->GetTx().GetHash()))
        {
            return true;
        }
    }
    return false;
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

    // Enforce the "old" sigops for <= 1MB blocks
    if (nBlockSize + nExtraSize <= BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
    {
        // BU: be conservative about what is generated
        if (nBlockSigOps + nExtraSigOps >= BLOCKSTREAM_CORE_MAX_BLOCK_SIGOPS)
        {
            // BU: so a block that is near the sigops limit might be shorter than it could be if
            // the high sigops tx was backed out and other tx added.
            if (nBlockSigOps > BLOCKSTREAM_CORE_MAX_BLOCK_SIGOPS - 2)
                blockFinished = true;
            return false;
        }
    }
    else
    {
        uint64_t blockMbSize = 1 + (nBlockSize + nExtraSize - 1) / 1000000;
        if (nBlockSigOps + nExtraSigOps > blockMiningSigopsPerMb.Value() * blockMbSize)
        {
            if (nBlockSigOps > blockMiningSigopsPerMb.Value() * blockMbSize - 2)
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

    int64_t micros_now = GetTimeMicros();
    int64_t micros_tx = iter->GetTimeMicros();
    if (micros_tx + 1000000 > micros_now)
    {
        /*LOG(WB, "Ignoring transaction %s as it is too recent (%d us).\n", iter->GetSharedTx()->GetHash().GetHex(),
          micros_now - micros_tx);*/
        return false;
    }
    // Last but not least, check that it is not a known doublespend to help working on a a single
    // delta blocks chain...
    /*! FIXME: Notice that this probably needs to be changed for a
      production variant of deltablocks to have low no false positives
      (asymptotically none so txn don't get stuck forever) and also
      low false negatives. Currently the respend stuff has up to 0.01 FP rate...*/
    {
        const CTransactionRef &tx = iter->GetSharedTx();
        for (auto inp : tx->vin)
        {
            if (respend::RespendDetector::likelyKnownRespent(inp.prevout))
            {
                /*LOG(WB, "Transaction %s is likely doublespend, disregarding for block build.\n",
                  tx->GetHash().GetHex());*/
                return false;
            }
            if (best_delta_template->spendsOutput(inp.prevout))
            {
                /*LOG(WB, "Transaction %s would be double-spending one included in delta block template, dropping.\n",
                  tx->GetHash().GetHex());*/
                return false;
            }
        }
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
    inBlock.insert(iter->GetTx().GetHash());

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
    // COZ_PROGRESS_NAMED("AddToBlock1");
}

void BlockAssembler::AddToBlock(std::vector<const CTxMemPoolEntry *> *vtxe, CTxMemPoolEntry *entry)
{
    vtxe->push_back(entry);
    nBlockSize += entry->GetTxSize();
    ++nBlockTx;
    nBlockSigOps += entry->GetSigOpCount();
    nFees += entry->GetFee();
    inBlock.insert(entry->GetTx().GetHash());
    // COZ_PROGRESS_NAMED("AddToBlock2");
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

        // If tx already in block, skip  (added by addPriorityTxs)
        if (inBlock.count(iter->GetTx().GetHash()))
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
        if (inBlock.count(iter->GetTx().GetHash()))
        {
            // DbgAssert(false, ); // can happen for prio tx if delta block
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
    CMutableTransaction txCoinbase(*pblock->coinbase());

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

    pblock->setCoinbase(MakeTransactionRef(std::move(txCoinbase)));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}
