// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Ordering of vector<CTransactionRef>s (block contents) with different
 * algorithms. Checks for orders.
 */
#ifndef BITCOIN_BLOCKORDER_H
#define BITCOIN_BLOCKORDER_H

#include "main.h"
#include "primitives/transaction.h"
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

typedef std::vector<CTransactionRef> CTxRefVector;

namespace BlockOrder
{
class TXIDCompare
{
public:
    inline bool operator()(const CTransaction *a, const CTransaction *b) { return a->GetHash() < b->GetHash(); }
    inline bool operator()(const CTransactionRef &a, const CTransactionRef &b) { return operator()(a.get(), b.get()); }
};

class Lexical
{
public:
    inline void prepare(const CTxRefVector &txrfv) {}
    /*! Given a CTxRefVector, sort it in-place lexicographically by TXID.
      For blocks of size n, the complexity of this should be O(n log n). */
    void sort(CTxRefVector &txrfv);
};

class TopoCanonical
{
public:
    void prepare(const CTxRefVector &txrfv);

    /*! Given a CTxRefVector, sort it partial-canonical "TopoCanonical". This is a variant / adaptation
      of Gavin Andresen's canonical sorting algorithm, as described here:

      Sorting by this order means that the block order will still be valid in the sense
      of block ordering rules on the BCH network as of August 2018, that is, following
      the topological block sorting order.

      It will, however, also be unique and can thus be transmitted in a TBD update of the
      Graphene protocol in an efficient manner that does not need to transmit the block order
      (just the ordering algorithm used).

      The complexity of this algorithm, for blocks of size n and a fraction f of transactions that are pointing to each
      other should be as follows (though this needs to be reviewed in a more detailed analysis. Minor points are easy to
      get
      wrong):
      O( (1-f)n log ((1-f)n ) + fn)

      This assumes that txrf is a poset!
    */
    void sort(CTxRefVector &txrfv);

private:
    std::unordered_map<const CTransaction *, CTransactionRef> ptr2ref;
    std::unordered_map<uint256, const CTransaction *, BlockHasher> txn_map;
    void fillIncoming(CTxRefVector &txrfv, std::unordered_map<const CTransaction *, int> &incoming);
    unsigned fillTodo(const CTxRefVector &txrfv,
        const std::unordered_map<const CTransaction *, int> &incoming,
        std::vector<const CTransaction *> &todo);
    void applyKahns(CTxRefVector &txrfv,
        std::unordered_map<const CTransaction *, int> incoming,
        std::vector<const CTransaction *> &todo,
        unsigned n_todo);
};


//! Returns true if vector txrfv is ordered with dependent transactions coming later
bool isTopological(const CTxRefVector &txrfv);
}
#endif // BITCOIN_BLOCKORDER_H
