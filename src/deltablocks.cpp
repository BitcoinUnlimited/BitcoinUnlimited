#include "main.h"
#include "pow.h"
#include "chainparams.h"
#include "arith_uint256.h"
#include "deltablocks.h"
#include "consensus/merkle.h"
#include <stack>
#include <queue>
// FIXME: Add copyright here (awemany / BU devs)
// FIXME: locking locking locking!!

//! For all the global deltablock data structures
CCriticalSection cs_db;

//! Track delta blocks for these n last strong blocks
const size_t TRACK_N_STRONG = 4;

/*! Known, complete delta blocks. */
static std::map<uint256, ConstCDeltaBlockRef> known_dbs;
// delta blocks in receive order, by strong block parent hash
static std::map<uint256, std::vector<ConstCDeltaBlockRef> > incoming_dbs;
// strong blocks in receive order, limited to TRACK_N_STRONG
static std::vector<uint256> strongs_for_db;

// to check wpow use sth like this:
// if (!CheckProofOfWork(ahashMerkleRoot, weakPOWfromPOW(nBits), Consensus::Params(), true)) { ...
extern unsigned int weakPOWfromPOW(unsigned int nBits) {
    arith_uint256 a;
    a.SetCompact(nBits);
    a = ~a;
    a *= 1000;
    a = ~a;
    return a.GetCompact();
}

bool CDeltaBlock::isEnabled(const CChainParams& params, const CBlockIndex *pindexPrev) {
    // FIXME: completely broken!
    bool canonical = enableCanonicalTxOrder.Value();
    return canonical;
}


CDeltaBlock::CDeltaBlock(const CBlockHeader &header,
                         const CTransactionRef &coinbase) :
    is_strong(CheckProofOfWork(header.GetHash(), header.nBits, Params().GetConsensus())),
    weakpow_cached(false), cached_weakpow(-1), fAllTransactionsKnown(false) {
    *(CBlockHeader*)this = header;
    setCoinbase(coinbase);
    parseCBhashes();
}

std::vector<uint256> CDeltaBlock::deltaParentHashes() const {
    LOCK(cs_db);
    return delta_parent_hashes;
}

std::vector<ConstCDeltaBlockRef> CDeltaBlock::ancestors() const {
    LOCK(cs_db);
    std::vector<ConstCDeltaBlockRef> result;
    for (auto hash : delta_parent_hashes) {
        if (!known_dbs.count(hash)) {
            LOG(WB, "Delta block misses ancestor(s)!\n");
            return std::vector<ConstCDeltaBlockRef>();
        }
        if (!known_dbs[hash]->isStrong())
            result.emplace_back(known_dbs[hash]);
    }
    return result;
}

std::vector<uint256> CDeltaBlock::ancestorHashes() const {
    return delta_parent_hashes;
}

int weakPOW_internal(const std::vector<ConstCDeltaBlockRef>& merge_set, const uint256& hashPrevBlock) {
    LOCK(cs_db);
    std::set<ConstCDeltaBlockRef> all_ancestors;
    std::stack<ConstCDeltaBlockRef> todo;
    for (auto a : merge_set)
        todo.push(a);

    while (todo.size() > 0) {
        const auto& anc = todo.top();
        todo.pop();
        if (all_ancestors.count(anc) == 0) {
            all_ancestors.insert(anc);
            if (! anc->fAllTransactionsKnown)
                return -1;
            if (anc->hashPrevBlock != hashPrevBlock)
                return -2;
            for (auto ancanc : anc->ancestors())
                todo.push(ancanc);
        }
    }
    return all_ancestors.size();
}

int CDeltaBlock::weakPOW() const {
    LOCK(cs_db);
    LOG(WB, "Querying deltablock %s for weak POW.\n", GetHash().GetHex());
    if (weakpow_cached) {
        LOG(WB, "Return cached wpow result %d\n", cached_weakpow);
        return cached_weakpow;
    }
    if (!fAllTransactionsKnown) {
        LOG(WB, "Returning -1 as not all transactions are known yet.\n");
        return -1;
    }
    cached_weakpow =  weakPOW_internal(ancestors(), hashPrevBlock);
    LOG(WB, "Calculated wpow from ancestors: %d\n", cached_weakpow);
    if (cached_weakpow == -2) { // ancestor mismatch. Weakblock is finally invalid
        LOG(WB, "WPOW=-1 as there is an ancestor mismatch.\n");
        weakpow_cached = true;
    } else if (cached_weakpow >=0) {
        cached_weakpow++; // add 1 for this block
        weakpow_cached=true;
    }
    LOG(WB, "WPOW result: %d\n", cached_weakpow);
    return cached_weakpow;
}

