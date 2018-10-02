// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "forks.h"
#include "unlimited.h"

bool IsDAAEnabled(const Consensus::Params &consensusparams, int nHeight)
{
    return nHeight >= consensusparams.daaHeight;
}

bool IsDAAEnabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev)
{
    if (pindexPrev == nullptr)
    {
        return false;
    }

    return IsDAAEnabled(consensusparams, pindexPrev->nHeight);
}

bool IsNov152018Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev)
{
    if (pindexPrev == nullptr)
    {
        return false;
    }

    return pindexPrev->IsforkActiveOnNextBlock(miningForkTime.Value());
}

bool IsNov152018Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexPrev)
{
    if (pindexPrev == nullptr)
    {
        return false;
    }

    return pindexPrev->forkAtNextBlock(miningForkTime.Value());
}
