#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <queue>
#include "sync.h"
#include "weakblock.h"
#include "tweak.h"
#include "util.h"
#include "chainparams.h"

// FIXME: what about asserts here?

//#define DEBUG_DETAIL 1

// BU tweaks enable and config for weak blocks
extern CTweak<uint32_t> wbConsiderPOWratio;
extern CTweak<uint32_t> wbEnable;

bool weakblocksEnabled() {
    LOCK(cs_weakblocks);
    return wbEnable.value;
}

uint32_t weakblocksConsiderPOWRatio() {
    AssertLockHeld(cs_weakblocks);
    if (Consensus::Params().fPowNoRetargeting) {
        //LOG(WB, "Returning consideration POW for testnet.\n");
        return 4;
    }
    //LOG(WB, "Returning configured consideration POW ratio %d.\n", wbConsiderPOWratio.value);
    return wbConsiderPOWratio.value;
}

uint32_t weakblocksMinPOWRatio() {
    AssertLockHeld(cs_weakblocks);
    if (Consensus::Params().fPowNoRetargeting)
        return 8;
    return 600;
}


// Weak blocks data structures

// map from TXID back to weak blocks it is contained in
std::multimap<uint256, const Weakblock*> txid2weakblock;

// set of all weakly confirmed transactions
// this one uses most of the memory
std::map<uint256, CTransaction> weak_transactions;

// counts the number of weak blocks found per TXID
// is the number of weak block confirmations
// FIXME: maybe use an appropriate smart pointer structure here?
// Drawback would be to do all the ref counting where it doesn't really matters
std::map<uint256, size_t> weak_txid_refcount;

// map from block hash to weak block.
std::map<uint256, const Weakblock*> hash2weakblock;

// map from weak block memory location to hash
std::unordered_map<const Weakblock*, uint256> weakblock2hash;

// map from weakblock memory location to header info
std::unordered_map<const Weakblock*, CBlockHeader> weakblock2header;

// map of weak block hashes to their underlying weak block hashes
// This is a map of hashes to possibly allow referencing to not-yet-received
// weakblocks in the future.
std::map<uint256, uint256> extends;

// weak/delta block chain tips
// Ordered chronologically - a later chain tip will be further down in the vector
// Therefore the "best weak block" is the one with the largest weak height that
// comes earliest in this vector.
std::vector<const Weakblock*> weak_chain_tips;

// Cache of blocks reassembled from weakblocks
std::unordered_map<const Weakblock*, const CBlock*> reassembled;

// weak chain tips to remove next round
// The weak blocks that are listed here can still be referenced for efficient
// delta transmission but will not be considered as active chain tips otherwise.
std::unordered_set<const Weakblock*> to_remove;


CCriticalSection cs_weakblocks;

uint256 candidateWeakHash(const CBlock& block) {
    if (block.vtx.size()<1) return uint256();

    const CTransactionRef& coinbase = block.vtx[0];

    for (const CTxOut out : coinbase->vout) {
        const CScript& cand = out.scriptPubKey;
        // is it OP_RETURN, size byte (34), 'WB'+32 byte hash?
        if (cand.size() == 36) {
            if (cand[0] == OP_RETURN && cand[1] == 0x22 &&
                cand[2] == 'W' && cand[3] == 'B') {
                uint256 hash;
                std::copy(cand.begin()+4, cand.end(), hash.begin());
                LOG(WB, "Found candidate weak block hash %s in block %s.\n", hash.GetHex(), block.GetHash().GetHex());
                return hash;
            }
        }
    }
    return uint256();
}

bool extendsWeak(const CBlock &block, const Weakblock* underlying) {
    AssertLockHeld(cs_weakblocks);
    if (underlying == NULL) return false;
    if (underlying->size() > block.vtx.size()) return false;
    for (size_t i=1; i < underlying->size(); i++)
        if (*(*underlying)[i] != *block.vtx[i])
            return false;
    return true;
}

bool extendsWeak(const Weakblock *wb, const Weakblock* underlying) {
    AssertLockHeld(cs_weakblocks);
    if (underlying == NULL || wb == NULL) return false;
    if (underlying->size() > wb->size()) return false;
    for (size_t i=1; i < underlying->size(); i++)
        if ((*underlying)[i] != (*wb)[i])
            return false;
    return true;
}


