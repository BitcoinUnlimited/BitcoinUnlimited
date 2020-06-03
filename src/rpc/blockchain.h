// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_BLOCKCHAIN_H
#define BITCOIN_RPC_BLOCKCHAIN_H

#include "chain.h"

#include <amount.h>
#include <stdint.h>
#include <vector>

class CBlock;
class CBlockIndex;
class UniValue;

static constexpr int NUM_GETBLOCKSTATS_PERCENTILES = 5;

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex *a, const CBlockIndex *b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
            return (a->nHeight > b->nHeight);

        return a < b;
    }
};

/** Used by getblockstats to get feerates at different percentiles by size  */
void CalculatePercentilesBySize(CAmount result[NUM_GETBLOCKSTATS_PERCENTILES],
    std::vector<std::pair<CAmount, int64_t> > &scores,
    int64_t total_size);

/** Roll the chain back to the given height.  If the override flag is set to true
 *  then you can rollback more than 100 blocks.
 */
std::string RollBackChain(int nRollBackHeight, bool fOverride);

/** Check if we are on the most work chain and if not then re-org to it */
std::string ReconsiderMostWorkChain(bool fOverride);
std::set<CBlockIndex *, CompareBlocksByHeight> GetChainTips();

UniValue mempoolToJSON(bool fVerbose = false);
UniValue blockToJSON(const CBlock &block, const CBlockIndex *blockindex, bool txDetails = false, bool listTxns = true);

#endif
