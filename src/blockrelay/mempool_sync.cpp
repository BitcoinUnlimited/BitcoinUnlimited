// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/mempool_sync.h"
#include "connmgr.h"
#include "dosman.h"
#include "net.h"
#include "nodestate.h"
#include "txadmission.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "util.h"
#include "utiltime.h"
#include "xversionkeys.h"

#include <random>

extern CTxMemPool mempool;
extern CTweak<bool> syncMempoolWithPeers;
extern CTweak<uint64_t> mempoolSyncMinVersionSupported;
extern CTweak<uint64_t> mempoolSyncMaxVersionSupported;
extern CCriticalSection cs_mempoolsync;
extern uint64_t lastMempoolSyncClear;

CMempoolSyncInfo::CMempoolSyncInfo(uint64_t _nTxInMempool,
    uint64_t _nRemainingMempoolBytes,
    uint64_t _shorttxidk0,
    uint64_t _shorttxidk1,
    uint64_t _nSatoshiPerK)
    : nTxInMempool(_nTxInMempool), nRemainingMempoolBytes(_nRemainingMempoolBytes), shorttxidk0(_shorttxidk0),
      shorttxidk1(_shorttxidk1), nSatoshiPerK(_nSatoshiPerK)
{
}
CMempoolSyncInfo::CMempoolSyncInfo()
{
    this->nTxInMempool = 0;
    this->nRemainingMempoolBytes = 0;
    this->shorttxidk0 = 0;
    this->shorttxidk1 = 0;
    this->nSatoshiPerK = 0;
}

CMempoolSync::CMempoolSync(std::vector<uint256> mempoolTxHashes,
    uint64_t nReceiverMemPoolTx,
    uint64_t nSenderMempoolPlusBlock,
    uint64_t shorttxidk0,
    uint64_t shorttxidk1,
    uint64_t _version)
{
    uint64_t grapheneSetVersion = CMempoolSync::GetGrapheneSetVersion(version);
    version = _version;
    nSenderMempoolTxs = mempoolTxHashes.size();

    pGrapheneSet = std::make_shared<CGrapheneSet>(CGrapheneSet(nReceiverMemPoolTx, nSenderMempoolPlusBlock,
        mempoolTxHashes, shorttxidk0, shorttxidk1, grapheneSetVersion, IBLT_ENTROPY, COMPUTE_OPTIMIZED, false));
}

CMempoolSync::~CMempoolSync() { pGrapheneSet = nullptr; }
bool HandleMempoolSyncRequest(CDataStream &vRecv, CNode *pfrom)
{
    LOG(MPOOLSYNC, "Handling mempool sync request from peer %s\n", pfrom->GetLogName());
    CMempoolSyncInfo mempoolinfo;
    vRecv >> mempoolinfo;

    NodeId nodeId = pfrom->GetId();

    // Requester should only contact peers that support mempool sync
    if (!syncMempoolWithPeers.Value())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Mempool sync requested from peer %s but not supported\n", pfrom->GetLogName());
    }

    // Requester must limit request frequency
    {
        LOCK(cs_mempoolsync);

        if (mempoolSyncResponded.count(nodeId) > 0 &&
            ((GetStopwatchMicros() - mempoolSyncResponded[nodeId].lastUpdated) <
                MEMPOOLSYNC_FREQ_US - MEMPOOLSYNC_FREQ_GRACE_US))
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("Mempool sync requested less than %d mu seconds ago from peer %s\n", MEMPOOLSYNC_FREQ_US,
                pfrom->GetLogName());
        }

        // Record request
        mempoolSyncResponded[nodeId] =
            CMempoolSyncState(GetStopwatchMicros(), mempoolinfo.shorttxidk0, mempoolinfo.shorttxidk1, false);
    }

    LOG(MPOOLSYNC, "Mempool currently holds %d transactions\n", mempool.size());

    std::vector<uint256> mempoolTxHashes;
    // cycle through mempool txs in order of ancestor_score
    {
        READLOCK(mempool.cs_txmempool);

        int64_t nRemainingMempoolBytes = mempoolinfo.nRemainingMempoolBytes;
        typename CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator it =
            mempool.mapTx.get<ancestor_score>().begin();
        for (; it != mempool.mapTx.get<ancestor_score>().end() && nRemainingMempoolBytes > 0; ++it)
        {
            size_t nTxSize = it->GetTx().GetTxSize();
            int64_t nFee = it->GetFee();
            CFeeRate feeRate(nFee, nTxSize);

            // Skip tx if fee rate is too low
            if (feeRate.GetFeePerK() < (int)mempoolinfo.nSatoshiPerK)
                continue;

            mempoolTxHashes.push_back(it->GetTx().GetHash());
            nRemainingMempoolBytes -= nTxSize;
        }
    }

    if (mempoolTxHashes.size() == 0)
    {
        LOG(MPOOLSYNC, "Mempool is empty; aborting mempool sync with peer %s\n", pfrom->GetLogName());
        return true;
    }

    // Assemble mempool sync object
    CMempoolSync mempoolSync(mempoolTxHashes, mempoolinfo.nTxInMempool, mempoolTxHashes.size(), mempoolinfo.shorttxidk0,
        mempoolinfo.shorttxidk1, NegotiateMempoolSyncVersion(pfrom));

    pfrom->PushMessage(NetMsgType::MEMPOOLSYNC, mempoolSync);
    LOG(MPOOLSYNC, "Sent mempool sync to peer %s using version %d\n", pfrom->GetLogName(), mempoolSync.version);

    return true;
}

