// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockdelta.h"

bool CBlockDelta::_SpendCoin(const COutPoint &outpoint, Coin *moveout)
{
    auto iter = blockOutputs.find(outpoint);
    if (iter != blockOutputs.end())
    {
        // move the coin then erase the entry in the set
        *moveout = std::move(iter->second);
        blockOutputs.erase(outpoint);
        return true;
    }
    return false;
}

bool CBlockDelta::AddOutputsToDelta(const CTransaction &tx, int nHeight)
{
    bool fCoinbase = tx.IsCoinBase();
    const uint256 &txid = tx.GetHash();
    // if we have an entry for this tx already, finding it again indicates a duplicate tx so we assert
    for (size_t i = 0; i < tx.vout.size(); ++i)
    {
        if (blockOutputs.emplace(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase)).second == false)
        {
            return false;
        }
    }
    return true;
}

void CBlockDelta::SpendCoins(const CTransaction &tx, CCoinsViewCache &utxo, CTxUndo &txundo)
{
    // mark inputs spent
    if (!tx.IsCoinBase())
    {
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn &txin : tx.vin)
        {
            txundo.vprevout.emplace_back();
            if (_SpendCoin(txin.prevout, &txundo.vprevout.back()) == false)
            {
                utxo.SpendCoin(txin.prevout, &txundo.vprevout.back());
            }
        }
    }
}

void CBlockDelta::AddNewOutputsToView(CCoinsViewCache &cache)
{
    for (auto &output : blockOutputs)
    {
        // printf("ADD COIN HERE \n");
        cache.AddCoin(output.first, std::move(output.second), output.second.fCoinBase);\
        // printf("ADD COIN DONE \n");
    }
}

bool CBlockDelta::GetCoin(const COutPoint &outpoint, Coin &coin) const
{
    auto it = blockOutputs.find(outpoint);
    if (it != blockOutputs.end())
    {
        coin = it->second;
        return true;
    }
    return false;
}
