// Copyright (c) 2017 The Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uahf_fork.h"
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
    // bool newsighash = false;
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
                if (data.back() & SIGHASH_FORKID)
                {
                    // newsighash = true;
                }
                else
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
        // LOGA("txn is UAHF-specific\n");
        return true;
    }
    return false;
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
// i.e. we are at block x - 1, [x-1, +inf]
// state fork: enabled or activated
bool IsUAHFforkActiveOnNextBlock(int height)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();
    if (height >= (consensusParams.uahfHeight - 1))
        return true;
    return false;
}

// return true only if 1st condition is true (Median past time > UAHF time)
// and not the 2nd, i.e. we are at precisely [x-1,x-1]
// state: fork enabled
bool UAHFforkAtNextBlock(int height)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();
    if (height == (consensusParams.uahfHeight - 1))
        return true;
    return false;
}
