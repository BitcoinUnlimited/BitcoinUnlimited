// Copyright (c) 2016-2021 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "requestManager.h"
#include "blockrelay/blockrelay_common.h"
#include "blockrelay/compactblock.h"
#include "blockrelay/graphene.h"
#include "blockrelay/mempool_sync.h"
#include "blockrelay/thinblock.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "dosman.h"
#include "extversionkeys.h"
#include "leakybucket.h"
#include "main.h"
#include "net.h"
#include "nodestate.h"
#include "parallel.h"
#include "primitives/block.h"
#include "rpc/server.h"
#include "stat.h"
#include "tinyformat.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "unlimited.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "validation/validation.h"
#include "validationinterface.h"
#include "version.h"
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/lexical_cast.hpp>
#include <inttypes.h>
#include <thread>


using namespace std;

extern CTweak<unsigned int> maxBlocksInTransitPerPeer;
extern CTweak<unsigned int> blockDownloadWindow;
extern CTweak<unsigned int> blockLookAheadInterval;

// Request management
extern CRequestManager requester;

// Any ping < 25 ms is good
unsigned int ACCEPTABLE_PING_USEC = 25 * 1000;

// When should I request an object from someone else (in microseconds)
unsigned int MIN_TX_REQUEST_RETRY_INTERVAL = DEFAULT_MIN_TX_REQUEST_RETRY_INTERVAL;
unsigned int txReqRetryInterval = MIN_TX_REQUEST_RETRY_INTERVAL;
// When should I request a block from someone else (in microseconds)
unsigned int MIN_BLK_REQUEST_RETRY_INTERVAL = DEFAULT_MIN_BLK_REQUEST_RETRY_INTERVAL;
unsigned int blkReqRetryInterval = MIN_BLK_REQUEST_RETRY_INTERVAL;

// defined in main.cpp.  should be moved into a utilities file but want to make rebasing easier
extern bool CanDirectFetch(const Consensus::Params &consensusParams);


static bool IsBlockType(const CInv &obj)
{
    return ((obj.type == MSG_BLOCK) || (obj.type == MSG_CMPCT_BLOCK) || (obj.type == MSG_XTHINBLOCK) ||
            (obj.type == MSG_GRAPHENEBLOCK));
}

// Constructor for CRequestManagerNodeState struct
CRequestManagerNodeState::CRequestManagerNodeState()
{
    nDownloadingSince = 0;
    nBlocksInFlight = 0;
    nNumRequests = 0;
    nLastRequest = 0;
}

CRequestManager::CRequestManager()
    : inFlightTxns("reqMgr/inFlight", STAT_OP_MAX), receivedTxns("reqMgr/received"), rejectedTxns("reqMgr/rejected"),
      droppedTxns("reqMgr/dropped", STAT_KEEP), pendingTxns("reqMgr/pending", STAT_KEEP),
      requestPacer(15000, 10000) // Max and average # of requests that can be made per second
{
    inFlight = 0;
    nOutbound = 0;

    sendIter = mapTxnInfo.end();
    sendBlkIter = mapBlkInfo.end();
}

void CRequestManager::Cleanup()
{
    LOCK(cs_objDownloader);
    sendIter = mapTxnInfo.end();
    sendBlkIter = mapBlkInfo.end();
    MapBlocksInFlightClear();
    OdMap::iterator i = mapTxnInfo.begin();
    while (i != mapTxnInfo.end())
    {
        auto prev = i;
        ++i;
        cleanup(prev); // cleanup erases which is why I need to advance the iterator first
    }

    i = mapBlkInfo.begin();
    while (i != mapBlkInfo.end())
    {
        auto prev = i;
        ++i;
        cleanup(prev); // cleanup erases which is why I need to advance the iterator first
    }
}

void CRequestManager::cleanup(OdMap::iterator &itemIt)
{
    LOCK(cs_objDownloader);
    CUnknownObj &item = itemIt->second;
    // Because we'll ignore anything deleted from the map, reduce the # of requests in flight by every request we made
    // for this object
    inFlight -= item.outstandingReqs;
    droppedTxns -= (item.outstandingReqs - 1);
    pendingTxns -= 1;

    // remove all the source nodes
    item.availableFrom.clear();

    if (item.obj.type == MSG_TX)
    {
        if (sendIter == itemIt)
            ++sendIter;
        mapTxnInfo.erase(itemIt);
    }
    else
    {
        if (sendBlkIter == itemIt)
            ++sendBlkIter;
        mapBlkInfo.erase(itemIt);
    }
}

// Get this object from somewhere, asynchronously.
void CRequestManager::AskFor(const CInv &obj, CNode *from, unsigned int priority)
{
    // LOG(REQ, "ReqMgr: Ask for %s.\n", obj.ToString().c_str());

    LOCK(cs_objDownloader);
    if (obj.type == MSG_TX)
    {
        // Don't allow the in flight requests to grow unbounded.
        if (mapTxnInfo.size() >= (size_t)(MAX_INV_SZ * 2 * GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE)))
        {
            LOG(REQ, "Tx request buffer full: Dropping request for %s", obj.hash.ToString());
            return;
        }

        uint256 temp = obj.hash;
        OdMap::value_type v(temp, CUnknownObj());
        std::pair<OdMap::iterator, bool> result = mapTxnInfo.insert(v);
        OdMap::iterator &item = result.first;
        CUnknownObj &data = item->second;
        data.obj = obj;
        if (result.second) // inserted
        {
            pendingTxns += 1;
            // all other fields are zeroed on creation
        }
        // else the txn already existed so nothing to do

        data.priority = max(priority, data.priority);

        // Got the data, now add the node as a source if we're not already processing
        // this txn. If we add more sources here while processing a txn then we could
        // end up with dangling noderefs when the peer tries to disconnect.
        if (!data.fProcessing)
            data.AddSource(from);
        else
        {
            LOG(REQ, "Not calling AddSource for %s at %s.  Already processing.\n", obj.ToString(), from->GetLogName());
        }
    }
    else if (IsBlockType(obj))
    {
        uint256 temp = obj.hash;
        OdMap::value_type v(temp, CUnknownObj());
        std::pair<OdMap::iterator, bool> result = mapBlkInfo.insert(v);
        OdMap::iterator &item = result.first;
        CUnknownObj &data = item->second;
        data.obj = obj;
        // if (result.second)  // means this was inserted rather than already existed
        // { } nothing to do
        data.priority = max(priority, data.priority);
        if (data.AddSource(from))
        {
            // LOG(BLK, "%s available at %s\n", obj.ToString().c_str(), from->addrName.c_str());
        }
    }
    else
    {
        DbgAssert(!"Request manager does not handle objects of this type", return );
    }
}

