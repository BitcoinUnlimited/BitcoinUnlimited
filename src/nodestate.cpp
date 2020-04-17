// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "nodestate.h"
#include "main.h"

extern std::atomic<int> nPreferredDownload;

/**
* Default constructor initializing all local member variables to "null" values
*/
CNodeState::CNodeState(CAddress addrIn, std::string addrNameIn) : address(addrIn), name(addrNameIn)
{
    pindexBestKnownBlock = nullptr;
    hashLastUnknownBlock.SetNull();
    pindexLastCommonBlock = nullptr;
    pindexBestHeaderSent = nullptr;
    fSyncStarted = false;
    nSyncStartTime = -1;
    fFirstHeadersReceived = false;
    nFirstHeadersExpectedHeight = -1;
    fRequestedInitialBlockAvailability = false;
    fPreferredDownload = false;
    fPreferHeaders = false;
}

/**
* Gets the CNodeState for the specified NodeId.
*
* @param[in] pnode  The NodeId to return CNodeState* for
* @return CNodeState* matching the NodeId, or nullptr if NodeId is not matched
*/
CNodeState *CState::_GetNodeState(const NodeId id)
{
// no need to lock explictly here cause CNodeStateAccessor
// hence we are using clang pragma to silence it when it will be used
// from CNodeStateAccessor
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
#endif
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(id);
    if (it == mapNodeState.end())
        return nullptr;
    return &it->second;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

/**
* Initialize the CNodeState for the specified NodeId.
*
* @param[in] pnode  The NodeId
* @return none
*/
void CState::InitializeNodeState(const CNode *pnode)
{
    LOCK(cs_cstate);
    mapNodeState.emplace_hint(mapNodeState.end(), std::piecewise_construct, std::forward_as_tuple(pnode->GetId()),
        std::forward_as_tuple(pnode->addr, pnode->addrName));
}

/**
* Remove the CNodeState for the specified NodeId.
*
* @param[in] pnode  The NodeId
* @return none
*/
void CState::RemoveNodeState(const NodeId id)
{
    LOCK2(cs_cstate, requester.cs_objDownloader);
    mapNodeState.erase(id);

    // Remove any other types of nodestate
    requester.RemoveNodeState(id);

    // Do a consistency check after the last peer is removed.
    if (mapNodeState.empty())
    {
        DbgAssert(requester.MapBlocksInFlightEmpty(), requester.MapBlocksInFlightClear());
        DbgAssert(requester.mapRequestManagerNodeState.empty(), requester.mapRequestManagerNodeState.clear());
        DbgAssert(nPreferredDownload.load() == 0, nPreferredDownload.store(0));
    }
}

void CState::Clear()
{
    LOCK(cs_cstate);
    mapNodeState.clear();
}
