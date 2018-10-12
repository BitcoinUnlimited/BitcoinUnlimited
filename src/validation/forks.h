// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FORKS_H
#define BITCOIN_FORKS_H

#include "chain.h"
#include "consensus/params.h"

/** Check is Cash HF has activated. */
bool IsDAAEnabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

bool IsNov152018Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);


/** Test if this node is configured to follow the Nov 15 fork (consensus.forkNov2018Time is nonzero),
    or whether the operator is enabling/disabling features manually. */
bool IsNov152018Scheduled();

/** Test if fork is active */
bool IsNov152018Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

#endif
