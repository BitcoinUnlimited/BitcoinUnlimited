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

const int REQ_6_1_SUNSET_HEIGHT = 530000;
const int TESTNET_REQ_6_1_SUNSET_HEIGHT = 1250000;

static const std::string ANTI_REPLAY_MAGIC_VALUE = "Bitcoin: A Peer-to-Peer Electronic Cash System";

std::vector<unsigned char> invalidOpReturn =
    std::vector<unsigned char>(std::begin(ANTI_REPLAY_MAGIC_VALUE), std::end(ANTI_REPLAY_MAGIC_VALUE));

bool ValidateUAHFBlock(const CBlock &block, CValidationState &state, int nHeight)
{
    // Validate transactions are HF compatible
    for (const CTransaction &tx : block.vtx)
    {
        int sunsetHeight =
            (Params().NetworkIDString() == "testnet") ? TESTNET_REQ_6_1_SUNSET_HEIGHT : REQ_6_1_SUNSET_HEIGHT;
        if ((nHeight <= sunsetHeight) && IsTxOpReturnInvalid(tx))
            return state.DoS(
                100, error("transaction is invalid on UAHF cash chain"), REJECT_INVALID, "bad-txns-wrong-fork");
    }
    return true;
}

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
    if (txentry.sighashType & SIGHASH_FORKID)
    {
        // LogPrintf("txn is UAHF-specific\n");
        return true;
    }
    return false;
}

bool IsTxOpReturnInvalid(const CTransaction &tx)
{
    for (auto txout : tx.vout)
    {
        int idx = txout.scriptPubKey.Find(OP_RETURN);
        if (idx)
        {
            CScript::const_iterator pc(txout.scriptPubKey.begin());
            opcodetype op;
#if 0 // Allow OP_RETURN anywhere
            for (;pc != txout.scriptPubKey.end();)
            {
                if (txout.scriptPubKey.GetOp(pc, op))
                {
                    if (op == OP_RETURN) break;
                }
            }
#else // OP_RETURN must be the first instruction
            if (txout.scriptPubKey.GetOp(pc, op))
            {
                if (op != OP_RETURN)
                    return false;
            }
#endif
            if (pc != txout.scriptPubKey.end())
            {
                std::vector<unsigned char> data;
                if (txout.scriptPubKey.GetOp(pc, op, data))
                {
                    // Note this code only works if the size <= 75 (or we'd have OP_PUSHDATAn instead)
                    if (op == invalidOpReturn.size())
                    {
                        if (data == invalidOpReturn)
                            return true;
                    }
                }
            }
        }
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
