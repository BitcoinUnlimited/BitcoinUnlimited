// Copyright (c) 2016-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/* The request manager creates an isolation layer between the bitcoin message processor and the network.
It tracks known locations of data objects and issues requests to the node most likely to respond.  It monitors responses
and is capable of re-requesting the object if the node disconnects or does not respond.

This stops this node from losing transactions if the remote node does not respond (previously, additional INVs would be
dropped because the transaction is "in flight"), yet when the request finally timed out or the connection dropped, the
INVs likely would no longer be propagating throughout the network so this node would miss the transaction.

It should also be possible to use the statistics gathered by the request manager to make unsolicited requests for data
likely held by other nodes, to choose the best node for expedited service, and to minimize data flow over poor
links, such as through the "Great Firewall of China".

This is a singleton class, instantiated as a global named "requester".

The P2P message processing software should no longer directly request data from a node.  Instead call:
requester.AskFor(...)

After the object arrives (its ok to call after ANY object arrives), call "requester.Received(...)" to indicate
successful receipt, "requester.Rejected(...)" to indicate a bad object (request manager will try someone else), or
"requester.AlreadyReceived" to indicate the receipt of an object that has already been received.
 */

#ifndef REQUEST_MANAGER_H
#define REQUEST_MANAGER_H

#include "net.h"
#include "nodestate.h"
#include "stat.h"

#include <atomic>

// Max requests allowed in a 10 minute window
static const uint8_t MAX_THINTYPE_OBJECT_REQUESTS = 100;

// When should I request a tx from someone else (in microseconds). cmdline/bitcoin.conf: -txretryinterval
extern unsigned int txReqRetryInterval;
extern unsigned int MIN_TX_REQUEST_RETRY_INTERVAL;
static const unsigned int DEFAULT_MIN_TX_REQUEST_RETRY_INTERVAL = 5 * 1000 * 1000;
// When should I request a block from someone else (in microseconds). cmdline/bitcoin.conf: -blkretryinterval
extern unsigned int blkReqRetryInterval;
extern unsigned int MIN_BLK_REQUEST_RETRY_INTERVAL;
static const unsigned int DEFAULT_MIN_BLK_REQUEST_RETRY_INTERVAL = 5 * 1000 * 1000;

class CNode;

class CNodeRequestData
{
public:
    int requestCount;
    int desirability;
    CNode *node;
    CNodeRequestData(CNode *);

    CNodeRequestData() : requestCount(0), desirability(0), node(NULL) {}
    void clear(void)
    {
        requestCount = 0;
        node = nullptr;
        desirability = 0;
    }
    bool operator<(const CNodeRequestData &rhs) const { return desirability < rhs.desirability; }
};

// Compare a CNodeRequestData object to a node
struct MatchCNodeRequestData
{
    CNode *node;
    MatchCNodeRequestData(CNode *n) : node(n){};
    inline bool operator()(const CNodeRequestData &nd) const { return nd.node == node; }
};

class CUnknownObj
{
public:
    typedef std::list<CNodeRequestData> ObjectSourceList;
    CInv obj;
    bool rateLimited;
    bool fProcessing; // object was received but is still being processed
    int64_t lastRequestTime; // In microseconds, 0 means no request
    unsigned int outstandingReqs;
    ObjectSourceList availableFrom;
    unsigned int priority;

    CUnknownObj()
    {
        rateLimited = false;
        fProcessing = false;
        outstandingReqs = 0;
        lastRequestTime = 0;
        priority = 0;
    }

    bool AddSource(CNode *from); // returns true if the source did not already exist
};

// The following structs are used for tracking the internal requestmanager nodestate.
struct QueuedBlock
{
    uint256 hash;
    int64_t nTime; //! Time of "getdata" request in microseconds.
};
struct CRequestManagerNodeState
{
    // An ordered list of blocks currently in flight.  We could use mapBlocksInFlight to get the same
    // data but then we'd have to iterate through the entire map to find what we're looking for.
    std::list<QueuedBlock> vBlocksInFlight;

    // When the first entry in vBlocksInFlight started downloading. Don't care when vBlocksInFlight is empty.
    int64_t nDownloadingSince;

    // How many blocks are currently in flight and requested by this node.
    int nBlocksInFlight;

    // Track how many thin type objects were requested for this peer
    double nNumRequests;
    uint64_t nLastRequest;

    CRequestManagerNodeState();
};

class CRequestManager
{
protected:
#ifdef ENABLE_MUTRACE
    friend class CPrintSomePointers;
#endif
#ifdef DEBUG
    friend UniValue getstructuresizes(const UniValue &params, bool fHelp);
#endif
    friend class CState;

    // maps and iterators all GUARDED_BY cs_objDownloader
    typedef std::map<uint256, CUnknownObj> OdMap;
    OdMap mapTxnInfo;
    OdMap mapBlkInfo;
    std::map<uint256, std::map<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight;
    std::map<NodeId, CRequestManagerNodeState> mapRequestManagerNodeState;
    OdMap::iterator sendIter;
    OdMap::iterator sendBlkIter;
    CCriticalSection cs_objDownloader;

