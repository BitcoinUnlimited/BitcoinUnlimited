// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "nodestate.h"

extern CChain chainActive;

/**
* Default constructor initializing all local member variables
*/
CNodeState::CNodeState()
{
    pindexBestKnownBlock = nullptr;
    hashLastUnknownBlock.SetNull();
    pindexLastCommonBlock = nullptr;
    pindexBestHeaderSent = nullptr;
    fSyncStarted = false;
    nSyncStartTime = GetTime();
    fFirstHeadersReceived = false;
    nFirstHeadersExpectedHeight = chainActive.Height();
    nDownloadingSince = 0;
    nBlocksInFlight = 0;
    nBlocksInFlightValidHeaders = 0;
    fPreferredDownload = false;
    fPreferHeaders = false;
}

/**
* Gets the CNodeState for the specified NodeId.
*
* @param[in] pnode  The NodeId to return CNodeState* for
* @return CNodeState* matching the NodeId, or NULL if NodeId is not matched
*/
CNodeState *State(NodeId id)
{
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(id);
    if (it == mapNodeState.end())
        return nullptr;
    return &it->second;
}
