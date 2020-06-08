
#include "bobtail/bobtail.h"
#include "main.h"
#include "pow.h"
#include "chainparams.h"
#include "arith_uint256.h"
#include "deltablocks.h"
#include "consensus/merkle.h"
#include "validation/validation.h"

#include <stack>
#include <queue>
// FIXME: Add copyright here (awemany / BU devs)
// FIXME: locking locking locking!!

//! For all the global deltablock data structures
CCriticalSection cs_db;

//! Track delta blocks for these n last strong blocks
const size_t TRACK_N_STRONG = 4;

/*! Known, complete delta blocks. */
extern std::map<uint256, ConstCDeltaBlockRef> known_dbs;
// delta blocks in receive order, by strong block parent hash
static std::map<uint256, std::vector<ConstCDeltaBlockRef> > incoming_dbs;
// strong blocks in receive order, limited to TRACK_N_STRONG
static std::vector<uint256> strongs_for_db;

bool CDeltaBlock::isEnabled(const CChainParams& params, const CBlockIndex *pindexPrev) {
    // FIXME: completely broken!
    bool canonical = fCanonicalTxsOrder;
    return canonical;
}


CDeltaBlock::CDeltaBlock(const CBlockHeader &header,
                         const CTransactionRef &coinbase) :
    weakpow_cached(false), cached_weakpow(-1), fAllTransactionsKnown(false) {
    *(CBlockHeader*)this = header;
    vtx.push_back(coinbase);
    parseCBhashes();
}

std::vector<uint256> CDeltaBlock::deltaParentHashes() const {
    LOCK(cs_db);
    return delta_parent_hashes;
}

std::vector<ConstCDeltaBlockRef> CDeltaBlock::ancestors() const {
    LOCK(cs_db);
    std::vector<ConstCDeltaBlockRef> result;
    for (auto &hash : delta_parent_hashes) {
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

    for (const CTxOut out : vtx[0]->vout) {
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

std::vector<uint256> CDeltaBlock::allAncestorHashes() const
{
    std::vector<uint256> hashes;

    for (auto &anc : allAncestors())
    {
        hashes.push_back(anc->GetHash());
    }

    return hashes;
}

std::vector<CTransactionRef> CDeltaBlock::deltaSet() const {
    LOCK(cs_db);
    return delta_set;
}

void CDeltaBlock::add(const CTransactionRef &txref) {
    // support LTOR only
    vtx.push_back(txref);
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
    if (known_dbs[ref->GetHash()] != nullptr) {
        LOG(WB, "Ignoring, already known.\n");
        return;
    }
    LOG(WB, "Delta block %s is strong: %s\n", ref->GetHash().ToString(), ref->isStrong());
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

    CTransactionRef cb_saved = vtx[0];
    delta_set.clear();
    std::vector<CTransactionRef> vtx;
    spent = CSpentMap();
    // mergeDeltablocks(ancestors(), mtx, spent);

    vtx.push_back(cb_saved);

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

//bool CDeltaBlock::isStrong() const { return is_strong; }
bool CDeltaBlock::isStrong() const { int k=3; /*FIXME*/ return CheckBobtailPoW(*((CBlockHeader *)this), delta_parent_hashes, Params().GetConsensus(), k); }

void CDeltaBlock::resetAll() {
    LOCK(cs_db);
    known_dbs.clear();
    incoming_dbs.clear();
    strongs_for_db.clear();
}

void CDeltaBlock::processNew(CDeltaBlockRef dbr) {
    const uint256 hash = dbr->GetHash();
    LOG(WB, "Processing new delta block %s with strong parent %s.\n", hash.GetHex(), dbr->hashPrevBlock.GetHex());

    // first, check for sufficient weak POW
    if (!CheckProofOfWork(hash, weakPOWfromPOW(dbr->nBits), Params().GetConsensus(), true)) {
        LOG(WB, "Delta block failed WPOW check. Ignoring.\n");
        return;
    }

    // next, check block's validity
    CValidationState state;
    CBlockIndex* pindexPrev = LookupBlockIndex(dbr->hashPrevBlock);

    /* FIXME: Deltablocks receival needs to be allowed also on top of
       non-tips in case there are strong block races.  The trouble is
       that TestBlockValidity uses a coins view and there's only one
       available for the tip. With a persistent data store for the
       UTXO that one can move around on, this should become easier. */


    // Testing: Assume nodes are meaning well and not generating junk
    // Still, run TestBlockValidity when on the main chain
    // (which should be most times), to simulate processing time.
    // FIXME!
    {
        LOCK(cs_main);
        if (pindexPrev == chainActive.Tip()) {
            TestBlockValidity(state, Params(), *dbr, pindexPrev, false, true);
        } else {
            LOG(WB, "FIXME: Delta block skipped validation as it is not based on the strong chain tip.\n");
        }
    }

      // any block here should be completely reconstructed
    //DbgAssert(dbr->vtx, return);
    {
        LOCK(cs_db);
        // TODO: REGISTER SUBBLOCK HERE!!!!
        //CDeltaBlock::tryRegister(dbr);
        if (CDeltaBlock::byHash(dbr->GetHash()) == nullptr) {
            LOG(WB, "Delta block %s failed to register. Dropping it.\n", dbr->GetHash().GetHex());
            return;
        }
        LOG(WB, "Delta block %s successfully checked for WPOW, validity and registered.\n",
            dbr->GetHash().GetHex());
    }

    dbr->fXVal = true;
    // if it is a strong block, process it as such as well
    // FIXME: copy'n'paste from unlimited.cpp
    // TODO: CHECK FOR BOBTAIL BLOCK HERE!!!
    /*
    if (dbr->isStrong()) {
        PV->StopAllValidationThreads(dbr->nBits);
        if (!ProcessNewBlock(state, Params() , nullptr, dbr.get(), true, nullptr, false)) {
            LOG(WB, "Delta block that is strong block has not been accepted!\n");
            return;
        }
    }
    */
}
bool CDeltaBlock::spendsOutput(const COutPoint &out) const {
    return spent.contains(out);
}
