// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BOBTAIL_BOBTAILBLOCK_H
#define BITCOIN_BOBTAIL_BOBTAILBLOCK_H

#include "primitives/block.h"
#include "subblock.h"

class CBobtailBlock : public CBlock
{
public:
    std::vector<CSubBlockRef> vdag;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(*(CBlock *)this);
        READWRITE(vdag);
    }

};

#endif
