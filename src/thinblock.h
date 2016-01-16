// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THINBLOCK_H
#define BITCOIN_THINBLOCK_H

#include "serialize.h"
#include "uint256.h"
#include "primitives/block.h"
#include "bloom.h"

#include <vector>

class CThinBlock
{
public:
    /** Public only for unit testing */
    CBlockHeader header;
    std::vector<uint256> vTxHashes; // List of all transactions id's in the block
    std::map<uint256, CTransaction> mapMissingTx; // map of transactions that did not match the bloom filter

public:

    /**
     * Create from a CThinBlock, finding missing transactions according to filter
     */
    CThinBlock(const CBlock& block, CBloomFilter& filter);

    CThinBlock() {}

    int64_t GetBlockTime() { return header.GetBlockTime(); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(header);
        READWRITE(vTxHashes);
        READWRITE(mapMissingTx);

    }
};

// This class is used for retrieving a list of still missing transactions after receiving a "thinblock" message.
// The CThinBlockTx when recieved can be used to fill in the missing transactions after which it is sent
// back to the requestor.
class CThinBlockTx
{
public:
    /** Public only for unit testing */
    uint256 blockhash;
    std::map<uint256, CTransaction> mapTx; // map of missing transactions

public:

    /**
     * Create from a CThinBlockTx, finding missing transactions
     */
    CThinBlockTx(uint256 blockHash, std::vector<uint256>& vHashesToRequest);

    CThinBlockTx() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(blockhash);
        READWRITE(mapTx);
    }
};
#endif // BITCOIN_THINBLOCK_H
