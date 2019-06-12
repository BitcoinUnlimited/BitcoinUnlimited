// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Copyright (c) 2016 Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "verifydb.h"

#include "blockstorage/blockstorage.h"
#include "init.h"
#include "ui_interface.h"
#include "validation.h"

CVerifyDB::CVerifyDB() { uiInterface.ShowProgress(_("Verifying blocks..."), 0); }
CVerifyDB::~CVerifyDB() { uiInterface.ShowProgress("", 100); }
bool CVerifyDB::VerifyDB(const CChainParams &chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == nullptr || chainActive.Tip()->pprev == nullptr)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0 || nCheckDepth > chainActive.Height())
    {
        nCheckDepth = chainActive.Height();
    }
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LOGA("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex *pindexState = chainActive.Tip();
    CBlockIndex *pindexFailure = nullptr;
    int nGoodTransactions = 0;
    CValidationState state;
    for (CBlockIndex *pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        if (shutdown_threads.load() == true)
        {
            return false;
        }
        uiInterface.ShowProgress(_("Verifying blocks..."),
            std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth *
                                           (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainActive.Height() - nCheckDepth)
            break;
        {
            READLOCK(cs_mapBlockIndex); // for nStatus
            if (fPruneMode && !(pindex->nStatus & BLOCK_HAVE_DATA))
            {
                // If pruning, only go back as far as we have data.
                LOGA("VerifyDB(): block verification stopping at height %d (pruning, no data)\n", pindex->nHeight);
                break;
            }
        }
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                pindex->GetBlockHash().ToString());
        nBlockSizeAtChainTip.store(block.GetBlockSize());

        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state))
            return error(
                "VerifyDB(): *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex)
        {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull())
            {
                if (!ReadUndoFromDisk(undo, pos, pindex->pprev))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight,
                        pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState &&
            (int64_t)(coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheMaxSize)
        {
            DisconnectResult res = DisconnectBlock(block, pindex, coins);
            if (res == DISCONNECT_FAILED)
            {
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s",
                    pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->pprev;
            if (res == DISCONNECT_UNCLEAN)
            {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            }
            else
                nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error(
            "VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n",
            chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4)
    {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip())
        {
            if (shutdown_threads.load() == true)
            {
                return false;
            }
            uiInterface.ShowProgress(_("Verifying blocks..."),
                std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) /
                                                     (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                    pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins, chainparams))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight,
                    pindex->GetBlockHash().ToString());
        }
    }

    LOGA("No coin database inconsistencies in last %i blocks (%i transactions)\n",
        chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}
