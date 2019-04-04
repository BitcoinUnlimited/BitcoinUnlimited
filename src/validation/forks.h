// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FORKS_H
#define BITCOIN_FORKS_H

#include "amount.h"
#include "arith_uint256.h"
#include "chain.h"
#include "consensus/params.h"
#include "tweak.h"
#include "univalue/include/univalue.h"

#include <vector>

class CValidationState;
class CBlock;
class CTransaction;
class CBlockIndex;
class CScript;
class CTxMemPoolEntry;

extern CChain chainActive;

// Return true if this transaction can only be committed post-fork
bool IsTxUAHFOnly(const CTxMemPoolEntry &tx);

// It is not possible to provably determine whether an arbitrary script signs using the old or new sighash type
// without executing the previous output and input scripts.  But we can make a good guess by assuming that
// these are standard scripts.
bool IsTxProbablyNewSigHash(const CTransaction &tx);

// was the fork activated on this or any prior block?
bool UAHFforkActivated(int height);

// Is the fork active on the next block?
bool IsUAHFforkActiveOnNextBlock(int height);

/** Check is Cash HF has activated. */
bool IsDAAEnabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

/** Test if this node is configured to follow the BCH chain (miningForkTime tweak is nonzero),
    or whether the operator is enabling/disabling features manually. */
bool AreWeOnBCHChain();

/** Check if Nov 15th, 2018 protocol upgrade is activated using block height */
bool IsNov152018Activated(const Consensus::Params &consensusparams, const int32_t nHeight);
bool IsNov152018Activated(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

/** Check if the next will be the first block where the new set of rules will be enforced */
bool IsMay152019Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

/** Test if May 15th 2019 fork has actived */
bool IsMay152019Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

/** Test if this node is configured to follow the Bitcoin SV defined hard fork */
bool AreWeOnSVChain();
/** Check if SV Nov 15th, 2018 protocol upgrade is activated using block height */
bool IsSv2018Activated(const Consensus::Params &consensusparams, const int32_t nHeight);
bool IsSv2018Activated(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);
#endif