// Get these objects from somewhere, asynchronously.
void CRequestManager::AskFor(const std::vector<CInv> &objArray, CNode *from, unsigned int priority)
{
    // In order to maintain locking order, we must lock cs_objDownloader first and before possibly taking cs_vNodes.
    // Also, locking here prevents anyone from asking again for any of these objects again before we've notified the
    // request manager of them all. In addition this helps keep blocks batached and requests for batches of blocks
    // in a better order.
    LOCK(cs_objDownloader);
    for (auto &inv : objArray)
    {
        AskFor(inv, from, priority);
    }
}

void CRequestManager::AskForDuringIBD(const std::vector<CInv> &objArray, CNode *from, unsigned int priority)
{
    // This is block and peer that was selected in FindNextBlocksToDownload() so we want to add it as a block
    // source first so that it gets requested first.
    if (from)
        AskFor(objArray, from, priority);

    // We can't hold cs_vNodes in the for loop below because it is out of order with cs_objDownloader which is
    // taken in ProcessBlockAvailability.  We can't take cs_objDownloader earlier because it deadlocks with the
    // CNodeStateAccessor. So make a copy of vNodes here
    std::vector<CNode *> vNodesCopy;

    {
        LOCK(cs_vNodes);
        vNodesCopy = vNodes;
        for (CNode *pnode : vNodesCopy)
        {
            pnode->AddRef();
        }
    }


    // Add the other peers as potential sources in the event the RequestManager needs to make a re-request
    // for this block. Only add NETWORK nodes that have block availability.
    for (CNode *pnode : vNodesCopy)
    {
        // skip the peer we added above and skip non NETWORK nodes
        if ((pnode == from) || (pnode->fClient))
        {
            pnode->Release();
            continue;
        }

        // Make sure pindexBestKnownBlock is up to date.
        ProcessBlockAvailability(pnode->id);

        // check block availability for this peer and only askfor a block if it is available.
        CNodeStateAccessor state(nodestate, pnode->id);
        if (state != nullptr)
        {
            if (state->pindexBestKnownBlock != nullptr &&
                state->pindexBestKnownBlock->nChainWork > chainActive.Tip()->nChainWork)
            {
                AskFor(objArray, pnode, priority);
            }
        }
        pnode->Release(); // Release the refs we took
    }
}

bool CRequestManager::AlreadyAskedForBlock(const uint256 &hash)
{
    LOCK(cs_objDownloader);
    OdMap::iterator item = mapBlkInfo.find(hash);
    if (item != mapBlkInfo.end())
        return true;

    return false;
}

void CRequestManager::UpdateTxnResponseTime(const CInv &obj, CNode *pfrom)
{
    int64_t now = GetStopwatchMicros();
    LOCK(cs_objDownloader);
    if (pfrom && obj.type == MSG_TX)
    {
        OdMap::iterator item = mapTxnInfo.find(obj.hash);
        if (item == mapTxnInfo.end())
            return;

        pfrom->txReqLatency << (now - item->second.lastRequestTime);
        receivedTxns += 1;
    }
}

void CRequestManager::ProcessingTxn(const uint256 &hash, CNode *pfrom)
{
    LOCK(cs_objDownloader);
    OdMap::iterator item = mapTxnInfo.find(hash);
    if (item == mapTxnInfo.end())
        return;

    item->second.fProcessing = true;
    LOG(REQ, "ReqMgr: Processing %s (received from %s).\n", item->second.obj.ToString(),
        pfrom ? pfrom->GetLogName() : "unknown");

    // As a last step we must clear all sources to release the noderef's. If we don't do this
    // then if the transaction ends up being a double spend, an orphan that is never reclaimed, or
    // perhaps some other validation failure, it would result in having dangling noderef's which then
    // prevent a node from fully disconnecting and thus preventing the CNode from calling it's destructor.
    //
    // However in the case of blocks we don't do this because if a block fails to validate we
    // reset the fProcessing flag to false so that we can get another block and check its validity.
    // This is so that we can prevent a DOS attack where a corrupted block is fed to us in order
    // to prevent us from downloading the good block.
    item->second.availableFrom.clear();
}

void CRequestManager::ProcessingBlock(const uint256 &hash, CNode *pfrom)
{
    LOCK(cs_objDownloader);
    OdMap::iterator item = mapBlkInfo.find(hash);
    if (item == mapBlkInfo.end())
        return;

    item->second.fProcessing = true;
    LOG(BLK, "ReqMgr: Processing %s (received from %s).\n", item->second.obj.ToString(),
        pfrom ? pfrom->GetLogName() : "unknown");
}
// This block has failed to be accepted so in case this is some sort of attack block
// we need to set the fProcessing flag back to false.
//
// We don't have to remove the source because it would have already been removed if/when we
// requested the block and if this was an unsolicited block or attack block then the source
// would never have been added to the request manager.
void CRequestManager::BlockRejected(const CInv &obj, CNode *pfrom)
{
    LOCK(cs_objDownloader);
    OdMap::iterator item = mapBlkInfo.find(obj.hash);
    if (item == mapBlkInfo.end())
        return;
    item->second.fProcessing = false;
}

void CRequestManager::Downloading(const uint256 &hash, CNode *pfrom)
{
    LOCK(cs_objDownloader);
    OdMap::iterator item = mapBlkInfo.find(hash);
    if (item == mapBlkInfo.end())
        return;

    item->second.nDownloadingSince = GetStopwatchMicros();
    LOG(BLK, "ReqMgr: Downloading %s (received from %s).\n", item->second.obj.ToString(),
        pfrom ? pfrom->GetLogName() : "unknown");
}

// Indicate that we got this object.
void CRequestManager::Received(const CInv &obj, CNode *pfrom)
{
    LOCK(cs_objDownloader);
    if (obj.type == MSG_TX)
    {
        OdMap::iterator item = mapTxnInfo.find(obj.hash);
        if (item == mapTxnInfo.end())
            return;

        LOG(REQ, "ReqMgr: TX received for %s.\n", item->second.obj.ToString().c_str());
        cleanup(item);
    }
    else if (IsBlockType(obj))
    {
        OdMap::iterator item = mapBlkInfo.find(obj.hash);
        if (item == mapBlkInfo.end())
            return;

        LOG(BLK, "%s removed from request queue (received from %s).\n", item->second.obj.ToString().c_str(),
            pfrom ? pfrom->GetLogName() : "unknown");
        cleanup(item);
    }
}

// Indicate that we got this object.
void CRequestManager::AlreadyReceived(CNode *pnode, const CInv &obj)
{
    LOCK(cs_objDownloader);
    OdMap::iterator item = mapTxnInfo.find(obj.hash);
    if (item == mapTxnInfo.end())
    {
        item = mapBlkInfo.find(obj.hash);
        if (item == mapBlkInfo.end())
            return; // Not in any map
    }
    LOG(REQ, "ReqMgr: Already received %s.  Removing request.\n", item->second.obj.ToString().c_str());

    // If we have it already make sure to mark it as received here or we'll end up disconnecting this
    // peer later when we think this block download attempt has timed out.
    MarkBlockAsReceived(obj.hash, pnode);

    // will be decremented in the item cleanup: if (inFlight) inFlight--;
    cleanup(item); // remove the item
}