bool CDeltaBlock::compatible(const CDeltaBlock& other) const {
    LOCK(cs_db);
    LOG(WB, "Delta blocks compatibility check: %s and %s?\n",
        this->GetHash().GetHex(),
        other.GetHash().GetHex());
    // FIXME: caching!
    /*! FIXME2: Speed this up by only checking deltas up to common ancestor blocks. */
    /*! Simply checks that spent outputs in both blocks are spent by the transaction. */
    for (auto p : other.spent) {
        const COutPoint& outpoint = p.first;
        const uint256& txid = p.second;
        if (spent.contains(outpoint) && spent.at(outpoint) != txid) {
            LOG(WB, "Delta blocks incompatible, mismatching TXIDs: %s and %s.\n",
                spent.at(outpoint).GetHex(), txid.GetHex());
            return false;
        }
    }
    LOG(WB, "Delta blocks compatible.\n");
    return true;
}

bool CDeltaBlock::compatible(const std::vector<ConstCDeltaBlockRef>& others) const {
    LOCK(cs_db);
    // FIXME: caching!
    for (auto cdbref : others)
        if (!compatible(*cdbref)) return false;
    return true;
}


std::vector<ConstCDeltaBlockRef> CDeltaBlock::tips(const uint256& strongparenthash) {
    LOCK(cs_db);
    // FIXME: cache chaintips / keep them in more efficient data structure
    if (!incoming_dbs.count(strongparenthash)) return std::vector<ConstCDeltaBlockRef>();
    std::set<ConstCDeltaBlockRef> not_a_tip;

    for (auto db : incoming_dbs[strongparenthash])
        for (auto anc : db->ancestors())
            not_a_tip.insert(anc);

    std::vector<ConstCDeltaBlockRef> result;
    for (auto db : incoming_dbs[strongparenthash])
        if (not_a_tip.count(db) == 0)
            result.emplace_back(db);

    LOG(WB, "Delta blocks calculated tips. Returning %d tips.\n", result.size());
    return result;
}

void CDeltaBlock::parseCBhashes() {
    LOCK(cs_db);
    delta_parent_hashes.clear();
    std::set<uint256> seen;

    LOG(WB, "Analyzing delta block (maybe template) %s for weak ancestor hashes.\n", GetHash().GetHex());

    for (const CTxOut out : coinbase()->vout) {
        const CScript& cand = out.scriptPubKey;
        // is it OP_RETURN, size byte (34), 'DB'+32 byte hash?
        if (cand.size() == 36) {
            if (cand[0] == OP_RETURN && cand[1] == 0x22 &&
                cand[2] == 'D' && cand[3] == 'B') {
                uint256 hash;
                std::copy(cand.begin()+4, cand.end(), hash.begin());
                LOG(WB, "Found ancestor hash %s.\n", hash.GetHex());
                if (! seen.count(hash)) { // only add refs once!
                    delta_parent_hashes.emplace_back(hash);
                    seen.insert(hash);
                } else {
                    LOG(WB, "ERROR: Ignoring duplicate!\n");
                }
            }
        }
    }
    LOG(WB, "Extracted %d ancestor hashes.\n", delta_parent_hashes.size());
}

void CDeltaBlock::addAncestorOPRETURNs(CMutableTransaction& coinbase,
                                       std::vector<uint256> ancestor_hashes) {
    for (const uint256& hash : ancestor_hashes) {
        CTxOut opret_out;

        opret_out.nValue = 0;
        opret_out.scriptPubKey = CScript() << OP_RETURN;
        opret_out.scriptPubKey.push_back(0x22); // size byte
        opret_out.scriptPubKey.push_back('D'); // marker
        opret_out.scriptPubKey.push_back('B');
        opret_out.scriptPubKey.insert(opret_out.scriptPubKey.end(), hash.begin(), hash.end());
        coinbase.vout.emplace_back(opret_out);
    }
    LOG(WB, "Created coinbase template with %d ancestor hashes.\n", ancestor_hashes.size());
}

