// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BOBTAIL_BOBTAIL_H
#define BITCOIN_BOBTAIL_BOBTAIL_H

#include "arith_uint256.h"
#include "consensus/params.h"
#include "subblock.h"

bool IsSubBlockMalformed(const CSubBlock &subblock);
bool CheckBobtailPoW(CBlockHeader deltaBlock, std::vector<uint256> ancestors, const Consensus::Params &params, uint8_t k);
bool CheckBobtailPoWFromOrderedProofs(std::vector<arith_uint256> proofs, arith_uint256 target, uint8_t k);
/*! Given a strong block parent, calculates the weak block POW
  necessary to be a valid weak block.  NOTE: Dealing with making sure
  that all weak blocks that go into the weak blocks subsystem have
  correct POW is out of the scope of the deltablocks subsystem
  itself!! */
extern unsigned int weakPOWfromPOW(unsigned int nBits);

#endif