// Indicate that we got this object, from and bytes are optional (for node performance tracking)
void CRequestManager::Rejected(const CInv &obj, CNode *from, unsigned char reason)
{
    LOCK(cs_objDownloader);
    OdMap::iterator item;
    if (obj.type == MSG_TX)
    {
        item = mapTxnInfo.find(obj.hash);
        if (item == mapTxnInfo.end())
        {
            LOG(REQ, "ReqMgr: Item already removed. Unknown txn rejected %s\n", obj.ToString().c_str());
            return;
        }
        if (inFlight)
            inFlight--;
        if (item->second.outstandingReqs)
            item->second.outstandingReqs--;

        rejectedTxns += 1;
    }
    else if (IsBlockType(obj))
    {
        item = mapBlkInfo.find(obj.hash);
        if (item == mapBlkInfo.end())
        {
            LOG(REQ, "ReqMgr: Item already removed. Unknown block rejected %s\n", obj.ToString().c_str());
            return;
        }
    }

    if (reason == REJECT_MALFORMED)
    {
    }
    else if (reason == REJECT_INVALID)
    {
    }
    else if (reason == REJECT_OBSOLETE)
    {
    }
    else if (reason == REJECT_CHECKPOINT)
    {
    }
    else if (reason == REJECT_INSUFFICIENTFEE)
    {
        item->second.rateLimited = true;
    }
    else if (reason == REJECT_DUPLICATE)
    {
        // TODO figure out why this might happen.
    }
    else if (reason == REJECT_NONSTANDARD)
    {
        // Not going to be in any memory pools... does the TX request also look in blocks?
        // TODO remove from request manager (and mark never receivable?)
        // TODO verify that the TX request command also looks in blocks?
    }
    else if (reason == REJECT_DUST)
    {
    }
    else
    {
        LOG(REQ, "ReqMgr: Unknown TX rejection code [0x%x].\n", reason);
        // assert(0); // TODO
    }
}

CNodeRequestData::CNodeRequestData(CNodeRef n)
{
    noderef = n;
    requestCount = 0;
    desirability = 0;

    const int MaxLatency = 10 * 1000 * 1000; // After 10 seconds latency I don't care

    // Calculate how much we like this node:

    // Prefer thin block nodes over low latency ones when the chain is syncd
    if (noderef.get()->ThinBlockCapable() && IsChainNearlySyncd())
    {
        desirability += MaxLatency;
    }

    // The bigger the latency (in microseconds), the less we want to request from this node
    int latency = noderef.get()->txReqLatency.GetTotalTyped();
    // data has never been requested from this node.  Should we encourage investigation into whether this node is fast,
    // or stick with nodes that we do have data on?
    if (latency == 0)
    {
        latency = 80 * 1000; // assign it a reasonably average latency (80ms) for sorting purposes
    }
    if (latency > MaxLatency)
        latency = MaxLatency;
    desirability -= latency;
}

// requires cs_objDownloader
bool CUnknownObj::AddSource(CNode *from)
{
    // node is not in the request list
    if (std::find_if(availableFrom.begin(), availableFrom.end(), MatchCNodeRequestData(from)) == availableFrom.end())
    {
        LOG(REQ, "AddSource %s is available at %s.\n", obj.ToString(), from->GetLogName());

        CNodeRef noderef(from);
        CNodeRequestData req(noderef);
        for (ObjectSourceList::iterator i = availableFrom.begin(); i != availableFrom.end(); ++i)
        {
            if (i->desirability < req.desirability)
            {
                availableFrom.insert(i, req);
                return true;
            }
        }
        availableFrom.push_back(req);
        return true;
    }
    return false;
}

void CRequestManager::RequestCorruptedBlock(const uint256 &blockHash)
{
    // set it to MSG_BLOCK here but it should get overwritten in RequestBlock
    CInv obj(MSG_BLOCK, blockHash);
    std::vector<CInv> vGetBlocks;
    vGetBlocks.push_back(obj);
    AskForDuringIBD(vGetBlocks, nullptr);
}

static bool IsGrapheneVersionSupported(CNode *pfrom)
{
    try
    {
        NegotiateGrapheneVersion(pfrom);
        return true;
    }
    catch (const std::runtime_error &error)
    {
        return false;
    }
}

bool CRequestManager::RequestBlock(CNode *pfrom, CInv obj)
{
    CInv inv2(obj);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    if (IsChainNearlySyncd() &&
        (!thinrelay.HasBlockRelayTimerExpired(obj.hash) || !thinrelay.IsBlockRelayTimerEnabled()))
    {
        // Ask for Graphene blocks
        // Must download a graphene block from a graphene enabled peer.
        if (IsGrapheneBlockEnabled() && pfrom->GrapheneCapable() && IsGrapheneVersionSupported(pfrom))
        {
            if (thinrelay.AddBlockInFlight(pfrom, inv2.hash, NetMsgType::GRAPHENEBLOCK))
            {
                MarkBlockAsInFlight(pfrom->GetId(), obj.hash);

                // Instead of building a bloom filter here as we would for an xthin, we actually
                // just need to fill in CMempoolInfo
                inv2.type = MSG_GRAPHENEBLOCK;
                CMemPoolInfo receiverMemPoolInfo = GetGrapheneMempoolInfo();
                ss << inv2;
                ss << receiverMemPoolInfo;
                graphenedata.UpdateOutBoundMemPoolInfo(
                    ::GetSerializeSize(receiverMemPoolInfo, SER_NETWORK, PROTOCOL_VERSION));

                pfrom->PushMessage(NetMsgType::GET_GRAPHENE, ss);
                LOG(GRAPHENE, "Requesting graphene block %s from peer %s\n", inv2.hash.ToString(), pfrom->GetLogName());
                return true;
            }
        }


        // Ask for an xthin if Graphene is not possible.
        // Must download an xthinblock from a xthin peer.
        if (IsThinBlocksEnabled() && pfrom->ThinBlockCapable())
        {
            if (thinrelay.AddBlockInFlight(pfrom, inv2.hash, NetMsgType::XTHINBLOCK))
            {
                MarkBlockAsInFlight(pfrom->GetId(), obj.hash);

                CBloomFilter filterMemPool;
                inv2.type = MSG_XTHINBLOCK;
                std::vector<uint256> vOrphanHashes;
                {
                    READLOCK(orphanpool.cs_orphanpool);
                    for (auto &mi : orphanpool.mapOrphanTransactions)
                        vOrphanHashes.emplace_back(mi.first);
                }
                BuildSeededBloomFilter(filterMemPool, vOrphanHashes, inv2.hash, pfrom);
                ss << inv2;
                ss << filterMemPool;

                pfrom->PushMessage(NetMsgType::GET_XTHIN, ss);
                LOG(THIN, "Requesting xthinblock %s from peer %s\n", inv2.hash.ToString(), pfrom->GetLogName());
                return true;
            }
        }

        // Ask for a compact block if Graphene or xthin is not possible.
        // Must download an xthinblock from a xthin peer.
        if (IsCompactBlocksEnabled() && pfrom->CompactBlockCapable())
        {
            if (thinrelay.AddBlockInFlight(pfrom, inv2.hash, NetMsgType::CMPCTBLOCK))
            {
                MarkBlockAsInFlight(pfrom->GetId(), obj.hash);

                std::vector<CInv> vGetData;
                inv2.type = MSG_CMPCT_BLOCK;
                vGetData.push_back(inv2);
                pfrom->PushMessage(NetMsgType::GETDATA, vGetData);
                LOG(CMPCT, "Requesting compact block %s from peer %s\n", inv2.hash.ToString(), pfrom->GetLogName());
                return true;
            }
        }
    }

    // Request a full block if the BlockRelayTimer has expired.
    if (!IsChainNearlySyncd() || thinrelay.HasBlockRelayTimerExpired(obj.hash) || !thinrelay.IsBlockRelayTimerEnabled())
    {
        std::vector<CInv> vToFetch;
        inv2.type = MSG_BLOCK;
        vToFetch.push_back(inv2);

        MarkBlockAsInFlight(pfrom->GetId(), obj.hash);
        pfrom->PushMessage(NetMsgType::GETDATA, vToFetch);
        LOG(THIN | GRAPHENE | CMPCT, "Requesting Regular Block %s from peer %s\n", inv2.hash.ToString(),
            pfrom->GetLogName());
        return true;
    }
    return false; // no block was requested
}