std::set<ConstCDeltaBlockRef> CDeltaBlock::allAncestors() const {
    LOCK(cs_db);
    std::set<ConstCDeltaBlockRef> result;
    std::stack<ConstCDeltaBlockRef> todo;
    for (auto anc : ancestors()) todo.push(anc);
    while (todo.size()>0) {
        auto db = todo.top(); todo.pop();
        DbgAssert(db->allTransactionsKnown(), return std::set<ConstCDeltaBlockRef>());
        result.insert(db);
        for (auto anc : db->ancestors()) {
            if (!result.count(anc)) {
                todo.push(anc);
            }
        }
    }
    return result;
}

std::vector<CTransactionRef> CDeltaBlock::deltaSet() const {
    LOCK(cs_db);
    return delta_set;
}

/*! Internal function to merge given delta blocks into one new one, saving memory by reusing the biggest
  contained block (by number of transactions). */
void CDeltaBlock::mergeDeltablocks(const std::vector<ConstCDeltaBlockRef> &ancestors,
                                          CPersistentTransactionMap &all_tx,
                                          CSpentMap &all_spent) {
    LOCK(cs_db);
    LOG(WB, "Merging deltablocks. Merging %d ancestors.\n", ancestors.size());
    ConstCDeltaBlockRef biggest = nullptr;
    size_t num_tx_max = 0;
    for (auto anc : ancestors) {
        if (anc->numTransactions() > num_tx_max) {
            num_tx_max = anc->numTransactions();
            biggest = anc;
        }
    }
    if (biggest != nullptr) {
        LOG(WB, "Found biggest (number of txn) ancestor: %s with %d total txns.\n", biggest->GetHash().GetHex(),
            num_tx_max);
    } else {
        LOG(WB, "No biggest ancestor found.\n");
    }
    if (biggest == nullptr) {
        all_tx = CPersistentTransactionMap();
        all_spent = CSpentMap();
        return;
    } else {
        all_tx = biggest->mtx.remove(*(biggest->mtx.by_rank(0).key_ptr())); // rm coinbase
        all_spent = biggest->spent;
    }

    //! Set of blocks which either have themselves been merged or a child of them has
    //  been merged, including all delta transactions
    std::set<ConstCDeltaBlockRef> done;
    done.insert(biggest);
    for (auto p : biggest->allAncestors()) {
        /*LOG(WB, "Marking (possibly indirect) ancestor %s of biggest ancestor %s as done.\n", p->GetHash().GetHex(),
          biggest->GetHash().GetHex()); */
        done.insert(p);
    }
    /* FIXME: document that this current approach is in principle O(n^2) for a strong
     * block period but as there'll be on the order of a thousand
     * blocks (for 600ms emission), one million operations over 600s
     * doesn't seem too bad yet ...

     This can probably be made a lot closer to O(n) by just using a
     priority queue for all ancestors to look at and marking ancestors
     with highest WPOW as done first, while also tracking which
     ancestors are done because they're part of the biggest one.
 */

    std::stack<ConstCDeltaBlockRef> todo;
    for (auto anc : ancestors)
        todo.push(anc);

    while (todo.size() > 0) {
        auto db = todo.top(); todo.pop();
        if (done.count(db) != 0) {
            LOG(WB, "Skipping %s, marked as done already.\n", db->GetHash().GetHex());
            continue;
        }
        auto eps = db->deltaSet();
        LOG(WB, "Adding %d transactions (excl. coinbase) from ancestor %s.\n", eps.size(), db->GetHash().GetHex());
        for (size_t i=0; i < eps.size(); i++) { // skip coinbase
            auto txref = eps[i];
            all_tx=all_tx.insert(CTransactionSlot(txref), txref); // LTOR
            auto hash = txref->GetHash();
            for (auto input : txref->vin)
                // FIXME: additional check here?
                all_spent = all_spent.insert(input.prevout, hash);
        }
        for (auto parent : db->ancestors()) {
            LOG(WB, "Marking (possibly indirect) ancestor %s of %s as done.\n", db->GetHash().GetHex(),
                db->GetHash().GetHex());
            todo.push(parent);
        }
        done.insert(db);
    }
    LOG(WB,"Merge result: All transactions: %d, All spent: %d\n", all_tx.size(), all_spent.size());
    /* Ideas: There might be a better heuristic here that iterates
       through a full block and adds all transactions instead of going
       through the deltas - at least when the full block is
       sufficiently small.  Or, more generally, it might make sense
       to add a nice and fast union function to the
       persistent_map. Though it seems that knowing the common, shared
       points (Deltablocks) helps with the efficiency of the merge
       operation. */
}