// Helper function to insert a transaction into the weak_transactions list
// and do ref counting.
static inline CTransaction* storeTransaction(const CTransaction &otx) {
    AssertLockHeld(cs_weakblocks);
    uint256 txid = otx.GetHash();
    CTransaction *tx = NULL;
    if (weak_transactions.count(txid) != 0) {
        tx = &weak_transactions[txid];
        weak_txid_refcount[txid]++;
    } else {
        assert (weak_txid_refcount.count(txid) == 0);
        weak_transactions[otx.GetHash()] = otx;
        tx = &weak_transactions[txid];
        weak_txid_refcount[txid]=1;
    }
    return tx;
}

bool storeWeakblock(const CBlock &block) {
    uint256 blockhash = block.GetHash();
    Weakblock* wb=new Weakblock();

    LOCK(cs_weakblocks);
    if (hash2weakblock.count(blockhash) > 0) {
        LOG(WB, "Ignoring attempt to store weak block %s twice.\n", blockhash.GetHex());
        // stored it already
        return false;
    }
    uint256 underlyinghash = candidateWeakHash(block);

    const Weakblock* underlying = NULL;
    if (hash2weakblock.count(underlyinghash) > 0)
        underlying = hash2weakblock[underlyinghash];

#if 0
    if (!underlyinghash.IsNull() && underlying == NULL) {
        // Note: It might be possible to store dangling underlying weakblocks in the extends map and fill then in later. But this makes it necessary to have some more complex validation checks here.
        LOG(WB, "Weak block %s with unknown underlying block %s. Ignoring.\n", blockhash.GetHex(), underlyinghash.GetHex());
        return false;
    }

    if (underlying != NULL && !extendsWeak(block, underlying)) {
        LOG(WB, "WARNING, block %s does not extend weak block %s, even though it says so!\n", blockhash.GetHex(), underlyinghash.GetHex());
        // Won't store invalid block
        return false;
    }
#else
    if (!underlyinghash.IsNull() && underlying == NULL) {
        LOG(WB, "Weak block %s with unknown underlying block %s. Assuming start of new chain.\n", blockhash.GetHex(), underlyinghash.GetHex());
    } else if (underlying != NULL && !extendsWeak(block, underlying)) {
        LOG(WB, "WARNING, block %s does not extend weak block %s, even though it says so! Assuming start of new chain.\n", blockhash.GetHex(), underlyinghash.GetHex());
        underlying = NULL;
    }

#endif

    for (const CTransactionRef& otx : block.vtx) {
        CTransaction *tx = storeTransaction(*otx);
        uint256 txhash = tx->GetHash();
        txid2weakblock.insert(std::pair<uint256, const Weakblock*>(txhash, wb));
        wb->push_back(tx);
    }

    hash2weakblock[blockhash] = wb;
    weakblock2hash[wb] = blockhash;
    weakblock2header[wb] = block;

    if (underlying != NULL) {
        extends[blockhash]=underlyinghash;
        LOG(WB, "Weakblock %s is referring to underlying weak block %s.\n", weakblock2hash[wb].GetHex(), underlyinghash.GetHex());

        auto wct_iter = find(weak_chain_tips.begin(),
                         weak_chain_tips.end(),
                         underlying);
        if (wct_iter != weak_chain_tips.end()) {
            LOG(WB, "Underlying weak block %s was chain tip before. Moving to new weakblock.\n", underlyinghash.GetHex());
            weak_chain_tips.erase(wct_iter);
        }
    }
    weak_chain_tips.push_back(wb);
    LOG(WB, "Tracking weak block %s of %d transactions.\n", blockhash.GetHex(), wb->size());
    return true;
}

/*! Reassemble a block from a weak block. Does NOT check the
  reassembled array for a cached result first; that is the purpose of
  the blockForWeak(..) accessor. */
static inline const CBlock* reassembleFromWeak(const Weakblock* wb) {
    AssertLockHeld(cs_weakblocks);
    assert(wb != NULL);

    CBlock* result = new CBlock(weakblock2header[wb]);
    for (CTransaction* tx : *wb) {
        result->vtx.push_back(std::shared_ptr<CTransaction>(tx));
    }
    assert (weakblock2hash[wb] == result->GetHash());
    return result;
}