void CRequestManager::ResetLastBlockRequestTime(const uint256 &hash)
{
    LOCK(cs_objDownloader);
    OdMap::iterator itemIter = sendBlkIter;
    itemIter = mapBlkInfo.find(hash);
    if (itemIter != mapBlkInfo.end())
    {
        CUnknownObj &item = itemIter->second;
        item.outstandingReqs--;
        item.lastRequestTime = 0;
        item.nDownloadingSince = 0;
    }
}

struct CompareIteratorByNodeRef
{
    bool operator()(const CNodeRef &a, const CNodeRef &b) const { return a.get() < b.get(); }
};

void CRequestManager::SendRequests()
{
    int64_t now = 0;

    // TODO: if a node goes offline, rerequest txns from someone else and cleanup references right away
    LOCK(cs_objDownloader);
    if (sendBlkIter == mapBlkInfo.end())
        sendBlkIter = mapBlkInfo.begin();

    // Modify retry interval. If we're doing IBD or if Traffic Shaping is ON we want to have a longer interval because
    // those blocks and txns can take much longer to download.
    unsigned int _blkReqRetryInterval = MIN_BLK_REQUEST_RETRY_INTERVAL;
    unsigned int _txReqRetryInterval = MIN_TX_REQUEST_RETRY_INTERVAL;
    if (IsTrafficShapingEnabled())
    {
        _blkReqRetryInterval *= 6;
        _txReqRetryInterval *= (12 * 2);
    }
    else if ((!IsChainNearlySyncd() && Params().NetworkIDString() != "regtest"))
    {
        _blkReqRetryInterval *= 2;
        _txReqRetryInterval *= 8;
    }

    // When we are still doing an initial sync we want to batch request the blocks instead of just
    // asking for one at time. We can do this because there will be no XTHIN requests possible during
    // this time.
    bool fBatchBlockRequests = IsInitialBlockDownload();
    std::map<CNodeRef, std::vector<CInv>, CompareIteratorByNodeRef> mapBatchBlockRequests;

    // Batch any transaction requests when possible. The process of batching and requesting batched transactions
    // is simlilar to batched block requests, however, we don't make the distinction of whether we're in the process
    // of syncing the chain, as we do with block requests.
    std::map<CNodeRef, std::vector<CInv>, CompareIteratorByNodeRef> mapBatchTxnRequests;

    // Get Blocks
    while (sendBlkIter != mapBlkInfo.end())
    {
        now = GetStopwatchMicros();
        OdMap::iterator itemIter = sendBlkIter;
        if (itemIter == mapBlkInfo.end())
            break;

        ++sendBlkIter; // move it forward up here in case we need to erase the item we are working with.
        CUnknownObj &item = itemIter->second;

        // If we've already received the item and it's in processing then skip it here so we don't
        // end up re-requesting it again.
        if (item.fProcessing)
            continue;

        // if never requested then lastRequestTime==0 so this will always be true
        if ((now - item.lastRequestTime > _blkReqRetryInterval && item.nDownloadingSince == 0) ||
            (item.nDownloadingSince != 0 && now - item.nDownloadingSince > blockLookAheadInterval.Value()))
        {
            if (!item.availableFrom.empty())
            {
                CNodeRequestData next;
                // Go thru the availableFrom list, looking for the first node that isn't disconnected
                while (!item.availableFrom.empty() && (next.noderef.get() == nullptr))
                {
                    next = item.availableFrom.front(); // Grab the next location where we can find this object.
                    item.availableFrom.pop_front();
                    if (next.noderef.get() != nullptr)
                    {
                        // Do not request from this node if it was disconnected
                        if (next.noderef.get()->fDisconnect)
                        {
                            next.noderef.~CNodeRef(); // force the loop to get another node
                        }
                    }
                }

                if (next.noderef.get() != nullptr)
                {
                    // If item.lastRequestTime is true then we've requested at least once and we'll try a re-request
                    if (item.lastRequestTime)
                    {
                        LOG(REQ, "Block request timeout for %s.  Retrying\n", item.obj.ToString().c_str());
                    }

                    CInv obj = item.obj;
                    item.outstandingReqs++;
                    int64_t then = item.lastRequestTime;
                    int64_t nDownloadingSincePrev = item.nDownloadingSince;
                    item.lastRequestTime = now;
                    item.nDownloadingSince = 0;
                    bool fReqBlkResult = false;

                    if (fBatchBlockRequests)
                    {
                        mapBatchBlockRequests[next.noderef].emplace_back(obj);
                    }
                    else
                    {
                        LEAVE_CRITICAL_SECTION(cs_objDownloader); // item and itemIter are now invalid
                        fReqBlkResult = RequestBlock(next.noderef.get(), obj);
                        ENTER_CRITICAL_SECTION(cs_objDownloader);

                        if (!fReqBlkResult)
                        {
                            // having released cs_objDownloader, item and itemiter may be invalid.
                            // So in the rare case that we could not request the block we need to
                            // find the item again (if it exists) and set the tracking back to what it was
                            itemIter = mapBlkInfo.find(obj.hash);
                            if (itemIter != mapBlkInfo.end())
                            {
                                item = itemIter->second;
                                item.outstandingReqs--;
                                item.lastRequestTime = then;
                                item.nDownloadingSince = nDownloadingSincePrev;
                            }
                        }
                    }

                    // If there was a request then release the ref otherwise put the item back into the list so
                    // we don't lose the block source.
                    if (fReqBlkResult)
                    {
                        next.noderef.~CNodeRef();
                    }
                    else
                    {
                        // We never asked for the block, typically because the graphene block timer hasn't timed out
                        // yet but we only have sources for an xthinblock. When this happens we add the node back to
                        // the end of the list so that we don't lose the source, when/if the graphene timer has
                        // a time out and we are then ready to ask for an xthinblock.
                        item.availableFrom.push_back(next);
                    }
                }
                else
                {
                    // We requested from all available sources so remove the source. This should not
                    // happen and would indicate some other problem.
                    LOG(REQ, "Block %s has no sources. Removing\n", item.obj.ToString());
                    cleanup(itemIter);
                }
            }
            else
            {
                // There can be no block sources because a node dropped out.  In this case, nothing can be done so
                // remove the item.
                LOG(REQ, "Block %s has no available sources. Removing\n", item.obj.ToString());
                cleanup(itemIter);
            }
        }
    }
    // send batched requests if any.
    if (fBatchBlockRequests && !mapBatchBlockRequests.empty())
    {
        LEAVE_CRITICAL_SECTION(cs_objDownloader);
        {
            for (auto iter : mapBatchBlockRequests)
            {
                for (auto &inv : iter.second)
                {
                    MarkBlockAsInFlight(iter.first.get()->GetId(), inv.hash);
                }
                iter.first.get()->PushMessage(NetMsgType::GETDATA, iter.second);
                LOG(REQ, "Sent batched request with %d blocks to node %s\n", iter.second.size(),
                    iter.first.get()->GetLogName());
            }
        }
        ENTER_CRITICAL_SECTION(cs_objDownloader);

        mapBatchBlockRequests.clear();
    }

    // Get Transactions
    if (sendIter == mapTxnInfo.end())
        sendIter = mapTxnInfo.begin();
    while ((sendIter != mapTxnInfo.end()) && requestPacer.try_leak(1))
    {
        now = GetStopwatchMicros();
        OdMap::iterator itemIter = sendIter;
        if (itemIter == mapTxnInfo.end())
            break;

        ++sendIter; // move it forward up here in case we need to erase the item we are working with.
        CUnknownObj &item = itemIter->second;

        // If we've already received the item and it's in processing then skip it here so we don't
        // end up re-requesting it again.
        if (item.fProcessing)
            continue;

        // if never requested then lastRequestTime==0 so this will always be true
        if (now - item.lastRequestTime > _txReqRetryInterval)
        {
            if (!item.rateLimited)
            {
                // If item.lastRequestTime is true then we've requested at least once, so this is a rerequest -> a txn
                // request was dropped.
                if (item.lastRequestTime)
                {
                    LOG(REQ, "Request timeout for %s.  Retrying\n", item.obj.ToString().c_str());
                    // Not reducing inFlight; it's still outstanding and will be cleaned up when
                    // item is removed from map.
                    // Note we can never be sure its really dropped verses just delayed for a long
                    // time so this is not authoritative.
                    droppedTxns += 1;
                }

                if (item.availableFrom.empty())
                {
                    // There can be no block sources because a node dropped out.  In this case, nothing can be done so
                    // remove the item.
                    LOG(REQ, "Tx has no sources for %s.  Removing\n", item.obj.ToString().c_str());
                    cleanup(itemIter);
                }
                else // Ok, we have at least one source so request this item.
                {
                    CNodeRequestData next;
                    // Go thru the availableFrom list, looking for the first node that isn't disconnected
                    while (!item.availableFrom.empty() && (next.noderef.get() == nullptr))
                    {
                        next = item.availableFrom.front(); // Grab the next location where we can find this object.
                        item.availableFrom.pop_front();
                        if (next.noderef.get() != nullptr)
                        {
                            if (next.noderef.get()->fDisconnect) // Node was disconnected so we can't request from it
                            {
                                next.noderef.~CNodeRef(); // force the loop to get another node
                            }
                        }
                    }

                    if (next.noderef.get() != nullptr)
                    {
                        // This commented code skips requesting TX if the node is not synced. The request
                        // manager should not make this decision but rather the caller should not give us the TX.
                        if (1)
                        {
                            item.outstandingReqs++;
                            item.lastRequestTime = now;

                            mapBatchTxnRequests[next.noderef].emplace_back(item.obj);

                            // If we have 1000 requests for this peer then send them right away.
                            if (mapBatchTxnRequests[next.noderef].size() >= 1000)
                            {
                                LEAVE_CRITICAL_SECTION(cs_objDownloader);
                                {
                                    next.noderef.get()->PushMessage(
                                        NetMsgType::GETDATA, mapBatchTxnRequests[next.noderef]);
                                    LOG(REQ, "Sent batched request with %d transations to node %s\n",
                                        mapBatchTxnRequests[next.noderef].size(), next.noderef.get()->GetLogName());
                                }
                                ENTER_CRITICAL_SECTION(cs_objDownloader);

                                mapBatchTxnRequests.erase(next.noderef);
                            }

                            // Now that we've completed setting up our request for this transaction
                            // we're done with this node, for this item, and can delete it.
                            next.noderef.~CNodeRef();
                        }

                        inFlight++;
                        inFlightTxns << inFlight;
                    }
                    else
                    {
                        // We requested from all available sources so remove the source. This should not
                        // happen and would indicate some other problem.
                        LOG(REQ, "Tx has no sources for %s.  Removing\n", item.obj.ToString().c_str());
                        cleanup(itemIter);
                    }
                }
            }
        }
    }
    // send batched requests if any.
    if (!mapBatchTxnRequests.empty())
    {
        LEAVE_CRITICAL_SECTION(cs_objDownloader);
        {
            for (auto iter : mapBatchTxnRequests)
            {
                iter.first.get()->PushMessage(NetMsgType::GETDATA, iter.second);
                LOG(REQ, "Sent batched request with %d transations to node %s\n", iter.second.size(),
                    iter.first.get()->GetLogName());
            }
        }
        ENTER_CRITICAL_SECTION(cs_objDownloader);

        mapBatchTxnRequests.clear();
    }
}

