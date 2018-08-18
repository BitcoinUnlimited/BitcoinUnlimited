// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockorder.h"
#include <algorithm>
#include <list>

class TXIDCompareReverse
{
public:
    inline bool operator()(const CTransaction *b, const CTransaction *a) { return a->GetHash() < b->GetHash(); }
    inline bool operator()(const CTransactionRef &a, const CTransactionRef &b) { return operator()(a.get(), b.get()); }
};


class MinInputTXIDCompare
{
    inline std::pair<uint256, uint32_t> min_input(const CTransaction *r)
    {
        uint256 min_hash;
        uint32_t min_idx = 0xffffffff;

        // note: logic should also work for coinbase as it only has a *single* input of zero
        for (auto &input : r->vin)
        {
            if (min_hash.IsNull() || input.prevout.hash < min_hash)
            {
                min_hash = input.prevout.hash;
                min_idx = input.prevout.n;
            }
            else if (input.prevout.hash == min_hash)
            {
                min_idx = std::min(input.prevout.n, min_idx);
            }
        }
        return std::pair<uint256, uint32_t>(min_hash, min_idx);
    }

public:
    inline bool operator()(const CTransaction *b, const CTransaction *a)
    {
        std::pair<uint256, uint32_t> min_a = min_input(a);
        std::pair<uint256, uint32_t> min_b = min_input(b);
        return min_a < min_b;
    }
};


// FIXME: deal with coinbase explictly
void BlockOrder::Lexical::sort(CTxRefVector &txrfv) { std::sort(txrfv.begin() + 1, txrfv.end(), TXIDCompare()); }
void BlockOrder::TopoCanonical::prepare(const CTxRefVector &txrfv)
{
    ptr2ref.reserve(txrfv.size() * 2);
    txn_map.reserve(txrfv.size() * 2);
    // build pointer map
    for (auto &txr : txrfv)
    {
        ptr2ref[txr.get()] = txr;
        txn_map[txr->GetHash()] = txr.get();
    }
}

inline void BlockOrder::TopoCanonical::fillIncoming(CTxRefVector &txrfv,
    std::unordered_map<const CTransaction *, int> &incoming)
{
    for (auto &txr : txrfv)
    {
        for (auto &input : txr->vin)
            if (txn_map.count(input.prevout.hash))
            {
                const CTransaction *input_tx = txn_map[input.prevout.hash];
                incoming[input_tx]++;
            }
        if (txr->IsCoinBase())
            std::swap(txrfv[0], txr);
    }
}

inline unsigned BlockOrder::TopoCanonical::fillTodo(const CTxRefVector &txrfv,
    const std::unordered_map<const CTransaction *, int> &incoming,
    std::vector<const CTransaction *> &todo)
{
    unsigned n_todo = 0;

    for (auto &txr : txrfv)
    {
        const CTransaction *tx = txr.get();
        if (!incoming.count(tx))
            todo[n_todo++] = tx;
    }
    return n_todo;
}

inline void BlockOrder::TopoCanonical::applyKahns(CTxRefVector &txrfv,
    std::unordered_map<const CTransaction *, int> incoming,
    std::vector<const CTransaction *> &todo,
    unsigned n_todo)
{
    const int N = txrfv.size();
    // and apply Kahn's algorithm to build final sorted vector: O(fn)
    int i = N - 1;
    int j = 1;
    while (j < N)
    {
        const CTransaction *tx = todo[j++];
        txrfv[i--] = ptr2ref[tx];
        for (auto &input : tx->vin)
        {
            if (txn_map.count(input.prevout.hash))
            {
                auto &input_tx = txn_map[input.prevout.hash];
                incoming[input_tx]--;
                if (!incoming[input_tx])
                    todo[n_todo++] = input_tx;
            }
        }
    }
}

void BlockOrder::TopoCanonical::sort(CTxRefVector &txrfv)
{
    // build incoming counts
    std::unordered_map<const CTransaction *, int> incoming(txrfv.size() * 2);
    fillIncoming(txrfv, incoming);

    std::vector<const CTransaction *> todo(txrfv.size());
    unsigned n_todo = fillTodo(txrfv, incoming, todo);

    // sort TODO list: O( (1-f)n log ( (1-f) n)
    // note that a fixed order flows from this fixed initial sorted TODO list (the dependency order does the rest)!
    // skip coinbase
    std::sort(todo.begin() + 1, todo.begin() + n_todo, TXIDCompareReverse());
    // std::sort(todo.begin(), todo.begin() + n_todo, MinInputTXIDCompare());
    applyKahns(txrfv, incoming, todo, n_todo);
}

bool BlockOrder::isTopological(const CTxRefVector &txrfv)
{
    std::unordered_map<uint256, int, BlockHasher> txn_pos;
    for (size_t i = 0; i < txrfv.size(); i++)
    {
        uint256 hash = txrfv[i]->GetHash();
        if (txn_pos.count(hash))
            return false; // return false also on duplicates
        txn_pos[hash] = i;
    }

    for (size_t i = 0; i < txrfv.size(); i++)
    {
        for (auto input : txrfv[i]->vin)
        {
            const uint256 inp_hash = input.prevout.hash;
            if (txn_pos.count(inp_hash) && txn_pos[inp_hash] >= i)
                return false;
        }
    }
    return true;
}
