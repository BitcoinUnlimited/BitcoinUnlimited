#include "netdeltablocks.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "txadmission.h"
#include "main.h"
#include "net.h"
#include "validation/validation.h"
// FIXME: Add copyright here (awemany / BU devs)

extern CTxMemPool mempool;
extern CCriticalSection cs_db;

// FIXME!!
static const uint64_t shorttxidk1 = 1, shorttxidk2 = 2;
static const uint32_t sipHashNonce = 123;

/* This is is copied/adapted from graphene.cpp. Which is in turn from a
 * copy&paste hell between all the different relay variants. Ths needs
 * to be cleaned up!! */

static std::map<uint256, std::vector<CNetDeltaBlock> > anc_missing;

static std::map<uint64_t, uint256> getPartialTxHashesFromAllSources(
    const uint64_t _shorttxidk0, const uint64_t _shorttxidk1,
    const std::vector<CTransactionRef> vAdditionalTxs, CTransactionRef cb) {
    std::map<uint64_t, uint256> result;

    const uint64_t version = 2;

    // Do the orphans first before taking the mempool.cs lock, so that we maintain correct locking order.
    READLOCK(orphanpool.cs);
    for (auto &kv : orphanpool.mapOrphanTransactions)
    {
        uint64_t cheapHash = GetShortID(_shorttxidk0, _shorttxidk1, kv.first, version);
        LOG(WB, "[orphanpool]  Strong hash: %s, Cheap hash: %ull\n", kv.first.GetHex(), cheapHash);
        auto ir = result.insert(std::make_pair(cheapHash, kv.first));
        if (!ir.second) return std::map<uint64_t, uint256>();
    }
    std::vector<uint256> memPoolHashes;
    mempool.queryHashes(memPoolHashes);
    for (const uint256 &hash : memPoolHashes)
    {
        uint64_t cheapHash = GetShortID(_shorttxidk0, _shorttxidk1, hash, version);
        //LOG(WB, "[mempool]     Strong hash: %s, Cheap hash: %ull\n", hash.GetHex(), cheapHash);
        auto ir = result.insert(std::make_pair(cheapHash, hash));
        if (!ir.second) return std::map<uint64_t, uint256>();
    }

    for (auto &tx : vAdditionalTxs) {
        const uint256 &hash = tx->GetHash();
        uint64_t cheapHash = GetShortID(_shorttxidk0, _shorttxidk1, hash, version);
        //LOG(WB, "[vAdditional] Strong hash: %s(%d), Cheap hash: %ull\n", hash.GetHex(), tx->IsCoinBase(), cheapHash);

        auto ir = result.insert(std::make_pair(cheapHash, hash));
        if (!ir.second) return std::map<uint64_t, uint256>();
    }
    if (cb != nullptr) {
        const uint256 &hash = cb->GetHash();
        uint64_t cheapHash = GetShortID(_shorttxidk0, _shorttxidk1, hash, version);
        auto ir = result.insert(std::make_pair(cheapHash, hash));
        if (!ir.second) return std::map<uint64_t, uint256>();
        //LOG(WB, "[coinbase]    Strong hash: %s(%d), Cheap hash: %ull\n", hash.GetHex(), cb->IsCoinBase(), cheapHash);
    }
    return result;
}


CNetDeltaBlock::CNetDeltaBlock(ConstCDeltaBlockRef &dbref,
                               uint64_t nReceiverMemPoolTx) : delta_tx_size(dbref->deltaSet().size() + 1) {
    LOG(WB, "Constructing network representation for delta block %s\n",
        dbref->GetHash().GetHex());

    /*
    for (auto& txr : dbref->deltaSet()) {
        LOG(WB, "Delta set txn: %s\n", txr->GetHash().GetHex());
    }

    for (auto& txr : *dbref) {
        LOG(WB, "Delta block txn: %s\n", txr->GetHash().GetHex());
    }
    */
    header = dbref->GetBlockHeader();
    std::vector<uint256> delta_hashes;
    delta_hashes.emplace_back(dbref->coinbase()->GetHash());
    for (auto& txref : dbref->deltaSet())
        delta_hashes.emplace_back(txref->GetHash());

    // FIXME: why is coinbase excluded? c.f. graphene.cpp
    uint64_t nSenderMempoolPlusDeltaBlock = GetGrapheneMempoolInfo().nTx + delta_tx_size - 1; // exclude coinbase

    delta_gset = new CGrapheneSet(nReceiverMemPoolTx, nSenderMempoolPlusDeltaBlock, delta_hashes,
                                  shorttxidk1, shorttxidk2, 2, sipHashNonce, false);
}

