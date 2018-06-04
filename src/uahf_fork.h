// Copyright (c) 2017 The Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UAHF_FORK_H
#define UAHF_FORK_H

#include "amount.h"
#include "arith_uint256.h"
#include "tweak.h"
#include "univalue/include/univalue.h"
#include <vector>

class CValidationState;
class CBlock;
class CTransaction;
class CBlockIndex;
class CScript;
class CTxMemPoolEntry;

// OP_RETURN magic invalid value:
extern std::vector<unsigned char> invalidOpReturn;

// Validate that the block's contents adhere to the UAHF hard fork requirements.
// the requirement that the fork block is >= 1MB is not checked because we do not
// know whether this is the fork block.
extern bool ValidateUAHFBlock(const CBlock &block, CValidationState &state, int nHeight);

// Return true if this transaction is invalid on the UAHF fork due to a special OP_RETURN code
extern bool IsTxOpReturnInvalid(const CTransaction &tx);

// Return true if this transaction can only be committed post-fork
extern bool IsTxUAHFOnly(const CTxMemPoolEntry &tx);

// It is not possible to provably determine whether an arbitrary script signs using the old or new sighash type
// without executing the previous output and input scripts.  But we can make a good guess by assuming that
// these are standard scripts.
bool IsTxProbablyNewSigHash(const CTransaction &tx);

// was the fork activated on this or any prior block?
extern bool UAHFforkActivated(int height);

// Is the next block the fork block?
extern bool UAHFforkAtNextBlock(int height);

// Is the fork active on the next block?
extern bool IsUAHFforkActiveOnNextBlock(int height);

extern CTweak<bool> walletSignWithForkSig;

#endif
