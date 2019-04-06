// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "forks.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "script/interpreter.h"
#include "txmempool.h"
#include "unlimited.h"

#include <inttypes.h>
#include <vector>

bool IsTxProbablyNewSigHash(const CTransaction &tx)
{
    bool oldsighash = false;
    for (auto txin : tx.vin)
    {
        std::vector<unsigned char> data;
        CScript::const_iterator pc(txin.scriptSig.begin());
        opcodetype op;
        if (txin.scriptSig.GetOp(pc, op, data))
        {
            if (!data.empty())
            {
                if (!(data.back() & SIGHASH_FORKID))
                {
                    oldsighash = true;
                }
            }
        }
    }
    return (oldsighash == false);
}

bool IsTxUAHFOnly(const CTxMemPoolEntry &txentry)
{
    if ((txentry.sighashType & SIGHASH_FORKID) || (txentry.sighashType == 0))
    {
        return true;
    }
    return false;
}

// return true for every block from fork block and forward [consensusParams.uahfHeight,+inf)
bool UAHFforkActivated(int height)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();
    if (height >= consensusParams.uahfHeight)
    {
        return true;
    }
    return false;
}

// This will check if the Fork will be enabled at the next block
// i.e. we are at block x - 1, [consensusParams.uahfHeight-1, +inf]
// state fork: enabled or activated
bool IsUAHFforkActiveOnNextBlock(int height)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();
    if (height >= (consensusParams.uahfHeight - 1))
        return true;
    return false;
}

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

bool AreWeOnBCHChain() { return miningForkTime.Value() != 0; }
bool IsNov152018Activated(const Consensus::Params &consensusparams, const int32_t nHeight)
{
    return nHeight >= consensusparams.nov2018Height;
}

bool IsNov152018Activated(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip)
{
    if (pindexTip == nullptr)
    {
        return false;
    }
    return IsNov152018Activated(consensusparams, pindexTip->nHeight);
}

bool IsMay152019Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip)
{
    if (pindexTip == nullptr)
    {
        return false;
    }
    return pindexTip->IsforkActiveOnNextBlock(miningForkTime.Value());
}

bool IsMay152019Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip)
{
    if (pindexTip == nullptr)
    {
        return false;
    }
    return pindexTip->forkAtNextBlock(miningForkTime.Value());
}

//* SV helpers/

bool AreWeOnSVChain() { return miningSvForkTime.Value() != 0; }
bool IsSv2018Activated(const Consensus::Params &consensusparams, const int32_t nHeight)
{
    return nHeight >= consensusparams.sv2018Height;
}

bool IsSv2018Activated(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip)
{
    if (pindexTip == nullptr)
    {
        return false;
    }
    return IsSv2018Activated(consensusparams, pindexTip->nHeight);
}