/**
 * Handle an incoming mempool synchronization payload
 */
bool CMempoolSync::ReceiveMempoolSync(CDataStream &vRecv, CNode *pfrom, std::string strCommand)
{
    // Deserialize mempool sync payload
    CMempoolSync mempoolSync;
    vRecv >> mempoolSync;
    NodeId nodeId = pfrom->GetId();

    LOG(MPOOLSYNC, "Received mempool sync from peer %s\n", pfrom->GetLogName());

    // Do not process unrequested mempool sync.
    {
        LOCK(cs_mempoolsync);

        if (!(mempoolSyncRequested.count(nodeId) == 1))
        {
            dosMan.Misbehaving(pfrom, 10);
            return error("Received unrequested mempool sync from peer %s\n", pfrom->GetLogName());
        }

        // Do not proceed if this request has already been processed
        if (mempoolSyncRequested[nodeId].completed)
        {
            dosMan.Misbehaving(pfrom, 100);
            return error(
                "Received mempool sync from peer %s but synchronization has already completed", pfrom->GetLogName());
        }
    }

    return mempoolSync.process(pfrom);
}

bool CMempoolSync::process(CNode *pfrom)
{
    NodeId nodeId = pfrom->GetId();
    std::set<uint256> passingTxHashes;
    std::map<uint64_t, uint256> mapPartialTxHash;
    std::set<uint64_t> setHashesToRequest;

    std::vector<uint256> mempoolTxHashes;
    GetMempoolTxHashes(mempoolTxHashes);

    // Collect cheap hashes
    {
        LOCK(cs_mempoolsync);
        for (const uint256 &hash : mempoolTxHashes)
        {
            uint64_t cheapHash = GetShortID(mempoolSyncRequested[nodeId].shorttxidk0,
                mempoolSyncRequested[nodeId].shorttxidk1, hash, SHORT_ID_VERSION);
            mapPartialTxHash.insert(std::make_pair(cheapHash, hash));
        }
    }

    try
    {
        std::vector<uint64_t> mempoolCheapHashes = pGrapheneSet->Reconcile(mapPartialTxHash);

        // Sort out what hashes we have from the complete set of cheapHashes
        for (uint64_t cheapHash : mempoolCheapHashes)
        {
            const auto &elem = mapPartialTxHash.find(cheapHash);
            if (elem == mapPartialTxHash.end())
                setHashesToRequest.insert(cheapHash);
        }
    }
    catch (const std::runtime_error &e)
    {
        LOG(MPOOLSYNC, "Mempool sync failed for peer %s. Graphene set could not be reconciled: %s\n",
            pfrom->GetLogName(), e.what());

        return false;
    }

    LOG(MPOOLSYNC, "Mempool sync received: %d total responder txns, requester waiting for %d txs from peer %s\n",
        nSenderMempoolTxs, setHashesToRequest.size(), pfrom->GetLogName());

    // If there are any missing transactions then we request them here.
    if (!setHashesToRequest.empty())
    {
        CRequestMempoolSyncTx mempoolSyncTx(setHashesToRequest);
        pfrom->PushMessage(NetMsgType::GET_MEMPOOLSYNCTX, mempoolSyncTx);
        LOG(MPOOLSYNC, "Requesting to sync %d missing transactions from %s\n", setHashesToRequest.size(),
            pfrom->GetLogName());

        return true;
    }

    // If there are no transactions to request, then synchronization is complete
    {
        LOCK(cs_mempoolsync);
        mempoolSyncRequested[nodeId].completed = true;
    }

    LOG(MPOOLSYNC, "Completeing mempool sync with %s; no missing transactions\n", pfrom->GetLogName());
    return true;
}

