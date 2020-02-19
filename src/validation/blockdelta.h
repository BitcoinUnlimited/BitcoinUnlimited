// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "undo.h"

class CBlockDelta
{
private:
    // attempts to spend a voin in blockOuputs, should only be called by SpendCoins
    bool _SpendCoin(const COutPoint &outpoint, Coin *moveout);
public:
    std::map<COutPoint, Coin> blockOutputs;

    CBlockDelta()
    {
        blockOutputs.clear();
    }
    bool AddOutputsToDelta(const CTransaction &tx, int nHeight);
    void SpendCoins(const CTransaction &tx, CCoinsViewCache &utxo, CTxUndo &txundo);
    void AddNewOutputsToView(CCoinsViewCache &cache);
    bool GetCoin(const COutPoint &outpoint, Coin &coin) const;
};
