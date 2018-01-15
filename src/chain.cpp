// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"

using namespace std;

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex)
{
    if (pindex == NULL)
    {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex)
    {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const
{
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex)
    {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex))
        {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        }
        else
        {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const
{
    if (pindex == NULL)
    {
        return NULL;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }
/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height)
{
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

CBlockIndex *CBlockIndex::GetAncestor(int height)
{
    if (height > nHeight || height < 0)
        return NULL;

    CBlockIndex *pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height)
    {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != NULL &&
            (heightSkip == height ||
                (heightSkip > height && !(heightSkipPrev < heightSkip - 2 && heightSkipPrev >= height))))
        {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        }
        else
        {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

const CBlockIndex *CBlockIndex::GetAncestor(int height) const
{
    return const_cast<CBlockIndex *>(this)->GetAncestor(height);
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

// According to UAHF spec there are 2 pre-conditions to get the fork activated:
//
// 1) First step is waiting for the first block to have GetMedianTimePast() (GMTP)
// be higher or equal then 1501590000 (Aug 1st 2017, 12:20:00 UTC). This block would
// be the last in common with the other branch to the fork.
// Let's call this block x-1. The match of this conditions will be called "Fork Enabled"
//
// 2) x-1 could be extended only by a block bigger than 1MB, so that size(block x) > 1MB
// The match of this conditions will be called "Fork Activated"

// return true for every block from fork block and forward [x,+inf)
// fork activated
bool CBlockIndex::forkActivated(int time)
{
    if (time == 0)
        return false;

    if (pprev && pprev->GetMedianTimePast() >= time)
    {
        return true;
    }
    return false;
}

// return true only if we are exactly on the fork block [x,x]
// state: fork activated
bool CBlockIndex::forkActivateNow(int time)
{
    if (time == 0)
        return false;
    return (pprev && pprev->forkAtNextBlock(time));
}

// This will check if the Fork will be enabled at the next block
// i.e. we are at block x - 1, [x-1, +inf]
// state fork: enabled or activated
bool CBlockIndex::IsforkActiveOnNextBlock(int time)
{
    if (time == 0)
        return false;
    // if the fork is already activated
    if (forkActivated(time))
        return true;
    if (GetMedianTimePast() >= time)
        return true;
    return false;
}

// return true only if 1st condition is true (Median past time > UAHF time)
// and not the 2nd, i.e. we are at precisely [x-1,x-1]
// state: fork enabled
bool CBlockIndex::forkAtNextBlock(int time)
{
    if (time == 0)
        return false;

    // if the fork is already activated
    if (forkActivated(time))
        return false;

    if (GetMedianTimePast() >= time)
        return true;
    return false;
}

std::string CBlockFileInfo::ToString() const
{
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst,
        nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}
