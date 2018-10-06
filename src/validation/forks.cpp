// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "forks.h"
#include "unlimited.h"

// For pindexTip use the current chainActive.Tip().

bool IsDAAEnabled(const Consensus::Params &consensusparams, int nHeight)
{
    return nHeight >= consensusparams.daaHeight;
}

bool IsDAAEnabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip)
{
    if (pindexTip == nullptr)
    {
        return false;
    }
    return IsDAAEnabled(consensusparams, pindexTip->nHeight);
}

bool IsNov152018Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip)
{
    if (pindexTip == nullptr)
    {
        return false;
    }
    return pindexTip->IsforkActiveOnNextBlock(miningForkTime.Value());
}

bool IsNov152018Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip)
{
    if (pindexTip == nullptr)
    {
        return false;
    }
    return pindexTip->forkAtNextBlock(miningForkTime.Value());
}
