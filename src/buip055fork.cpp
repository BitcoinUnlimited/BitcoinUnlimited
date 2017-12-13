// Copyright (c) 2017 The Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "buip055fork.h"
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

bool UpdateBUIP055Globals(CBlockIndex *activeTip)
{
    if (activeTip)
    {
        if ((miningForkTime.value != 0) && activeTip->forkAtNextBlock(miningForkTime.value))
        {
            excessiveBlockSize = miningForkEB.value;
            maxGeneratedBlock = miningForkMG.value;
            return true;
        }
    }
    return false;
}

bool ValidateBUIP055Block(const CBlock &block, CValidationState &state, int nHeight)
{
    // Validate transactions are HF compatible
    for (const CTransaction &tx : block.vtx)
    {
        int sunsetHeight =
            (Params().NetworkIDString() == "testnet") ? TESTNET_REQ_6_1_SUNSET_HEIGHT : REQ_6_1_SUNSET_HEIGHT;
        if ((nHeight <= sunsetHeight) && IsTxOpReturnInvalid(tx))
            return state.DoS(
                100, error("transaction is invalid on BUIP055 chain"), REJECT_INVALID, "bad-txns-wrong-fork");
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

bool IsTxBUIP055Only(const CTxMemPoolEntry &txentry)
{
    if (txentry.sighashType & SIGHASH_FORKID)
    {
        // LogPrintf("txn is BUIP055-specific\n");
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