CNetDeltaBlock::CNetDeltaBlock() : delta_gset(nullptr) {}

// ! Get a transaction by hash, not caring about where it is from exactly  ..
static CTransactionRef getTXFromWherever(const uint256& hash,
                                         std::map<uint256, CTransactionRef> delta_map) {
    CTransactionRef txr = CommitQGet(hash);
    if (txr == nullptr) {
        if (orphanpool.mapOrphanTransactions.count(hash)) {
            txr = orphanpool.mapOrphanTransactions[hash].ptx;
        } else {
            txr = mempool.get(hash);
            if (txr == nullptr) {
                if (delta_map.count(hash) >0)
                    txr = delta_map[hash];
            }
        }
    }
    if (txr == nullptr) {
        // if null here, something weird
        // happened during
        // reconstruction
        // (probably a race of
        // some sort)
        LOG(WB, "ERROR: Transaction %s disappeared while expecting it.\n", hash.GetHex());
    }
    return txr;
}

bool CNetDeltaBlock::reconstruct(CDeltaBlockRef& dbr, CNetDeltaRequestMissing** ppmissing_tx) {
    // FIXME: logging!
    // Check corner case of the graphene-set based block being
    // simplified to just a full transmission of the delta set
    // (delta_tx_additional == full delta set)
    if (delta_tx_size < 1) {
        LOG(WB, "Reconstructing delta block without coinbase is impossible.\n");
        return false; // block w/o coinbase impossible
    }

    LOG(WB, "Reconstructing delta block %s from %d delivered transactions, expected full size %d.\n",
        header.GetHash().GetHex(),
        delta_tx_additional.size(), delta_tx_size);

    if (delta_tx_additional.size() == delta_tx_size) {
        // reconstruct from complete delta set
        dbr = CDeltaBlockRef(new CDeltaBlock(header, delta_tx_additional[0]));

        // shuffle to make resulting persistent_map roughly balanced
        std::random_shuffle(delta_tx_additional.begin(), delta_tx_additional.end());

        dbr->tryMakeComplete(delta_tx_additional);
        LOG(WB, "Reconstructed delta block has all txn: %d\n", dbr->allTransactionsKnown());
        LOG(WB, "Reconstructed (from full set) delta block max depth: %d, for size: %d\n",
                dbr->treeMaxDepth(), dbr->numTransactions());
        if (!dbr->allTransactionsKnown()) return false;

        // dummy object just to signal completeness to the caller
        *ppmissing_tx = new CNetDeltaRequestMissing();
        return true;
    }

    std::map<uint64_t, uint256> mapPartialTxHash(
        getPartialTxHashesFromAllSources(shorttxidk1,
                                         shorttxidk2,
                                         delta_tx_additional, nullptr));

    if (!mapPartialTxHash.size()) {
        LOG(WB, "Reconstructing delta block %s failed due to hash collision.\n",
            header.GetHash().GetHex());
        *ppmissing_tx = new CNetDeltaRequestMissing();
        (*ppmissing_tx)->blockhash = dbr->GetHash();
        return true;
    }
    if (delta_gset == nullptr) {
        LOG(WB, "ERROR: Expected non-null graphene set object.\n");
        return false;
    }

    if (ppmissing_tx == nullptr) {
        LOG(WB, "ERROR: Expected place where to put the missing transaction set.\n");
        return false;
    }

    std::vector<uint64_t> deltaCheapHashes;
    try {
        deltaCheapHashes = delta_gset->Reconcile(mapPartialTxHash);
    } catch(std::runtime_error& err) {
        LOG(WB, "ERROR: Graphene set reconcilation failed, IBLT did not decode.\n");
        *ppmissing_tx = new CNetDeltaRequestMissing();
        (*ppmissing_tx)->blockhash = dbr->GetHash();
        return true;
    }

    /*! Reconstruction not possible as the length doesn't match the expected amount.
      Minus 1 as coinbase is always transmitted separately. */
    if (deltaCheapHashes.size() != delta_tx_size) {
        LOG(WB, "ERROR: Expected length of recontructed graphene set (%d) doesn't match number"
            " of transactions in delta block (%d).\n",
            deltaCheapHashes.size(), delta_tx_size);
        return false;
    }

    // Check for and reject if there are duplicates. FIXME: actually neccessary?
    std::set<uint64_t> dupcheck;
    for (auto cheaphash : deltaCheapHashes) dupcheck.insert(cheaphash);
    if (dupcheck.size() != deltaCheapHashes.size()) {
        LOG(WB, "ERROR: Duplicates in the reconstructed graphene set.\n");
        return false;
    }

    *ppmissing_tx = new CNetDeltaRequestMissing();
    CNetDeltaRequestMissing& missing_tx = **ppmissing_tx;

    for (auto cheaphash : deltaCheapHashes) {
        if (mapPartialTxHash.count(cheaphash) == 0)
            missing_tx.setCheapHashesToRequest.insert(cheaphash);
    }
    if (missing_tx.setCheapHashesToRequest.size() > 0) {
        LOG(WB, "Failed to reconstruct from graphene set as %d transactions are missing still.\n",
            missing_tx.setCheapHashesToRequest.size());
        missing_tx.blockhash = header.GetHash();
        return true;
    }

    // ok we should have everything here from Graphene reconstruction now
    std::map<uint256, CTransactionRef> delta_map;
    for (auto txr : delta_tx_additional)
        delta_map[txr->GetHash()] = txr;

    READLOCK(orphanpool.cs);
    std::vector<CTransactionRef> delta_tx;
    for (auto cheaphash : deltaCheapHashes) {
        const uint256& hash = mapPartialTxHash[cheaphash];
        CTransactionRef txr = getTXFromWherever(hash, delta_map);
        if (txr == nullptr) {
            LOG(WB, "Failed to reconstruct delta block as transaction %s went missing in the meantime.\n",
                hash.GetHex());
            missing_tx.blockhash = header.GetHash();
            return true;
        }
        delta_tx.emplace_back(txr);
    }

    dbr = CDeltaBlockRef(new CDeltaBlock(header, delta_tx_additional[0]));
    dbr->tryMakeComplete(delta_tx);
    LOG(WB, "Reconstructed delta block has all txn: %d\n", dbr->allTransactionsKnown());
    LOG(WB, "Reconstructed (from graphene-slimmed set) delta block max depth: %d, for size: %d\n",
                dbr->treeMaxDepth(), dbr->numTransactions());
    if (!dbr->allTransactionsKnown()) return false;
    return true;
}

