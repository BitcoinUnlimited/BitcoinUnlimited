// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BOBTAIL_SUBBLOCK_H
#define BITCOIN_BOBTAIL_SUBBLOCK_H

#include "primitives/block.h"

class CSubBlock;
typedef std::shared_ptr<CSubBlock> CSubBlockRef;

class CSubBlock : public CBlockHeader
{
public:
    std::vector<CTransactionRef> vtx;
    bool fXVal;

    CSubBlock() { SetNull(); }

    CSubBlock(const CBlockHeader &header)
    {
        SetNull();
        *((CBlockHeader *)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(*(CBlockHeader *)this);
        READWRITE(vtx);
    }

    void SetNull();

    bool IsNull() const;

    CBlockHeader GetBlockHeader() const;

    std::string ToString() const;

    std::set<uint256> GetAncestorHashes() const;

    std::vector<uint256> GetTxHashes() const;
};


#endif