CDeltaBlockRef CDeltaBlock::bestTemplate(const uint256& strongparenthash,
                                         const std::vector<ConstCDeltaBlockRef>* tips_override) {
    LOCK(cs_db);
    /*! Gather potential sets of delta block tips to merge.
      Note that this is in up to O(n^2) for n weak chain tips. */
    LOG(WB, "Creating best delta block template for strong parent %s.\n", strongparenthash.GetHex());

    std::vector< std::vector<ConstCDeltaBlockRef> > candidate_merges;

    auto all_tips = tips(strongparenthash);
    const std::vector<ConstCDeltaBlockRef>* used_tips = tips_override == nullptr ? &all_tips : tips_override;
    for (auto cblock : *used_tips) {
        LOG(WB, "Finding matching sets for block %s.\n", cblock->GetHash().GetHex());
        bool merged = false;
        for (auto& cm :  candidate_merges) {
            if (cblock->compatible(cm)) {
                cm.emplace_back(cblock);
                merged = true;
            }
        }
        if (!merged) {
            std::vector<ConstCDeltaBlockRef> new_cm;
            new_cm.emplace_back(cblock);
            candidate_merges.emplace_back(new_cm);
        }
    }

    LOG(WB, "Found %d candidate mergeable sets.\n", candidate_merges.size());
    for (auto cm : candidate_merges)
        LOG(WB, "Candidate set size: %d\n", cm.size());

    // total amount of weak pow for above merges
    std::vector<int> total_weak_pow;
    for (auto cm : candidate_merges)
        total_weak_pow.emplace_back(weakPOW_internal(cm, strongparenthash));

    // and gather maximum
    int max_weak_pow = -1;
    std::vector<ConstCDeltaBlockRef> merge_set;
    for (size_t i = 0; i < candidate_merges.size(); i++) {
        if (total_weak_pow[i] > max_weak_pow) {
            max_weak_pow = total_weak_pow[i];
            merge_set = candidate_merges[i];
        }
    }
    LOG(WB, "Maximum WPOW %d for a set of %d ancestors.\n", max_weak_pow, merge_set.size());

    CBlockHeader header;
    header.hashPrevBlock = strongparenthash;

    //! Set parent hashes
    std::vector<uint256> delta_parent_hashes;

    for (ConstCDeltaBlockRef cdbr : merge_set) {
        delta_parent_hashes.emplace_back(cdbr->GetHash());
    }

    /*! The coinbase that is returned by this function is very
      rudimentary and incomplete.  This is as to not take over too
      much of what the miner code in miner.cpp etc. should be
      doing. The coinbase here has basically just a vout filled with
      the OP_RETURNs for the ancestor pointers and that's it. It needs
      to be reworked by the miner code correspondingly. */
    CMutableTransaction coinbase_template;

    // make sure IsCoinBase() is true
    coinbase_template.vin.resize(1);
    coinbase_template.vin[0].prevout.SetNull();

    addAncestorOPRETURNs(coinbase_template, delta_parent_hashes);

    CPersistentTransactionMap all_tx;
    CSpentMap all_spent;

    CDeltaBlock::mergeDeltablocks(merge_set, all_tx, all_spent);

    CTransactionRef cb(new CTransaction(coinbase_template));

    CDeltaBlockRef cdr = std::shared_ptr<CDeltaBlock>(
        new CDeltaBlock(header, cb));
    cdr->mtx = all_tx.insert(CTransactionSlot(cb, 0), cb);
    cdr->spent = all_spent;
    cdr->delta_parent_hashes = delta_parent_hashes;
    return cdr;
}

void CDeltaBlock::add(const CTransactionRef &txref) {
    // support LTOR only
    mtx = mtx.insert(CTransactionSlot(txref), txref);
    // also needs to update spent index
    auto hash = txref->GetHash();
    for (auto input : txref->vin) {
        if (spent.contains(input.prevout)) return; // respend -> invalid, FIXME
        spent = spent.insert(input.prevout, hash);
    }
    delta_set.emplace_back(txref);
}