const CBlock* blockForWeak(const Weakblock* wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == NULL) return NULL;
    if (reassembled.count(wb) == 0)
        reassembled[wb] = reassembleFromWeak(wb);
    return reassembled[wb];
}

const Weakblock* getWeakblock(const uint256& blockhash) {
    AssertLockHeld(cs_weakblocks);
    if (hash2weakblock.count(blockhash))
        return hash2weakblock[blockhash];
    else return NULL;
}

const uint256 HashForWeak(const Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == NULL) return uint256();
    return weakblock2hash[wb];
}

int weakHeight(const uint256 wbhash) {
    AssertLockHeld(cs_weakblocks);
    if (wbhash.IsNull()) {
        LOG(WB, "weakHeight(0) == -1\n");
        return -1;
    }
    if (to_remove.count(hash2weakblock[wbhash])) {
        //LOG(WB, "weakHeight(%s) == -1 (block marked for removal)\n", wbhash.GetHex());
        return -1;
    }

    if (extends.count(wbhash)) {
        int prev_height = weakHeight(extends[wbhash]);
        if (prev_height >=0)
            return 1+prev_height;
        else
            return -1;
    } else return 0;
}

int weakHeight(const Weakblock* wb) {
    if (wb == NULL) {
        LOG(WB, "weakHeight(NULL) == -1\n");
        return -1;
    }
    return weakHeight(weakblock2hash[wb]);
}

const Weakblock* getWeakLongestChainTip() {
    LOCK(cs_weakblocks);
    int max_height=-1;
    const Weakblock* longest = NULL;

    for (const Weakblock* wb : weak_chain_tips) {
        int height = weakHeight(wb);
        if (height > max_height) {
            longest = wb;
            max_height = height;
        }
    }
    return longest;
}

// opposite of storeTransaction: remove a transaction from weak_transactions and
// do ref-counting.
static inline void removeTransaction(const CTransaction *tx) {
    AssertLockHeld(cs_weakblocks);
    uint256  txhash=tx->GetHash();
    assert (weak_txid_refcount[txhash] > 0);
    assert (weak_transactions.count(txhash) > 0);
    weak_txid_refcount[txhash]--;

    if (weak_txid_refcount[txhash] == 0) {
        weak_transactions.erase(txhash);
        weak_txid_refcount.erase(txhash);
        txid2weakblock.erase(txhash);
    }
}

// Forget about a weak block. Cares about the immediate indices and the transaction list
// but NOT the DAG in extends / weak_chain_tips.
static inline void forgetWeakblock(Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    LOG(WB, "Removing weakblock %s.\n", weakblock2hash[wb].GetHex());
    // map from TXID back to weak blocks it is contained in
    uint256 wbhash = weakblock2hash[wb];

    for (CTransaction* tx : *wb) {
        removeTransaction(tx);
    }
    hash2weakblock.erase(wbhash);
    weakblock2hash.erase(wb);
    weakblock2header.erase(wb);
    if (reassembled.count(wb) > 0)
        reassembled.erase(wb);
    delete wb;
}

/* Remove a weak block chain tip and all blocks before that one that are not part of other known chains. */
static inline void purgeChainTip(Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    LOG(WB, "Purging weak block %s, which is currently a chain tip.\n", weakblock2hash[wb].GetHex());

    Weakblock* wb_old;

    do {
        uint256 wbhash=weakblock2hash[wb];
        forgetWeakblock(wb);
        wb_old = NULL;

        if (extends.count(wbhash)) {
            uint256 underlyinghash=extends[wbhash];
            extends.erase(wbhash);
            if (hash2weakblock.count(underlyinghash)) {
                wb_old = const_cast<Weakblock*>(wb);
                wb = const_cast<Weakblock*>(hash2weakblock[underlyinghash]);

                // stop if any other chain depends on wb now
                // FIXME: this might be somewhat slow?
                for (std::pair<const Weakblock*, uint256> p : weakblock2hash) {
                    const uint256 otherhash = p.second;
                    if (extends.count(otherhash)) {
                        if (extends[otherhash] == underlyinghash) {
                            LOG(WB, "Stopping removal at %s as it is used by other chain block %s.\n",
                                     otherhash.GetHex(), underlyinghash.GetHex());
                            return;
                        }
                    }
                }
            }
        }
    } while (wb_old != NULL);
    LOG(WB, "Purge finished, reached bottom of chain.\n");
}