typedef CNetDeltaRequestMissing CNDRMT;
typedef CNetDeltaBlock CNDB;

/*! Checks whether a delta block's parent hash is recent enough to be considered for further processing.
 */
bool IsRecentDeltaBlock(const uint256& prevhash) {
    LOCK(cs_main); // FIXME: necessary?
    return CDeltaBlock::knownStrong(prevhash);
}

bool sendFullDeltaBlock(ConstCDeltaBlockRef db, CNode* pto) {
    // FIXME: Approximate receiver mempool tx number with own value which is inaccurate. But by how much?
    LOG(WB, "Sending full delta block %s (complete delta set) to node %s.\n",
        db->GetHash().GetHex(), pto->GetLogName());
    CNetDeltaBlock ndb(db, GetGrapheneMempoolInfo().nTx);

    // needs no graphene info as it is basically a full delta block
    delete ndb.delta_gset;
    ndb.delta_gset = nullptr;
    ndb.delta_tx_additional.clear();
    ndb.delta_tx_additional.emplace_back(db->coinbase());
    for (auto& txref : db->deltaSet())
        ndb.delta_tx_additional.emplace_back(txref);
    pto->PushMessage(NetMsgType::DELTABLOCK, ndb);
    return true;
}

bool sendDeltaBlock(ConstCDeltaBlockRef db, CNode* pto, std::set<uint64_t> requestedCheapHashes) {
    std::map<uint64_t, uint256> mapMissingTx = getPartialTxHashesFromAllSources(shorttxidk1,
                                                                                shorttxidk2,
                                                                                db->deltaSet(),
                                                                                db->coinbase());
    // FIXME: Again, approximate receiver mempool tx number with own value which is inaccurate. But by how much?
    CNetDeltaBlock ndb(db, GetGrapheneMempoolInfo().nTx);

    // delta_tx_additional always contains the coinbase first
    ndb.delta_tx_additional.emplace_back(db->coinbase());
    LOG(WB, "Adding coinbase %s to set of included txn.\n", db->coinbase()->GetHash().GetHex());
    std::map<uint256, CTransactionRef> delta_map;
    for (auto txr : db->deltaSet())
        delta_map[txr->GetHash()] = txr;

    for (auto cheaphash : requestedCheapHashes) {
        if (mapMissingTx.count(cheaphash) == 0) {
            /* FIXME: How can this happen at all?! */
            LOG(WB, "Got a DBMISSTX message for block %s that refers to transaction with cheap hash "
                "%u which I don't know anything about. Sending complete delta set.\n",
                db->GetHash().GetHex(), cheaphash);
            return false;
        }
        const uint256 hash = mapMissingTx[cheaphash];
        CTransactionRef txref = getTXFromWherever(hash, delta_map);
        if (!txref->IsCoinBase()) {
            ndb.delta_tx_additional.emplace_back(txref);
            LOG(WB, "Adding transaction %s (cheap %ull) to set of included txn.\n",
                txref->GetHash().GetHex(), cheaphash);
        } else {
            LOG(WB, "Skipping coinbase %s (cheap %ull).\n", txref->GetHash().GetHex(), cheaphash);
        }
    }
    LOG(WB, "Sending graphene-slimmed delta block %s (%d additional) to node %s.\n",
        db->GetHash().GetHex(), ndb.delta_tx_additional.size(), pto->GetLogName());
    pto->PushMessage(NetMsgType::DELTABLOCK, ndb);
    return true;
}