bool CRequestManager::CheckForRequestDOS(CNode *pfrom, const CChainParams &chainparams)
{
    // Check for Misbehaving and DOS
    // If they make more than MAX_THINTYPE_OBJECT_REQUESTS requests in 10 minutes then assign misbehavior points.
    //
    // Other networks have variable mining rates, so only apply these rules to mainnet only.
    if (chainparams.NetworkIDString() == "main")
    {
        LOCK(cs_objDownloader);

        std::map<NodeId, CRequestManagerNodeState>::iterator it = mapRequestManagerNodeState.find(pfrom->GetId());
        DbgAssert(it != mapRequestManagerNodeState.end(), return false);
        CRequestManagerNodeState *state = &it->second;

        // First decay the previous value
        uint64_t nNow = GetTime();
        state->nNumRequests = std::pow(1.0 - 1.0 / 600.0, (double)(nNow - state->nLastRequest));

        // Now add one request and update the time
        state->nNumRequests++;
        state->nLastRequest = nNow;

        if (state->nNumRequests >= MAX_THINTYPE_OBJECT_REQUESTS)
        {
            pfrom->fDisconnect = true;
            return error("Disconnecting  %s. Making too many (%f) thin object requests.", pfrom->GetLogName(),
                state->nNumRequests);
        }
    }
    return true;
}

