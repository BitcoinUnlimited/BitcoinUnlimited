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

// Return true if this transaction can only be committed post-fork
bool IsTxUAHFOnly(const CTxMemPoolEntry &tx);

// It is not possible to provably determine whether an arbitrary script signs using the old or new sighash type
// without executing the previous output and input scripts.  But we can make a good guess by assuming that
// these are standard scripts.
bool IsTxProbablyNewSigHash(const CTransaction &tx);

// was the fork activated on this or any prior block?
bool UAHFforkActivated(int height);

// Is the next block the fork block?
bool UAHFforkAtNextBlock(int height);

// Is the fork active on the next block?
bool IsUAHFforkActiveOnNextBlock(int height);

/** Check is Cash HF has activated. */
bool IsDAAEnabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

bool IsNov152018Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

/** Test if this node is configured to follow the Nov 15 fork (consensus.forkNov2018Time is nonzero),
    or whether the operator is enabling/disabling features manually. */
bool IsNov152018Scheduled();

/** Test if fork is active */
bool IsNov152018Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);


/** Test if this node is configured to follow the Bitcoin SV defined hard fork */
bool IsSv2018Scheduled();

/** Test if SV fork is active */
bool IsSv2018Enabled(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

/** Test if SV fork is happening on the next block */
bool IsSv2018Next(const Consensus::Params &consensusparams, const CBlockIndex *pindexTip);

#endif
