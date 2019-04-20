#ifndef BITCOIN_DELTABLOCKS_H
#define BITCOIN_DELTABLOCKS_H

#include "primitives/transaction.h"
#include "primitives/block.h"
#include "persistent_map.h"
#include <memory>
#include <vector>

// Deltablocks / Merged weak blocks for Bitcoin

class CDeltaBlock;
typedef std::shared_ptr<CDeltaBlock> CDeltaBlockRef;
typedef std::shared_ptr<const CDeltaBlock> ConstCDeltaBlockRef;

/*! Given a strong block parent, calculates the weak block POW
  necessary to be a valid weak block.  NOTE: Dealing with making sure
  that all weak blocks that go into the weak blocks subsystem have
  correct POW is out of the scope of the deltablocks subsystem
  itself!! */
extern unsigned int weakPOWfromPOW(unsigned int nBits);

typedef persistent_map<COutPoint, uint256> CSpentMap;


class CChainParams;
class CBlockIndex;

/*! A delta block is a block with some special methods and handling to
allow for delta blocks merging and more efficient delta-blocks-mode
propagation.

It can be incomplete, in which case only its header and the coinbase
transaction are known (and thus all ancestor blocks as well), or it
can be complete - which means including all transactions and the same
for all ancestors.
*/
class CDeltaBlock : public CBlock {
public:
    //! Create incomplete deltablock from header and coinbase
    CDeltaBlock(const CBlockHeader &header,
                const CTransactionRef &coinbase);

    /*! Set of parent delta block hashes, as parsed from the coinbase */
    std::vector<uint256> deltaParentHashes() const;

    /*! If fully known: Get set of weak ancestors. */
    std::vector<ConstCDeltaBlockRef> ancestors() const;

    /*! ancestors hashes from the coinbase (not otherwise tested) */
    std::vector<uint256> ancestorHashes() const;

    //! Get delta set of transactions. That is the set of transactions
    //! that is added in this delta block, excluding the new coinbase.
    //! Order is pretty much random!
    std::vector<CTransactionRef> deltaSet() const;

    /*! Get cumulative POW of weak block in weak block POW units. (one
      for each POW crossing the weak POW threshold, or one in any
      ancestor, not counting doubly). Negative if this block or any of the ancestor blocks
      doesn't have a weak POW that is at least weakPOWfromPOW(strong-parent) or if any weak
      ancestors don't have the same strongparenthash or if any parent is not yet known. */
    int weakPOW() const;

    /*! Check whether this deltablock could be merged with the given other deltablock. */
    bool compatible(const CDeltaBlock& other) const;

    /*! Same as above, but checks for a set of delta block refs. */
    bool compatible(const std::vector<ConstCDeltaBlockRef>& others) const;

    static void addAncestorOPRETURNs(CMutableTransaction& coinbase,
                                     std::vector<uint256> ancestor_hashes);

    /*! Get set of currently known, complete delta chain tips for
     *  given strong prevblock. Sorted by arrival order (earlier
     *  arrived blocks first).  */
    static std::vector<ConstCDeltaBlockRef> tips(const uint256& strongparenthash);

    /*! Create merged template from ancestors, preferring the earliest
      arrived deltablock that has the most transactions, and then
      merging all other potential ancestors exhaustively. Considers
      only blocks that have the given strong parent hash. Note that
      almost all the header fields in the returned result are not
      going to be filled in yet.

      tips_override can be used to select special tips to use as merge candidates (for unit testing).
    */
    static CDeltaBlockRef bestTemplate(const uint256& strongparenthash,
                                       const std::vector<ConstCDeltaBlockRef>* tips_override=nullptr);

    //! Tries to register this delta block reference in the internal table
    /*! Does nothing if block is already known. */
    static void tryRegister(const CDeltaBlockRef& ref);

    //! Use to fill in delta transaction data and mark the block as complete
    /*! This might fail if something about this block doesn't allow it
     *  to be completed. In this case, the corresponding entry in the
     *  weak blocks table is not marked complete. */
    void tryMakeComplete(const std::vector<CTransactionRef>& delta_txns);

    /*! Notify deltablocks subsystem of a newly arrived strong block, for internal housekeeping. */
    static void newStrong(const uint256& stronghash);

    /*! Check whether the given strong hash is known as a potential anchor for a delta blocks
      chain in the deltablocks subsystem. */
    static bool knownStrong(const uint256& stronghash);

    /*! Global delta blocks enable flag */
    static bool isEnabled(const CChainParams& params, const CBlockIndex *pIndexPrev);

    /*! Get a delta block by its hash. */
    static ConstCDeltaBlockRef byHash(const uint256& hash);

    /*! Get the latest delta block that arrived for a given strong
     *  hash parent. Or return nullptr if there's none. */
    static ConstCDeltaBlockRef latestForStrong(const uint256& hash);

    //! Overwritten add that also adds to the delta set
    void add(const CTransactionRef &txnref);

    //! Return all currently known delta blocks (by strong block hash)
    static std::map<uint256, std::vector<ConstCDeltaBlockRef> > knownInReceiveOrder();

    //! Set all transactions to be known
    void setAllTransactionsKnown();

    //! Are all transactions known?
    bool allTransactionsKnown() const;

    //! Check whether delta block has strong POW (only checked after registration)
    bool isStrong() const;

    //! Reset internal data; mostly for unit testing
    static void resetAll();

    //! Test outpoint for whether it is spent in the delta block already
    bool spendsOutput(const COutPoint &out) const;
private:
    static void mergeDeltablocks(
        const std::vector<ConstCDeltaBlockRef> &ancestors,
        CPersistentTransactionMap &all_tx,
        CSpentMap &all_spent);

    //! Recursively return all ancestors
    std::set<ConstCDeltaBlockRef> allAncestors() const;
    void parseCBhashes();

    std::vector<uint256> delta_parent_hashes;

    std::vector<CTransactionRef> delta_set;

    mutable bool is_strong;
    mutable bool weakpow_cached; // true only if final weak pow is known
    mutable int cached_weakpow;

    //! Flag to indicate whether all transactions are known for this weak block
    bool fAllTransactionsKnown;

    //! Spent index for this delta block. "y=spent[x]: output x is spend by TXID y"
    CSpentMap spent;

    friend int weakPOW_internal(const std::vector<ConstCDeltaBlockRef>& merge_set, const uint256& hashPrevBlock);
};



#endif