// Check whether the last unknown block a peer advertised is not yet known.
void CRequestManager::ProcessBlockAvailability(NodeId nodeid)
{
    CNodeStateAccessor state(nodestate, nodeid);
    DbgAssert(state != nullptr, return );

    if (!state->hashLastUnknownBlock.IsNull())
    {
        auto *pindex = LookupBlockIndex(state->hashLastUnknownBlock);
        if (pindex && pindex->nChainWork > 0)
        {
            if (state->pindexBestKnownBlock == nullptr || pindex->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            {
                state->pindexBestKnownBlock = pindex;
            }
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

// Update tracking information about which blocks a peer is assumed to have.
void CRequestManager::UpdateBlockAvailability(NodeId nodeid, const uint256 &hash)
{
    auto *pindex = LookupBlockIndex(hash);

    CNodeStateAccessor state(nodestate, nodeid);
    DbgAssert(state != nullptr, return );

    ProcessBlockAvailability(nodeid);

    if (pindex && pindex->nChainWork > 0)
    {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == nullptr || pindex->nChainWork >= state->pindexBestKnownBlock->nChainWork)
        {
            state->pindexBestKnownBlock = pindex;
        }
    }
    else
    {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

void CRequestManager::RequestNextBlocksToDownload(CNode *pto)
{
    AssertLockHeld(cs_main);

    uint64_t nBlocksInFlight = 0;
    {
        LOCK(cs_objDownloader);
        nBlocksInFlight = mapRequestManagerNodeState[pto->GetId()].nBlocksInFlight;
    }
    if (!pto->fDisconnectRequest && !pto->fDisconnect && !pto->fClient && nBlocksInFlight < pto->nMaxBlocksInTransit)
    {
        std::vector<CBlockIndex *> vToDownload;

        FindNextBlocksToDownload(pto, pto->nMaxBlocksInTransit.load() - nBlocksInFlight, vToDownload);
        // LOG(REQ, "IBD AskFor %d blocks from peer=%s\n", vToDownload.size(), pto->GetLogName());
        std::vector<CInv> vGetBlocks;
        for (CBlockIndex *pindex : vToDownload)
        {
            CInv inv(MSG_BLOCK, pindex->GetBlockHash());
            if (!AlreadyHaveBlock(inv))
            {
                vGetBlocks.emplace_back(inv);
                // LOG(REQ, "AskFor block %s (%d) peer=%s\n", pindex->GetBlockHash().ToString(),
                //     pindex->nHeight, pto->GetLogName());
            }
        }
        if (!vGetBlocks.empty())
        {
            std::vector<CInv> vToFetchNew;
            {
                LOCK(cs_objDownloader);
                for (CInv &inv : vGetBlocks)
                {
                    // If this block is already in flight then don't ask for it again during the IBD process.
                    //
                    // If it's an additional source for a new peer then it would have been added already in
                    // FindNextBlocksToDownload().
                    std::map<uint256, std::map<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight =
                        mapBlocksInFlight.find(inv.hash);
                    if (itInFlight != mapBlocksInFlight.end())
                    {
                        continue;
                    }

                    vToFetchNew.push_back(inv);
                }
            }
            vGetBlocks.swap(vToFetchNew);

            if (!IsInitialBlockDownload())
            {
                AskFor(vGetBlocks, pto);
            }
            else
            {
                AskForDuringIBD(vGetBlocks, pto);
            }
        }
    }
}

// Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
// at most count entries.
void CRequestManager::FindNextBlocksToDownload(CNode *node, size_t count, std::vector<CBlockIndex *> &vBlocks)
{
    if (count == 0)
    {
        return;
    }
    DbgAssert(count <= 128, count = 128);

    NodeId nodeid = node->GetId();
    vBlocks.reserve(vBlocks.size() + count);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(nodeid);

    CNodeStateAccessor state(nodestate, nodeid);
    DbgAssert(state != nullptr, return );

    LOCK(cs_main);
    if (state->pindexBestKnownBlock == nullptr ||
        state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork)
    {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == nullptr)
    {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock =
            chainActive[std::min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of its current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock =
        const_cast<CBlockIndex *>(LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock));
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    std::vector<CBlockIndex *> vToFetch;
    CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the current chain tip + the block download window.  We need to ensure
    // the if running in pruning mode we don't download too many blocks ahead and as a result use to
    // much disk space to store unconnected blocks.
    int nWindowEnd = chainActive.Height() + BLOCK_DOWNLOAD_WINDOW.load();

    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    while (pindexWalk->nHeight < nMaxHeight)
    {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min((size_t)(nMaxHeight - pindexWalk->nHeight), count - vBlocks.size());
        if (nToFetch == 0)
        {
            break;
        }
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--)
        {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the mean time, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
        // already part of our chain (and therefore don't need it even if pruned).
        for (CBlockIndex *pindex : vToFetch)
        {
            uint256 blockHash = pindex->GetBlockHash();
            if (AlreadyAskedForBlock(blockHash))
            {
                // Only add a new source if there is a block in flight from a different peer. This prevents
                // us from re-adding a source for the same peer and possibly downloading two duplicate blocks.
                // This edge condition can typically happen when we were only connected to only one peer and we
                // exceed the download timeout causing us to re-request the same block from the same peer.
                std::map<uint256, std::map<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight =
                    mapBlocksInFlight.find(blockHash);
                if (itInFlight != mapBlocksInFlight.end() && !itInFlight->second.count(nodeid))
                {
                    AskFor(CInv(MSG_BLOCK, blockHash), node); // Add another source
                    continue;
                }
            }

            if (!pindex->IsValid(BLOCK_VALID_TREE))
            {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA || chainActive.Contains(pindex))
            {
                if (pindex->nChainTx)
                    state->pindexLastCommonBlock = pindex;
            }
            else
            {
                // Return if we've reached the end of the download window.
                if (pindex->nHeight > nWindowEnd)
                {
                    return;
                }

                // Return if we've reached the end of the number of blocks we can download for this peer.
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count)
                {
                    return;
                }
            }
        }
    }
}

void CRequestManager::RequestMempoolSync(CNode *pto)
{
    LOCK(cs_mempoolsync);
    NodeId nodeId = pto->GetId();

    if ((mempoolSyncRequested.count(nodeId) == 0 ||
            ((GetStopwatchMicros() - mempoolSyncRequested[nodeId].lastUpdated) > MEMPOOLSYNC_FREQ_US)) &&
        pto->canSyncMempoolWithPeers)
    {
        // Similar to Graphene, receiver must send CMempoolInfo
        CMempoolSyncInfo receiverMemPoolInfo = GetMempoolSyncInfo();
        mempoolSyncRequested[nodeId] = CMempoolSyncState(
            GetStopwatchMicros(), receiverMemPoolInfo.shorttxidk0, receiverMemPoolInfo.shorttxidk1, false);
        if (NegotiateMempoolSyncVersion(pto) > 0)
            pto->PushMessage(NetMsgType::GET_MEMPOOLSYNC, receiverMemPoolInfo);
        else
        {
            CInv inv;
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << inv;
            ss << receiverMemPoolInfo;
            pto->PushMessage(NetMsgType::GET_MEMPOOLSYNC, ss);
        }
        LOG(MPOOLSYNC, "Requesting mempool synchronization from peer %s\n", pto->GetLogName());

        lastMempoolSync = GetStopwatchMicros();
    }
}

// indicate whether we requested this block.
void CRequestManager::MarkBlockAsInFlight(NodeId nodeid, const uint256 &hash)
{
    // If started then clear the timers used for preferential downloading
    thinrelay.ClearBlockRelayTimer(hash);

    // Add to inflight, if it hasn't already been marked inflight for this node id.
    LOCK(cs_objDownloader);
    std::map<uint256, std::map<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight =
        mapBlocksInFlight.find(hash);
    if (itInFlight == mapBlocksInFlight.end() || !itInFlight->second.count(nodeid))
    {
        // Get a request manager nodestate pointer.
        std::map<NodeId, CRequestManagerNodeState>::iterator it = mapRequestManagerNodeState.find(nodeid);
        DbgAssert(it != mapRequestManagerNodeState.end(), return );
        CRequestManagerNodeState *state = &it->second;

        // Add queued block to nodestate and add iterator for queued block to mapBlocksInFlight
        int64_t nNow = GetStopwatchMicros();
        QueuedBlock newentry = {hash, nNow};
        std::list<QueuedBlock>::iterator it2 = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
        mapBlocksInFlight[hash][nodeid] = it2;

        // Increment blocks in flight for this node and if applicable the time we started downloading.
        state->nBlocksInFlight++;
        if (state->nBlocksInFlight == 1)
        {
            // We're starting a block download (batch) from this peer.
            state->nDownloadingSince = GetStopwatchMicros();
        }
    }
}

// Returns a bool if successful in indicating we received this block.
bool CRequestManager::MarkBlockAsReceived(const uint256 &hash, CNode *pnode)
{
    if (!pnode)
        return false;

    LOCK(cs_objDownloader);
    NodeId nodeid = pnode->GetId();

    // Check if we have any block in flight, for this hash, that we asked for.
    std::map<uint256, std::map<NodeId, std::list<QueuedBlock>::iterator> >::iterator itHash =
        mapBlocksInFlight.find(hash);
    if (itHash == mapBlocksInFlight.end())
        return false;

    // Lookup this block for this nodeid and if we have one in flight then mark it as received.
    std::map<NodeId, std::list<QueuedBlock>::iterator>::iterator itInFlight = itHash->second.find(nodeid);
    if (itInFlight != itHash->second.end())
    {
        // Get a request manager nodestate pointer.
        std::map<NodeId, CRequestManagerNodeState>::iterator it = mapRequestManagerNodeState.find(nodeid);
        DbgAssert(it != mapRequestManagerNodeState.end(), return false);
        CRequestManagerNodeState *state = &it->second;

        int64_t getdataTime = itInFlight->second->nTime;
        int64_t now = GetStopwatchMicros();
        double nResponseTime = (double)(now - getdataTime) / 1000000.0;

        // calculate avg block response time over a range of blocks to be used for IBD tuning.
        uint8_t blockRange = 50;
        {
            LOCK(pnode->cs_nAvgBlkResponseTime);
            if (pnode->nAvgBlkResponseTime < 0)
                pnode->nAvgBlkResponseTime = 0.0;
            if (pnode->nAvgBlkResponseTime > 0)
                pnode->nAvgBlkResponseTime -= (pnode->nAvgBlkResponseTime / blockRange);
            pnode->nAvgBlkResponseTime += nResponseTime / blockRange;

            // Protect nOverallAverageResponseTime and nIterations with cs_overallaverage.
            static CCriticalSection cs_overallaverage;
            static double nOverallAverageResponseTime = 00.0;
            static uint32_t nIterations = 0;

            // Get the average value for overall average response time (s) of all nodes.
            {
                LOCK(cs_overallaverage);
                uint32_t nOverallRange = blockRange * nMaxOutConnections;
                if (nIterations <= nOverallRange)
                    nIterations++;

                if (nOverallRange > 0)
                {
                    if (nIterations > nOverallRange)
                    {
                        nOverallAverageResponseTime -= (nOverallAverageResponseTime / nOverallRange);
                    }
                    nOverallAverageResponseTime += nResponseTime / nOverallRange;
                }
                else
                {
                    LOG(IBD, "Calculation of average response time failed and will be inaccurate due to division by "
                             "zero.\n");
                }

                // Request for a disconnect if over the response time limit.  We don't do an fDisconnect = true here
                // because we want to drain the queue for any blocks that are still returning.  This prevents us from
                // having to re-request all those blocks again.
                //
                // We only check wether to issue a disconnect during initial sync and we only disconnect up to two
                // peers at a time if and only if all our outbound slots have been used to prevent any sudden loss of
                // all peers. We do this for two peers and not one in the event that one of the peers is hung and their
                // block queue does not drain; in that event we would end up waiting for 10 minutes before finally
                // disconnecting.
                //
                // We disconnect a peer only if their average response time is more than 4 times the overall average.
                static int nStartDisconnections GUARDED_BY(cs_overallaverage) = BEGIN_PRUNING_PEERS;
                if (!pnode->fDisconnectRequest &&
                    (nOutbound >= nMaxOutConnections - 1 || nOutbound >= nStartDisconnections) &&
                    IsInitialBlockDownload() && nIterations > nOverallRange &&
                    pnode->nAvgBlkResponseTime > nOverallAverageResponseTime * 4)
                {
                    LOG(IBD, "disconnecting %s because too slow , overall avg %d peer avg %d\n", pnode->GetLogName(),
                        nOverallAverageResponseTime, pnode->nAvgBlkResponseTime);
                    pnode->InitiateGracefulDisconnect();
                    // We must not return here but continue in order
                    // to update the vBlocksInFlight stats.

                    // Increment so we start disconnecting at a higher number of peers each time. This
                    // helps to improve the very beginning of IBD such that we don't have to wait for all outbound
                    // connections to be established before we start pruning the slow peers and yet we don't end
                    // up suddenly overpruning.
                    nStartDisconnections = nOutbound;
                    if (nStartDisconnections < nMaxOutConnections)
                        nStartDisconnections++;
                }
            }

            if (pnode->nAvgBlkResponseTime < 0.2)
            {
                pnode->nMaxBlocksInTransit.store(64);
            }
            else if (pnode->nAvgBlkResponseTime < 0.5)
            {
                pnode->nMaxBlocksInTransit.store(56);
            }
            else if (pnode->nAvgBlkResponseTime < 0.9)
            {
                pnode->nMaxBlocksInTransit.store(48);
            }
            else if (pnode->nAvgBlkResponseTime < 1.4)
            {
                pnode->nMaxBlocksInTransit.store(32);
            }
            else if (pnode->nAvgBlkResponseTime < 2.0)
            {
                pnode->nMaxBlocksInTransit.store(24);
            }
            else
            {
                pnode->nMaxBlocksInTransit.store(16);
            }

            LOG(THIN | BLK, "Average block response time is %.2f seconds for %s\n", pnode->nAvgBlkResponseTime,
                pnode->GetLogName());
        }

        // if there are no blocks in flight then ask for a few more blocks
        if (state->nBlocksInFlight <= 0)
            pnode->nMaxBlocksInTransit.fetch_add(4);

        if (maxBlocksInTransitPerPeer.Value() != 0)
        {
            pnode->nMaxBlocksInTransit.store(maxBlocksInTransitPerPeer.Value());
        }
        if (blockDownloadWindow.Value() != 0)
        {
            BLOCK_DOWNLOAD_WINDOW.store(blockDownloadWindow.Value());
        }
        LOG(THIN | BLK, "BLOCK_DOWNLOAD_WINDOW is %d nMaxBlocksInTransit is %lu\n", BLOCK_DOWNLOAD_WINDOW.load(),
            pnode->nMaxBlocksInTransit.load());

        // Update the appropriate response time based on the type of block received.
        if (IsChainNearlySyncd())
        {
            // Update Thinblock stats
            if (thinrelay.IsBlockInFlight(pnode, NetMsgType::XTHINBLOCK, hash))
            {
                thindata.UpdateResponseTime(nResponseTime);
            }
            // Update Graphene stats
            if (thinrelay.IsBlockInFlight(pnode, NetMsgType::GRAPHENEBLOCK, hash))
            {
                graphenedata.UpdateResponseTime(nResponseTime);
            }
            // Update CompactBlock stats
            if (thinrelay.IsBlockInFlight(pnode, NetMsgType::CMPCTBLOCK, hash))
            {
                compactdata.UpdateResponseTime(nResponseTime);
            }
        }

        if (state->vBlocksInFlight.begin() == itInFlight->second)
        {
            // First block on the queue was received, update the start download time for the next one
            state->nDownloadingSince = std::max(state->nDownloadingSince, (int64_t)GetStopwatchMicros());
        }
        // In order to prevent a dangling iterator we must erase from vBlocksInFlight after mapBlockInFlight
        // however that will invalidate the iterator held by mapBlocksInFlight. Use a temporary to work around this.
        std::list<QueuedBlock>::iterator tmp = itInFlight->second;
        state->nBlocksInFlight--;
        MapBlocksInFlightErase(hash, nodeid);
        state->vBlocksInFlight.erase(tmp);

        return true;
    }
    return false;
}

void CRequestManager::MapBlocksInFlightErase(const uint256 &hash, NodeId nodeid)
{
    // If there are more than one block in flight for the same block hash then we only remove
    // the entry for this particular node, otherwise entirely remove the hash from mapBlocksInFlight.
    LOCK(cs_objDownloader);
    std::map<uint256, std::map<NodeId, std::list<QueuedBlock>::iterator> >::iterator itHash =
        mapBlocksInFlight.find(hash);
    if (itHash != mapBlocksInFlight.end())
    {
        itHash->second.erase(nodeid);
    }
}

bool CRequestManager::MapBlocksInFlightEmpty()
{
    LOCK(cs_objDownloader);
    return mapBlocksInFlight.empty();
}

void CRequestManager::MapBlocksInFlightClear()
{
    LOCK(cs_objDownloader);
    mapBlocksInFlight.clear();
}

void CRequestManager::GetBlocksInFlight(std::vector<uint256> &vBlocksInFlight, NodeId nodeid)
{
    LOCK(cs_objDownloader);
    for (auto &iter : mapRequestManagerNodeState[nodeid].vBlocksInFlight)
    {
        vBlocksInFlight.emplace_back(iter.hash);
    }
}

int CRequestManager::GetNumBlocksInFlight(NodeId nodeid)
{
    LOCK(cs_objDownloader);
    return mapRequestManagerNodeState[nodeid].nBlocksInFlight;
}

void CRequestManager::RemoveNodeState(NodeId nodeid)
{
    LOCK(cs_objDownloader);
    std::vector<uint256> vBlocksInFlight;
    GetBlocksInFlight(vBlocksInFlight, nodeid);
    for (const uint256 &hash : vBlocksInFlight)
    {
        // Erase mapblocksinflight entries for this node.
        MapBlocksInFlightErase(hash, nodeid);

        // Reset all requests times to zero so that we can immediately re-request these blocks
        ResetLastBlockRequestTime(hash);
    }
    mapRequestManagerNodeState.erase(nodeid);
}

void CRequestManager::DisconnectOnDownloadTimeout(CNode *pnode, const Consensus::Params &consensusParams, int64_t nNow)
{
    // In case there is a block that has been in flight from this peer for 2 + 0.5 * N times the block interval
    // (with N the number of peers from which we're downloading validated blocks), disconnect due to timeout.
    // We compensate for other peers to prevent killing off peers due to our own downstream link
    // being saturated. We only count validated in-flight blocks so peers can't advertise non-existing block hashes
    // to unreasonably increase our timeout.
    LOCK(cs_objDownloader);
    NodeId nodeid = pnode->GetId();
    if (!pnode->fDisconnect && mapRequestManagerNodeState[nodeid].vBlocksInFlight.size() > 0)
    {
        if (nNow >
            mapRequestManagerNodeState[nodeid].nDownloadingSince +
                consensusParams.nPowTargetSpacing * (BLOCK_DOWNLOAD_TIMEOUT_BASE + BLOCK_DOWNLOAD_TIMEOUT_PER_PEER))
        {
            LOGA("Timeout downloading block %s from peer %s, disconnecting\n",
                mapRequestManagerNodeState[nodeid].vBlocksInFlight.front().hash.ToString(), pnode->GetLogName());
            pnode->fDisconnect = true;
        }
    }
}