bool CNetDeltaRequestMissing::HandleMessage(CDataStream& vRecv, CNode* pfrom) {
    CNDRMT req;
    vRecv >> req;
    ConstCDeltaBlockRef db = CDeltaBlock::byHash(req.blockhash);
    LOG(WB, "Got DBMISSTX for delta block %s", req.blockhash.GetHex());
    if (db == nullptr) {
        LOG(WB, "Got a DBMISSTX message for delta block %s, which is unknown to me.\n",
            req.blockhash.GetHex());
        return false;
    }

    // ok the peer seems to be in valid need of a delta block.
    if (!req.setCheapHashesToRequest.size()) {
        LOG(WB, "DBMISSTX message requests full block.\n");
        // the request has set no missing transactions which is deemed
        // to be a request for the full block including all
        // transactions.

        return sendFullDeltaBlock(db, pfrom);
    } else {
        LOG(WB, "DBMISSTX message requests %d additional transactions.\n", req.setCheapHashesToRequest.size());
        bool success = sendDeltaBlock(db, pfrom, req.setCheapHashesToRequest);
        if (!success) return sendFullDeltaBlock(db, pfrom);
        return success;
    }
}

bool CNetDeltaBlock::HandleMessage(CDataStream& vRecv, CNode* pfrom) {
    CNetDeltaBlock ndb;
    vRecv >> ndb;
    const uint256 hash = ndb.header.GetHash();

    if (pfrom != nullptr)
        LOG(WB, "Got a deltablock with hash %s, nBits: %d.\n", hash.GetHex(), ndb.header.nBits);
    else
        LOG(WB, "Retrying deltablock with hash %s.\n", hash.GetHex());

    // Let's see whether we know this one already
    const bool known = CDeltaBlock::byHash(hash) != nullptr;
    if (known) {
        if (pfrom != nullptr) LOG(WB, "DELTABLOCK is known already. Ignoring.\n");
        return false;
    }

    {
        /*! Make sure current tip is known to DB subsystem. */
        LOCK(cs_main);
        uint256 strong_tip_hash = chainActive.Tip()->GetBlockHash();
        if (!CDeltaBlock::knownStrong(strong_tip_hash)) {
            LOG(WB, "Delta blocks subsystem doesn't know about current tip yet.\n");
            CDeltaBlock::newStrong(strong_tip_hash);
        }
    }

    if (pfrom != nullptr) {
        // Avoid spamming w/o any POW effort
        if (!CheckProofOfWork(ndb.header.GetHash(), weakPOWfromPOW(ndb.header.nBits),
                              Params().GetConsensus(), true)) {
            LOG(WB, "Net Delta block failed early WPOW check. Ignoring.\n");
            return false;
        }

        //for (auto txr : ndb.delta_tx_additional)
        //    LOG(WB, "Included TX: %s\n", txr->GetHash().GetHex());


        LOG(WB, "DELTABLOCK not known yet.\n");
        if (!IsRecentDeltaBlock(ndb.header.hashPrevBlock)) {
            LOG(WB, "Delta block's parent hash %s is not recent enough (or even known) to be worth considering.\n",
                ndb.header.hashPrevBlock.GetHex());
            return false;
        }
        if (ndb.delta_tx_additional.size() < 1) {
            LOG(WB, "Malformed DELTABLOCK without coinbase received. Ignoring.\n");
            return false;
        }
    }
    CDeltaBlockRef db(new CDeltaBlock(ndb.header, ndb.delta_tx_additional[0]));

    bool missing_anc = false;

    for (auto h : db->deltaParentHashes()) {
        if (CDeltaBlock::byHash(h) == nullptr) {
            LOG(WB, "Ancestor %s missing.\n", h.GetHex());
            if (pfrom != nullptr) {
                CNetDeltaRequestMissing reqanc;
                reqanc.blockhash = h;
                pfrom->PushMessage(NetMsgType::DBMISSTX, reqanc);
            }
            {
                LOCK(cs_db);
                anc_missing[h].emplace_back(ndb);
            }
            missing_anc=true;
        }
    }
    if (missing_anc) {
        return false;
    }
    CNetDeltaRequestMissing* missing = nullptr;
    const bool reconstruct_result = ndb.reconstruct(db, &missing);
    if (!reconstruct_result) {
        LOG(WB, "Deltablock cannot be reconstructed.\n");
        return false;
    }
    if (missing == nullptr) {
        LOG(WB, "Internal error, no missing transactions filled.\n");
        return false;
    }
    if (! missing->blockhash.IsNull()) {
        if (missing->setCheapHashesToRequest.size() > 0) {
            LOG(WB, "Some %d transaction(s) missing still. Rerequesting.\n", missing->setCheapHashesToRequest.size());
        } else {
            LOG(WB, "Reconstruction failed for other reasons - rerequesting full delta block.\n");
        }
        if (pfrom != nullptr) {
            pfrom->PushMessage(NetMsgType::DBMISSTX, *missing);
        } else {
            LOG(WB, "This happened during reconstruction - querying all peers.\n");
            LOCK(cs_vNodes);
            for (CNode* pto : vNodes)
            if (pto->successfullyConnected())
                pto->PushMessage(NetMsgType::DBMISSTX, *missing);
        }
        delete missing;

        // we know a bit about the block now
        return true;
    }

    if (!db->allTransactionsKnown()) {
        LOG(WB, "Reconstruction of delta block failed.\n");
        return false;
    }

    processNew(db, pfrom);

    std::vector<CNetDeltaBlock> retry;
    {
        LOCK(cs_db);
        retry = anc_missing[hash];
        anc_missing.erase(hash);
    }
    for (auto x : retry) {
        CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
        s << x;
        CNetDeltaBlock::HandleMessage(s, nullptr);
    }
    return true;

    // FIXME: The whole situation of a node feeding us wrong delta
    // blocks data needs to be dealt with properly and lead to
    // banning!
    }