void purgeOldWeakblocks() {
    LOCK(cs_weakblocks);
    LOG(WB, "Purging old chain tips. %d chain tips right now.\n", weak_chain_tips.size());

    std::vector<const Weakblock*> new_weak_chain_tips;
    for (const Weakblock* wb : weak_chain_tips) {
        if (to_remove.count(wb)) {
            purgeChainTip(const_cast<Weakblock*>(wb));
            to_remove.erase(wb);
        } else {
            to_remove.insert(wb);
            new_weak_chain_tips.push_back(wb);
        }
    }
    weak_chain_tips = new_weak_chain_tips;
}

std::vector<std::pair<uint256, int> > weakChainTips() {
    LOCK(cs_weakblocks);
    std::vector<std::pair<uint256, int> > result;
    for (const Weakblock* wb : weak_chain_tips)
        result.push_back(std::pair<uint256, int>(weakblock2hash[wb],
                                                    weakHeight(wb)));
    return result;
}

const Weakblock* underlyingWeak(const Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == NULL) return NULL;

    if (extends.count(weakblock2hash[wb])) {
        uint256 underlyinghash=extends[weakblock2hash[wb]];
        if (hash2weakblock.count(underlyinghash))
            return hash2weakblock[underlyinghash];
        else return NULL;
    }
    else return NULL;
}

int numKnownWeakblocks() { LOCK(cs_weakblocks); return weakblock2hash.size(); }
int numKnownWeakblockTransactions() { LOCK(cs_weakblocks); return weak_transactions.size(); }


void weakblocksConsistencyCheck() {
    LOCK(cs_weakblocks);
    LOG(WB, "Doing internal consistency check.\n");
    assert(hash2weakblock.count(uint256()) == 0);
    assert(weakblock2header.count(NULL) == 0);
    assert(weakblock2hash.count(NULL) == 0);
    assert(hash2weakblock.size() == weakblock2hash.size());
    assert(weakblock2header.size() == hash2weakblock.size());
    assert(weak_chain_tips.size() <= hash2weakblock.size());
    int longest_height=-1;
    std::set<const Weakblock*> longest_tips;

    for (std::pair<uint256, const Weakblock*> p : hash2weakblock) {
        const uint256 blockhash = p.first;
        const Weakblock* wb = p.second;

        LOG(WB, "Consistency check for weak block %s.\n", blockhash.GetHex());

        assert(weakblock2hash[wb] == blockhash);

        // collect chain of blocks this one builds upon
        std::set<const Weakblock*> chain;
        const Weakblock* node = wb;

        while (underlyingWeak(node) != NULL) {
            node = underlyingWeak(node);
            chain.insert(node);
            assert(extendsWeak(wb, node));
        }
        LOG(WB, "Chain size: %d, weak height: %d\n", chain.size(), weakHeight(wb));
        assert ((int)chain.size() == weakHeight(wb));

        if ((int)chain.size() >= longest_height) {
            if ((int)chain.size() > longest_height) {
                longest_tips.clear();
            }
            longest_tips.insert(wb);
            longest_height = chain.size();
        }
    }

    if (longest_height < 0) {
        assert(getWeakLongestChainTip() == NULL);
    } else {
        assert(longest_tips.count(getWeakLongestChainTip()));
    }

    // make sure that all hashes in extends are actual, known weak blocks
    // this requirement might be relaxed later on
    for (std::pair<uint256, uint256> p : extends) {
        assert (hash2weakblock.count(p.first) > 0);
        assert (hash2weakblock.count(p.second) > 0);
    }
}

void weakblocksEmptyCheck() {
    LOCK(cs_weakblocks);
    assert (txid2weakblock.size() == 0);
    assert (weak_transactions.size() == 0);
    assert (weak_txid_refcount.size() == 0);
    assert (hash2weakblock.size() == 0);
    assert (weakblock2hash.size() == 0);
    assert (weakblock2header.size() == 0);
    assert (extends.size() == 0);
    assert (weak_chain_tips.size() == 0);
    assert (reassembled.size() == 0);
}
