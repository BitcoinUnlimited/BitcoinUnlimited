// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "thinblock.h"

CThinBlock::CThinBlock(const CBlock& block, CBloomFilter& filter)
{
    header = block.GetBlockHeader();

    vTxHashes.reserve(block.vtx.size());
    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const uint256& hash = block.vtx[i].GetHash();
        vTxHashes.push_back(hash);

        // Find the transactions that do not match the filter.
        // These are the ones we need to relay back to the requesting peer.
        // NOTE: We always add the first tx, the coinbase as it is the one
        //       most often missing.
        if (!filter.contains(hash) || i == 0)
            mapMissingTx[hash] = block.vtx[i];
    }
}
CThinBlockTx::CThinBlockTx(uint256 blockHash, std::vector<uint256>& vHashesToRequest)
{
    blockhash = blockHash;

    CTransaction tx;
    for (unsigned int i = 0; i < vHashesToRequest.size(); i++)
        mapTx[vHashesToRequest[i]] = tx;
}
