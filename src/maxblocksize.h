// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAXBLOCKSIZE_H
#define BITCOIN_MAXBLOCKSIZE_H

#include "consensus/consensus.h"
#include "consensus/params.h"
#include "script/script.h"

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;

uint64_t GetNextMaxBlockSize(const CBlockIndex *pindexLast, const Consensus::Params &);

uint64_t GetMaxBlockSizeVote(const CScript &coinbase, int32_t nHeight);

#endif // BITCOIN_MAXBLOCKSIZE_H
