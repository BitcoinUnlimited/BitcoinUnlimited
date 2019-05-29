// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EXPEDITED_H
#define BITCOIN_EXPEDITED_H

#include "blockrelay/thinblock.h"

enum
{
    EXPEDITED_STOP = 1,
    EXPEDITED_BLOCKS = 2,
    EXPEDITED_TXNS = 4,
};

enum
{
    EXPEDITED_MSG_HDR = 1,
    EXPEDITED_MSG_XTHIN = 2,
};


// Checks to see if the node is configured in bitcoin.conf to
extern bool CheckAndRequestExpeditedBlocks(CNode *pfrom);

// be an expedited block source and if so, request them.
extern void SendExpeditedBlock(CXThinBlock &thinBlock, unsigned char hops, CNode *pskip = nullptr);
extern void SendExpeditedBlock(const CBlock &block, CNode *pskip = nullptr);
extern bool HandleExpeditedRequest(CDataStream &vRecv, CNode *pfrom);

// process incoming unsolicited block
extern bool HandleExpeditedBlock(CDataStream &vRecv, CNode *pfrom);

#endif