    int inFlight;
    CStatHistory<int> inFlightTxns;
    CStatHistory<int> receivedTxns;
    CStatHistory<int> rejectedTxns;
    CStatHistory<int> droppedTxns;
    CStatHistory<int> pendingTxns;

    void cleanup(OdMap::iterator &item);
    CLeakyBucket requestPacer;

public:
    CRequestManager();

    // How many outbound nodes are we connected to.
    std::atomic<int32_t> nOutbound;

    /** Size of the "block download window": how far ahead of our current height do we fetch?
     *  Larger windows tolerate larger download speed differences between peer, but increase the potential
     *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
     *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
    std::atomic<unsigned int> BLOCK_DOWNLOAD_WINDOW{1024};

    // Request a single block.
    bool RequestBlock(CNode *pfrom, CInv obj);

    // Get this object from somewhere, asynchronously.
    void AskFor(const CInv &obj, CNode *from, unsigned int priority = 0);

    // Get these objects from somewhere, asynchronously.
    void AskFor(const std::vector<CInv> &objArray, CNode *from, unsigned int priority = 0);

    // Get these objects from somewhere, asynchronously during IBD. During IBD we must assume every peer connected
    // can give us the blocks we need and so we tell the request manager about these sources. Otherwise the request
    // manager may not be able to re-request blocks from anyone after a timeout and we also need to be able to not
    // request another group of blocks that are already in flight.
    void AskForDuringIBD(const std::vector<CInv> &objArray, CNode *from, unsigned int priority = 0);

    // Did we already ask for this block. We need to do this during IBD to make sure we don't ask for another set
    // of the same blocks.
    bool AlreadyAskedForBlock(const uint256 &hash);

    // Update the response time for this transaction request
    void UpdateTxnResponseTime(const CInv &obj, CNode *pfrom);

    // Indicate that we are processing this object.
    void Processing(const CInv &obj, CNode *pfrom);

    // Indicate that we got this object
    void Received(const CInv &obj, CNode *pfrom);

    // Indicate that we previously got this object
    void AlreadyReceived(CNode *pnode, const CInv &obj);

    // Indicate that getting this object was rejected
    void Rejected(const CInv &obj, CNode *from, unsigned char reason = 0);

    // request a block by its hash
    void RequestCorruptedBlock(const uint256 &blockHash);

    // Resets the last request time to zero when a node disconnects and has blocks in flight.
    void ResetLastBlockRequestTime(const uint256 &hash);

    void SendRequests();

    // Check whether the limit for thintype object requests has been exceeded
    bool CheckForRequestDOS(CNode *pfrom, const CChainParams &chainparams);

    // Check whether the last unknown block a peer advertised is not yet known.
    void ProcessBlockAvailability(NodeId nodeid);

    // Update tracking information about which blocks a peer is assumed to have.
    void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash);

    // Request the next blocks. Mostly this will get exucuted during IBD but sometimes even
    // when the chain is syncd a block will get request via this method.
    void RequestNextBlocksToDownload(CNode *pto);

    // This gets called from RequestNextBlocksToDownload
    void FindNextBlocksToDownload(CNode *node, unsigned int count, std::vector<CBlockIndex *> &vBlocks);

    // Returns a bool indicating whether we requested this block.
    void MarkBlockAsInFlight(NodeId nodeid, const uint256 &hash);

    // Returns a bool if successful in indicating we received this block.
    bool MarkBlockAsReceived(const uint256 &hash, CNode *pnode);

    // Methods for handling mapBlocksInFlight which is protected.
    void MapBlocksInFlightErase(const uint256 &hash, NodeId nodeid);
    bool MapBlocksInFlightEmpty();
    void MapBlocksInFlightClear();

    void MapNodestateClear()
    {
        LOCK(cs_objDownloader);
        mapRequestManagerNodeState.clear();
    }

    // Methods for handling mapRequestManagerNodeState which is protected.
    void GetBlocksInFlight(std::vector<uint256> &vBlocksInFlight, NodeId nodeid);
    int GetNumBlocksInFlight(NodeId nodeid);

    // Add entry to the requestmanager nodestate map
    void InitializeNodeState(NodeId nodeid)
    {
        LOCK(cs_objDownloader);
        mapRequestManagerNodeState.emplace(nodeid, CRequestManagerNodeState());
    }

    // Remove a request manager node from the nodestate map.
    void RemoveNodeState(NodeId nodeid);

    // Check for block download timeout and disconnect node if necessary.
    void DisconnectOnDownloadTimeout(CNode *pnode, const Consensus::Params &consensusParams, int64_t nNow);
};


extern CRequestManager requester; // Singleton class

#endif