bool CRequestMempoolSyncTx::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    CRequestMempoolSyncTx reqMempoolSyncTx;
    vRecv >> reqMempoolSyncTx;
    NodeId nodeId = pfrom->GetId();

    // Message consistency checking
    if (reqMempoolSyncTx.setCheapHashesToRequest.empty())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Incorrectly constructed getmemsynctx received.  Banning peer=%s", pfrom->GetLogName());
    }

    // A response was received for a request that was not made
    std::vector<CTransactionRef> vTx;
    {
        LOCK(cs_mempoolsync);

        if (mempoolSyncResponded.count(nodeId) == 0)
        {
            dosMan.Misbehaving(pfrom, 10);
            return error("Received getmemsynctx from peer %s but mempool sync is not in progress", pfrom->GetLogName());
        }

        // Already processed requested transactions
        if (mempoolSyncResponded[nodeId].completed)
        {
            dosMan.Misbehaving(pfrom, 100);
            return error(
                "Received getmemsynctx from peer %s but mempool sync has already completed", pfrom->GetLogName());
        }

        LOG(MPOOLSYNC, "Received getmemsynctx from peer=%s requesting %d transactions\n", pfrom->GetLogName(),
            reqMempoolSyncTx.setCheapHashesToRequest.size());

        std::vector<uint256> mempoolTxHashes;
        GetMempoolTxHashes(mempoolTxHashes);

        // Locate transactions requested
        // Note that only those still in the mempool will be located
        for (auto &hash : mempoolTxHashes)
        {
            uint64_t cheapHash = GetShortID(mempoolSyncResponded[nodeId].shorttxidk0,
                mempoolSyncResponded[nodeId].shorttxidk1, hash, SHORT_ID_VERSION);

            if (reqMempoolSyncTx.setCheapHashesToRequest.count(cheapHash) == 0)
                continue;

            // Check mempool first
            auto txRef = mempool.get(hash);
            if (txRef != nullptr)
                vTx.push_back(txRef);

            // Then CommitQ
            txRef = CommitQGet(hash);
            if (txRef != nullptr)
                vTx.push_back(txRef);

            // Finally orphanpool
            {
                READLOCK(orphanpool.cs_orphanpool);

                std::map<uint256, CTxOrphanPool::COrphanTx>::iterator iter =
                    orphanpool.mapOrphanTransactions.find(hash);
                if (iter != orphanpool.mapOrphanTransactions.end())
                {
                    vTx.push_back(iter->second.ptx);
                }
            }
        }
    }

    LOG(MPOOLSYNC, "Sending %d mempool sync transactions to peer=%s\n", vTx.size(), pfrom->GetLogName());

    // Assemble missing transaction object
    CMempoolSyncTx mempoolSyncTx(vTx);
    pfrom->PushMessage(NetMsgType::MEMPOOLSYNCTX, mempoolSyncTx);

    // We should not receive any future messages related to this synchronization round
    {
        LOCK(cs_mempoolsync);
        mempoolSyncResponded[nodeId].completed = true;
    }

    return true;
}

bool CMempoolSyncTx::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    std::string strCommand = NetMsgType::MEMPOOLSYNCTX;
    CMempoolSyncTx mempoolSyncTx;
    vRecv >> mempoolSyncTx;
    NodeId nodeId = pfrom->GetId();

    {
        LOCK(cs_mempoolsync);

        // Do not process unrequested memsynctx.
        if (mempoolSyncRequested.count(nodeId) == 0)
        {
            dosMan.Misbehaving(pfrom, 10);
            return error("Received memsynctx from peer %s but mempool sync is not in progress", pfrom->GetLogName());
        }

        // Already received requested transactions
        if (mempoolSyncRequested[nodeId].completed)
        {
            dosMan.Misbehaving(pfrom, 100);
            return error(
                "Received memsynctx from peer %s but transactions have already been sent", pfrom->GetLogName());
        }
    }

    LOG(MPOOLSYNC, "Received memsynctx from peer=%s; adding %d transactions to mempool\n", pfrom->GetLogName(),
        mempoolSyncTx.vTx.size());

    // Add transactions to mempool
    for (const auto &tx : mempoolSyncTx.vTx)
    {
        CTxInputData inputData;
        inputData.tx = tx;
        inputData.nodeId = pfrom->id;
        EnqueueTxForAdmission(inputData);
    }

    LOG(MPOOLSYNC, "Recovered %d txs from peer=%s via mempool sync\n", mempoolSyncTx.vTx.size(), pfrom->GetLogName());

    // We should not receive any future messages related to this round of synchronization
    {
        LOCK(cs_mempoolsync);

        mempoolSyncRequested[nodeId].completed = true;
    }

    return true;
}