void CNetDeltaBlock::processNew(CDeltaBlockRef dbr, CNode *pfrom) {
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
    DbgAssert(dbr->allTransactionsKnown(), return);
    {
        LOCK(cs_db);
        CDeltaBlock::tryRegister(dbr);
        if (CDeltaBlock::byHash(dbr->GetHash()) == nullptr) {
            LOG(WB, "Delta block %s failed to register. Dropping it.\n", dbr->GetHash().GetHex());
            return;
        }
        LOG(WB, "Delta block %s successfully checked for WPOW, validity and registered.\n",
            dbr->GetHash().GetHex());
    }
    // FIXME: Do not send it to the peer(s) it has been received from!
    {
        LOCK(cs_vNodes);
        for (CNode* pto : vNodes)
            if (pto != pfrom && pto->successfullyConnected())
                sendDeltaBlock(dbr, pto, std::set<uint64_t>());
    }

    dbr->fXVal = true;
    // if it is a strong block, process it as such as well
    // FIXME: copy'n'paste from unlimited.cpp
    if (dbr->isStrong()) {
        PV->StopAllValidationThreads(dbr->nBits);
        if (!ProcessNewBlock(state, Params() , nullptr, dbr.get(), true, nullptr, false)) {
            LOG(WB, "Delta block that is strong block has not been accepted!\n");
            return;
        }
    }

}
