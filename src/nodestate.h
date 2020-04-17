// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODESTATE_H
#define BITCOIN_NODESTATE_H

#include "net.h" // For NodeId
#include "requestManager.h"


/**
* Maintain validation-specific state about nodes, instead
* of by CNode's own locks. This simplifies asynchronous operation, where
* processing of incoming data is done after the ProcessMessage call returns,
* and we're no longer holding the node's locks.
*/
struct CNodeState
{
    //! The peer's address
    CService address;
    //! String name of this peer (debugging/logging purposes).
    std::string name;
    //! The best known block we know this peer has announced.
    CBlockIndex *pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex *pindexLastCommonBlock;
    //! The best header we have sent our peer.
    CBlockIndex *pindexBestHeaderSent;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! The start time of the sync
    int64_t nSyncStartTime;
    //! Were the first headers requested in a sync received
    bool fFirstHeadersReceived;
    //! Our current block height at the time we requested GETHEADERS
    int nFirstHeadersExpectedHeight;
    //! During IBD we need to update the block availabiity for each peer. We do this by requesting a header
    //  when a peer connects and also when we ask for the initial set of all headers.
    bool fRequestedInitialBlockAvailability;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;
    //! Whether this peer wants invs or headers (when possible) for block announcements.
    bool fPreferHeaders;

    CNodeState(CAddress addrIn, std::string addrNameIn);
};

class CState
{
protected:
    /** Map maintaining per-node state. */
    CCriticalSection cs_cstate;
    std::map<NodeId, CNodeState> mapNodeState GUARDED_BY(cs_cstate);
    friend class CNodeStateAccessor;

public:
    /** Return a node pointer for an node id (does not lock -- use CNodeStateAccessor)
     * Do not use it directly, this is meant to be used through CNodeStateAccessor
     **/
    CNodeState *_GetNodeState(const NodeId id);

    /** Add a nodestate from the map */
    void InitializeNodeState(const CNode *pnode);

    /** Delete a nodestate from the map */
    void RemoveNodeState(const NodeId id);

    /** Clear the entire nodestate map */
    void Clear();

    /** Is mapNodestate empty */
    bool Empty()
    {
        LOCK(cs_cstate);
        return mapNodeState.empty();
    }
};

class CNodeStateAccessor
{
    CCriticalSection *cs_ns_accessor;
    CNodeState *obj;

public:
    CNodeStateAccessor(CCriticalSection *_cs, CNodeState *_obj) : cs_ns_accessor(_cs), obj(_obj)
    {
        cs_ns_accessor->lock();
    }
    CNodeStateAccessor(CState &ns, const NodeId id)
    {
        cs_ns_accessor = &ns.cs_cstate;
        cs_ns_accessor->lock();
        obj = ns._GetNodeState(id);
    }

    CNodeState *operator->() { return obj; }
    CNodeState &operator*() { return *obj; }
    bool operator!=(void *ptr) { return obj != ptr; }
    bool operator==(void *ptr) { return obj == ptr; }
    ~CNodeStateAccessor()
    {
        obj = nullptr;
        cs_ns_accessor->unlock();
    }
};

extern CState nodestate;

#endif // BITCOIN_NODESTATE_H