void GetMempoolTxHashes(std::vector<uint256> &mempoolTxHashes)
{
    {
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        for (auto &kv : *txCommitQ)
        {
            mempoolTxHashes.push_back(kv.first);
        }
    }

    {
        READLOCK(orphanpool.cs_orphanpool);
        for (auto &kv : orphanpool.mapOrphanTransactions)
        {
            mempoolTxHashes.push_back(kv.first);
        }
    }

    std::vector<uint256> memPoolHashes;
    mempool.queryHashes(memPoolHashes);

    for (const uint256 &hash : memPoolHashes)
    {
        mempoolTxHashes.push_back(hash);
    }
}

CMempoolSyncInfo GetMempoolSyncInfo()
{
    // We need the number of transactions in the mempool and orphanpools but also the number
    // in the txCommitQ that have been processed and valid, and which will be in the mempool shortly.
    uint64_t nCommitQ = 0;
    {
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        nCommitQ = txCommitQ->size();
    }

    uint64_t nTxInMempool = mempool.size() + orphanpool.GetOrphanPoolSize() + nCommitQ;
    uint64_t nMempoolMaxTxBytes = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    uint64_t nSatoshiPerK = minRelayTxFee.GetFeePerK();

    // Form SipHash keys
    uint64_t seed = GetRand(std::numeric_limits<uint64_t>::max());
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << seed;
    CSHA256 hasher;
    hasher.Write((unsigned char *)&(*stream.begin()), stream.end() - stream.begin());
    uint256 shorttxidhash;
    hasher.Finalize(shorttxidhash.begin());
    uint64_t shorttxidk0 = shorttxidhash.GetUint64(0);
    uint64_t shorttxidk1 = shorttxidhash.GetUint64(1);

    // Calculate how many bytes of space remain in the mempool
    uint64_t nRemainingMempoolTxBytes = nMempoolMaxTxBytes;
    {
        READLOCK(mempool.cs_txmempool);
        for (const CTxMemPoolEntry &e : mempool.mapTx)
        {
            nRemainingMempoolTxBytes += e.GetTx().GetTxSize();
        }
    }

    return CMempoolSyncInfo(nTxInMempool, nRemainingMempoolTxBytes, shorttxidk0, shorttxidk1, nSatoshiPerK);
}

uint64_t NegotiateMempoolSyncVersion(CNode *pfrom)
{
    uint64_t peerMin = pfrom->nMempoolSyncMinVersionSupported;
    uint64_t selfMin = mempoolSyncMinVersionSupported.Value();
    uint64_t peerMax = pfrom->nMempoolSyncMaxVersionSupported;
    uint64_t selfMax = mempoolSyncMaxVersionSupported.Value();

    uint64_t upper = (uint64_t)std::min(peerMax, selfMax);
    uint64_t lower = (uint64_t)std::max(peerMin, selfMin);

    if (lower > upper)
        throw std::runtime_error("Sender and receiver support incompatible mempool sync versions");

    return upper;
}

CNode *SelectMempoolSyncPeer(std::vector<CNode *> vNodesCopy)
{
    std::vector<CNode *> vSyncableNodes;

    for (auto node : vNodesCopy)
    {
        // Skip if mempool sync is not supported
        if (!node->canSyncMempoolWithPeers)
            continue;

        // Skip if version cannot be negotiated
        try
        {
            NegotiateMempoolSyncVersion(node);
        }
        catch (std::runtime_error &e)
        {
            continue;
        }

        CNodeStateAccessor state(nodestate, node->GetId());
        int nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
        int nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;

        // Skip if node is in IBD
        if ((nCommonHeight < chainActive.Tip()->nHeight - 10) && (nSyncHeight < chainActive.Tip()->nHeight - 10))
            continue;

        vSyncableNodes.push_back(node);
    }

    // Randomly select node with whom to request mempoolsync
    if (vSyncableNodes.size() > 0)
        return vSyncableNodes[GetRandInt(vSyncableNodes.size())];
    else
        return nullptr;
}

void ClearDisconnectedFromMempoolSyncMaps(NodeId nodeid)
{
    LOCK(cs_mempoolsync);
    mempoolSyncRequested.erase(nodeid);
    mempoolSyncResponded.erase(nodeid);
}
