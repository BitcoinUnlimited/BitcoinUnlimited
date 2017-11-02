// Copyright (c) 2016-2017 The Bitcoin Unlimited developers
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
#include "stat.h"
// When should I request a tx from someone else (in microseconds). cmdline/bitcoin.conf: -txretryinterval
extern unsigned int MIN_TX_REQUEST_RETRY_INTERVAL;
static const unsigned int DEFAULT_MIN_TX_REQUEST_RETRY_INTERVAL = 5 * 1000 * 1000;
// When should I request a block from someone else (in microseconds). cmdline/bitcoin.conf: -blkretryinterval
extern unsigned int MIN_BLK_REQUEST_RETRY_INTERVAL;
static const unsigned int DEFAULT_MIN_BLK_REQUEST_RETRY_INTERVAL = 5 * 1000 * 1000;

// How long in seconds we wait for a xthin request to be fullfilled before disconnecting the node.
static const unsigned int THINBLOCK_DOWNLOAD_TIMEOUT = 5 * 60;

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
        node = 0;
        desirability = 0;
    }
    bool operator<(const CNodeRequestData &rhs) const { return desirability < rhs.desirability; }
};

struct MatchCNodeRequestData // Compare a CNodeRequestData object to a node
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
    uint8_t paused;
    int64_t lastRequestTime; // In microseconds, 0 means no request
    unsigned int outstandingReqs;
    NodeId receivingFrom;
    // char    requestCount[MAX_AVAIL_FROM];
    // CNode* availableFrom[MAX_AVAIL_FROM];
    ObjectSourceList availableFrom;
    unsigned int priority;

    CUnknownObj()
    {
        paused = 0;
        receivingFrom = 0;
        outstandingReqs = 0;
        lastRequestTime = 0;
        priority = 0;
    }

    bool AddSource(CNode *from); // returns true if the source did not already exist
};

class ShardedMap
{
public:
    enum
    {
        NUM_SHARDS = 8
    };

    typedef std::map<uint256, CUnknownObj> OdMap;

    OdMap mp[NUM_SHARDS];
    CCriticalSection cs[NUM_SHARDS];

    class Accessor
    {
    protected:
        OdMap *mp;
        CCriticalSection *cs;

    public:
        Accessor(ShardedMap &sm, unsigned int index)
        {
            int shard = index & (NUM_SHARDS - 1);
            mp = &sm.mp[shard];
            cs = &sm.cs[shard];
            cs->lock();
        }
        Accessor(ShardedMap &sm, uint256 val)
        {
            int shard = (*val.begin()) & (NUM_SHARDS - 1);
            mp = &sm.mp[shard];
            cs = &sm.cs[shard];
            cs->lock();
        }

        // you must lock again before destruction
        void unlock()
        {
            if (cs)
            {
                cs->unlock();
            }
        }
        void lock()
        {
            if (cs)
            {
                cs->lock();
            }
        }

        ~Accessor()
        {
            if (mp)
                mp = NULL;
            if (cs)
            {
                cs->unlock();
                cs = NULL;
            }
        }

        OdMap &operator*(void) { return *mp; }
        OdMap *operator->(void) { return mp; }
    };

    class iterator
    {
    public:
        int shard;
        ShardedMap *sm;
        OdMap::iterator it;
        friend class ShardedMap;
        OdMap::iterator &operator->() { return it; }
        OdMap::value_type operator*() { return *it; }
        bool operator==(const iterator &other) const
        {
            // special case the end
            if (other.shard == -1)
            {
                if (shard == -1)
                    return true;
                return false;
            }
            if (shard == -1)
            {
                if (other.shard == -1)
                    return true;
                return false;
            }
            return (shard == other.shard) && (it == other.it);
        }
        bool operator!=(const iterator &other) const { return !(*this == other); }
        iterator &operator++();

        iterator()
        {
            shard = -1;
            sm = NULL;
        }
    };

    void _erase(iterator &it) { mp[it.shard].erase(it.it); }
    size_t size()
    {
        size_t ret = 0;
        for (int i = 0; i < NUM_SHARDS; i++)
        {
            Accessor macc(*this, i);
            ret += macc->size();
        }
        return ret;
    }

    iterator end(int shard = -1);
    // I need to pass the iterator so I can assign it
    // within the lock
    void begin(iterator &ret, int shard = 0);
};

class CRequestManager
{
protected:
#ifdef ENABLE_MUTRACE
    friend class PrintSomePointers;
#endif
#ifdef DEBUG
    friend UniValue getstructuresizes(const UniValue &params, bool fHelp);
#endif

    // map of transactions
    typedef std::map<uint256, CUnknownObj> OdMap;
    ShardedMap mapTxnInfo;
    OdMap mapBlkInfo;
    CCriticalSection cs_objDownloader; // protects mapTxnInfo and mapBlkInfo

    // ShardedMap::iterator sendIter;
    OdMap::iterator sendBlkIter;

    int inFlight;
    // int maxInFlight;
    CStatHistory<int> inFlightTxns;
    CStatHistory<int> receivedTxns;
    CStatHistory<int> rejectedTxns;
    CStatHistory<int> droppedTxns;
    CStatHistory<int> pendingTxns;

    void cleanup(CUnknownObj &item);
    void cleanup(ShardedMap::Accessor &macc, OdMap::iterator &itemIt);
    void cleanup(OdMap::iterator &item);
    void cleanup(ShardedMap::iterator &item);

    CLeakyBucket requestPacer;
    CLeakyBucket blockPacer;

public:
    CRequestManager();

    // Return the number of blocks currently in the process of being requested
    int getOutstandingBlockRequests() { return mapBlkInfo.size(); }
    // Get this object from somewhere, asynchronously.
    void AskFor(const CInv &obj, CNode *from, unsigned int priority = 0);

    // Get these objects from somewhere, asynchronously.
    void AskFor(const std::vector<CInv> &objArray, CNode *from, unsigned int priority = 0);

    // Indicate that we got this object, from and bytes are optional (for node performance tracking)
    void Received(const CInv &obj, CNode *from, int bytes = 0);

    // Indicate that we previously got this object
    void AlreadyReceived(const CInv &obj);

    // Indicate that getting this object was rejected
    void Rejected(const CInv &obj, CNode *from, unsigned char reason = 0);

    // Do not rerequest this object, but hold onto the data (recursive)
    // This is useful once the data has been received, but still needs to be processed
    // "Resume" unpauses, "Received" unpauses all and marks the data received.
    void Pause(const CInv &obj);

    // Undo a pause
    void Resume(const CInv &obj);

    void SendRequests();

    // Indicates whether a node ping time is acceptable relative to the overall average of all nodes.
    bool IsNodePingAcceptable(CNode *pnode);

    // Call if the passed node is or is about to be disconnected to notify the request manager to not expect data.
    void RemoveSource(CNode *from);
};


extern CRequestManager requester; // Singleton class

#endif