void CDeltaBlock::tryRegister(const CDeltaBlockRef& ref) {
    LOCK(cs_db);
    LOG(WB, "Trying to register delta block %s.\n", ref->GetHash().GetHex());
    if (known_dbs.count(ref->GetHash())) {
        LOG(WB, "Ignoring, already known.\n");
        return;
    }
    LOG(WB, "Delta block is strong: %s\n", ref->is_strong);
    known_dbs[ref->GetHash()] = ref;
    incoming_dbs[ref->hashPrevBlock].emplace_back(ref);

    /* Log transactions in delta block's delta set, this is used for
       performance estimates for now. */
    for (auto txr : ref->delta_set)
        LOG(WB, "Delta set of delta block %s contains TXID %s\n", ref->GetHash().GetHex(),
            txr->GetHash().GetHex());

    /* Log weak ancestor hashes as well, used for performance estimates / testing. */
    for (const auto& hash : ref->delta_parent_hashes)
        LOG(WB, "Delta block %s contains weak parent %s\n", ref->GetHash().GetHex(),
            hash.GetHex());

    LOG(WB, "Delta block %s has WPOW %d\n", ref->GetHash().GetHex(),
        ref->weakPOW());
}

void CDeltaBlock::tryMakeComplete(const std::vector<CTransactionRef>& delta_txns) {
    LOCK(cs_db);
    LOG(WB, "Trying to complete delta block %s with a delta set of size %d.\n",
        GetHash().GetHex(), delta_txns.size());

    CTransactionRef cb_saved = coinbase();
    delta_set.clear();
    mtx = CPersistentTransactionMap();
    spent = CSpentMap();
    mergeDeltablocks(ancestors(),
                     mtx, spent);

    setCoinbase(cb_saved);

    for (size_t i=0; i < delta_txns.size(); i++)
        if (!delta_txns[i]->IsCoinBase())
            add(delta_txns[i]);


    uint256 calc_merkle_root = BlockMerkleRoot(*this);
    if (hashMerkleRoot != calc_merkle_root) return;

    // only now, after the above checks update fAllTransactionsKnown
    setAllTransactionsKnown();
}

void CDeltaBlock::newStrong(const uint256& stronghash) {
    LOCK(cs_db);
    LOG(WB, "Delta blocks informed about new strong block %s.\n",
        stronghash.GetHex());

    /* FIXME: the logic invoking newStrong() should never let this
     * happen, deal with it as a proper internal error */
    for (auto h : strongs_for_db)
        if (h == stronghash) return;

    strongs_for_db.emplace_back(stronghash);
    if (strongs_for_db.size() > TRACK_N_STRONG) {
        uint256 rm_hash = strongs_for_db[0];
        strongs_for_db.erase(strongs_for_db.begin());
        for (auto db : incoming_dbs[rm_hash])
            known_dbs.erase(db->GetHash());
        incoming_dbs.erase(rm_hash);
    }
}

bool CDeltaBlock::knownStrong(const uint256& stronghash) {
    LOCK(cs_db);
    // FIXME: might get expensive for many strong blocks tracked
    bool result = false;

    for (auto h : strongs_for_db)
        if (h == stronghash) {
            result = true;
            break;
        }
    LOG(WB, "Check whether strong block is known as recent strong block %s to deltablocks subsystem: %d\n",
        stronghash.GetHex(), result);
    return result;
}

ConstCDeltaBlockRef CDeltaBlock::byHash(const uint256& hash) {
    LOCK(cs_db);
    if (known_dbs.count(hash) == 0) return nullptr;
    else return known_dbs[hash];
}

ConstCDeltaBlockRef CDeltaBlock::latestForStrong(const uint256& hash) {
    LOCK(cs_db);
    if (incoming_dbs.count(hash) == 0) return nullptr;
    else return incoming_dbs[hash].back();
}

std::map<uint256, std::vector<ConstCDeltaBlockRef> >  CDeltaBlock::knownInReceiveOrder() {
    LOCK(cs_db);
    return incoming_dbs;
}

void CDeltaBlock::setAllTransactionsKnown() { fAllTransactionsKnown = true; }
bool CDeltaBlock::allTransactionsKnown() const { return fAllTransactionsKnown; }

bool CDeltaBlock::isStrong() const { return is_strong; }

void CDeltaBlock::resetAll() {
    LOCK(cs_db);
    known_dbs.clear();
    incoming_dbs.clear();
    strongs_for_db.clear();
}

bool CDeltaBlock::spendsOutput(const COutPoint &out) const {
    return spent.contains(out);
}
