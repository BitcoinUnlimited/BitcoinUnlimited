// Copyright (c) 2016-2019 The Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/blockrelay_common.h"
#include "blockrelay/graphene.h"
#include "blockrelay/thinblock.h"
#include "bloom.h"
#include "chainparams.h"
#include "dosman.h"
#include "main.h"
#include "net.h"
#include "primitives/block.h"
#include "protocol.h"
#include "random.h"
#include "requestManager.h"
#include "serialize.h"
#include "streams.h"
#include "streams.h"
#include "txmempool.h"
#include "uint256.h"
#include "unlimited.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation/validation.h"
#include "version.h"

#include "test/test_bitcoin.h"

#include <algorithm>
#include <atomic>
#include <boost/test/unit_test.hpp>
#include <sstream>
#include <string>

// Cleanup all maps
static void CleanupAll(std::vector<CNode *> &vPeers)
{
    for (auto pnode : vPeers)
    {
        thinrelay.ClearAllBlocksToReconstruct(pnode->GetId());
        thinrelay.ClearAllBlocksInFlight(pnode->GetId());
        thinrelay.RemovePeers(pnode);

        pnode->vSendMsg.clear();
        pnode->vLowPrioritySendMsg.clear();
    }
    requester.MapBlocksInFlightClear();
}

// Return the netmessage string for a block/xthin/graphene request
static std::string NetMessage(std::deque<CSerializeData> &_vSendMsg)
{
    if (_vSendMsg.size() == 0)
        return "none";

    CInv inv_result;
    CSerializeData data = _vSendMsg.front();
    std::string ssData(data.begin(), data.end());
    std::string ss(ssData.begin() + 4, ssData.begin() + 16);
    _vSendMsg.pop_front();

    // Remove whitespace
    ss.erase(std::remove(ss.begin(), ss.end(), '\000'), ss.end());

    // if it's a getdata then we need to find out what type
    if (ss == "getdata")
    {
        CDataStream ssInv(SER_NETWORK, PROTOCOL_VERSION);
        ssInv.insert(ssInv.begin(), ssData.begin() + 25, ssData.begin() + 61);

        CInv inv;
        ssInv >> inv;

        if (inv.type == MSG_BLOCK)
            return "getdata";
        else if (inv.type == MSG_CMPCT_BLOCK)
            return "cmpctblock";
        else
            return "nothing";
    }
    return ss;
}

static void ClearThinBlocksInFlight(CNode &node, CInv &inv) { thinrelay.ClearBlockInFlight(&node, inv.hash); }
BOOST_FIXTURE_TEST_SUITE(requestmanager_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(blockrequest_tests)
{
    // Test the requesting of blocks/graphenblocks/thinblocks with varying node configurations.
    // This tests all the code paths within RequestBlock() in the request manager.

    // create dummy test addrs
    CAddress addr_xthin(ipaddress(0xa0b0c001, 10000));
    CAddress addr_graphene(ipaddress(0xa0b0c002, 10001));
    CAddress addr_cmpct(ipaddress(0xa0b0c003, 10002));
    CAddress addr_none(ipaddress(0xa0b0c004, 10003));

    // create nodes
    CNode dummyNodeXthin(INVALID_SOCKET, addr_xthin, "", true);
    CNode dummyNodeGraphene(INVALID_SOCKET, addr_graphene, "", true);
    CNode dummyNodeCmpct(INVALID_SOCKET, addr_cmpct, "", true);
    CNode dummyNodeNone(INVALID_SOCKET, addr_none, "", true);
    dummyNodeXthin.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeXthin.state_incoming = ConnectionStateIncoming::READY;
    dummyNodeXthin.state_outgoing = ConnectionStateOutgoing::READY;
    dummyNodeXthin.nServices |= NODE_XTHIN;
    dummyNodeXthin.nServices &= ~NODE_GRAPHENE;
    dummyNodeXthin.fSupportsCompactBlocks = false;
    dummyNodeXthin.id = 1;
    dummyNodeGraphene.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeGraphene.state_incoming = ConnectionStateIncoming::READY;
    dummyNodeGraphene.state_outgoing = ConnectionStateOutgoing::READY;
    dummyNodeGraphene.nServices |= NODE_GRAPHENE;
    dummyNodeGraphene.nServices &= ~NODE_XTHIN;
    dummyNodeGraphene.fSupportsCompactBlocks = false;
    dummyNodeGraphene.id = 2;
    dummyNodeCmpct.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeCmpct.state_incoming = ConnectionStateIncoming::READY;
    dummyNodeCmpct.state_outgoing = ConnectionStateOutgoing::READY;
    dummyNodeCmpct.nServices &= ~NODE_GRAPHENE;
    dummyNodeCmpct.nServices &= ~NODE_XTHIN;
    dummyNodeCmpct.fSupportsCompactBlocks = true;
    dummyNodeCmpct.id = 3;
    dummyNodeNone.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeNone.state_incoming = ConnectionStateIncoming::READY;
    dummyNodeNone.state_outgoing = ConnectionStateOutgoing::READY;
    dummyNodeNone.nServices &= ~NODE_GRAPHENE;
    dummyNodeNone.nServices &= ~NODE_XTHIN;
    dummyNodeNone.fSupportsCompactBlocks = false;
    dummyNodeNone.id = 4;

    // Add to vNodes
    vNodes.push_back(&dummyNodeXthin);
    vNodes.push_back(&dummyNodeGraphene);
    vNodes.push_back(&dummyNodeCmpct);
    vNodes.push_back(&dummyNodeNone);

    // Initialize Nodes
    GetNodeSignals().InitializeNode(&dummyNodeXthin);
    GetNodeSignals().InitializeNode(&dummyNodeGraphene);
    GetNodeSignals().InitializeNode(&dummyNodeCmpct);
    GetNodeSignals().InitializeNode(&dummyNodeNone);

    // Create basic Inv for requesting blocks. This simulates an entry in the request manager for a block
    // download.
    uint256 hash = GetRandHash();
    uint256 randhash = GetRandHash();
    CInv inv(MSG_BLOCK, hash);
    CInv inv_cmpct(MSG_CMPCT_BLOCK, hash);

    uint64_t nTime = GetTime();
    dosMan.ClearBanned();

    // Test the General Case: Chain synced, graphene ON, Thinblocks ON, Cmpct ON
    // This should return a Graphene block.
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "get_xthin");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "get_grblk");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeCmpct, inv_cmpct) == true);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg) == "cmpctblock");
    thinrelay.ClearBlockRelayTimer(inv_cmpct.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Test the General Case: Chain synced, graphene ON, Thinblocks ON, Cmpct ON
    // This should return a Graphene block.
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "get_xthin");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "get_grblk");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeCmpct, inv_cmpct) == true);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg) == "cmpctblock");
    thinrelay.ClearBlockRelayTimer(inv_cmpct.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Thin timer disabled: Chain synced, graphene ON, Thinblocks OFF, Cmpct ON
    // Although the timer would have been on because one relay type was off, here we explicity turn off
    // the timer. We should still be able to request a Graphene, or Cmpct or regular block
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    SetArg("-preferential-timer", "0");
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    // This test would generally cause a request for a "get_xthin", however xthins is not on and
    // the timer is off which results in a full block request.
    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "getdata");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "get_grblk");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeCmpct, inv_cmpct) == true);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg) == "cmpctblock");
    thinrelay.ClearBlockRelayTimer(inv_cmpct.hash);
    CleanupAll(vNodes);

    inv.type = MSG_BLOCK;
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");
    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);
    SetArg("-preferential-timer", "10000"); // reset from 0

    // Chain NOT synced with any nodes, graphene ON, Thinblocks ON, Cmpct ON
    IsChainNearlySyncdSet(false);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "getdata");

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "getdata");

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd: No graphene nodes, No Thinblock nodes, No Cmpct nodes, Thinblocks OFF, Graphene OFF, CMPCT OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemovePeers(&dummyNodeNone);
    CleanupAll(vNodes);

    // Chains IS sync'd: HAVE graphene nodes, NO Thinblock nodes, No Cmpt nodes, Graphene OFF, Thinblocks OFF,
    // Compactblocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpt nodes, Graphene OFF, Thinblocks ON, Cmpctblocks
    // OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpt nodes, Graphene OFF, Thinblocks OFF,
    // Cmpctblocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Graphene OFF, Thinblocks ON,
    // Cmpctblocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Graphene OFF, Thinblocks ON,
    // Cmpctblocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Graphene OFF, Thinblocks OFF,
    // Cmpctblocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene OFF, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene OFF, Cmpct
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);


    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks OFF, Graphene ON, Cmpt
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks OFF, Graphene ON, Cmpt
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks ON, Graphene ON, Cmpt
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks ON, Graphene ON, Cmpt
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Thinblocks ON, Graphene ON, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "get_grblk");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "get_grblk");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "get_grblk");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "get_grblk");

    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks ON, Graphene OFF, Cmpct
    // OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "get_xthin");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks ON, Graphene ON, Cmpct ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "get_xthin");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, No Thinblock nodes, Have Cmpct Nodes, Thinblocks OFF, Graphene OFF, Cmpct
    // ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeCmpct, inv_cmpct) == true);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg) == "cmpctblock");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    // Chains IS sync'd,  NO graphene nodes, No Thinblock nodes, Have Cmpct Nodes, Thinblocks ON, Graphene ON, Cmpct ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeCmpct, inv_cmpct) == true);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg) == "cmpctblock");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);


    /******************************
     * Check full blocks are downloaded when no block announcements come from a graphene, thinblock or cmpct peer.
     * The timers in this case will be disabled so we will immediately download a full block.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Have Cmpct node, Thinblocks ON, Graphene ON, Cmpct
    // ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);


    /******************************
     * Check full blocks are downloaded when graphene is off but thin type timer is exceeded
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Have Cmpct nodes, Graphene OFF, Thinblocks ON,
    // Cmpct ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the xthin timer should be triggered
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);


    /******************************
     * Check a full block is downloaded when Graphene timer is exceeded but then we get an announcement
     * from a graphene peer (thinblocks is OFF), and then request from that graphene peer before we
     * request from any others.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks OFF, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for graphene
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    thinrelay.AddBlockInFlight(&dummyNodeGraphene, randhash, NetMsgType::GRAPHENEBLOCK);
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg) == "getdata");

    CleanupAll(vNodes);

    /******************************
     * Check another graphene block is downloaded when Graphene timer is exceeded and then we get an announcement
     * from a graphene peer (thinblocks is ON), and then request from that graphene peer before we
     * request from any others.
     * However this time we already have a grapheneblock in flight but we end up downloading another graphene block
     * because we haven't exceeded the limit on number of thintype blocks in flight.
     * Then proceed to request more thintype blocks until the limit is exceeded.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON, Cmpct OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should succeed as should successive requests up until the limit of thintype requests in flight
    inv.hash = GetRandHash();
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(dummyNodeGraphene.vSendMsg.size() == 1);

    inv.hash = GetRandHash();
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(dummyNodeGraphene.vSendMsg.size() == 2);

    inv.hash = GetRandHash();
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(dummyNodeGraphene.vSendMsg.size() == 3);

    inv.hash = GetRandHash();
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(dummyNodeGraphene.vSendMsg.size() == 4);

    inv.hash = GetRandHash();
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == true);
    BOOST_CHECK(dummyNodeGraphene.vSendMsg.size() == 5);

    // Now move the clock ahead so that the timers are exceeded and we should now
    // download an xthin
    SetMockTime(nTime + 20);
    dummyNodeXthin.vSendMsg.clear();
    inv.hash = GetRandHash();
    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(dummyNodeXthin.vSendMsg.size() == 1);

    // Try to send a 7th block. It should fail to send as it's above the limit of thintype blocks in flight.
    inv.hash = GetRandHash();
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == false);
    BOOST_CHECK(dummyNodeGraphene.vSendMsg.size() == 5);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    /******************************
     * Check a Xthin is is downloaded when thinblock timer is exceeded but then we get an announcement
     * from a thinblock peer, and then request from that thinblock peer before we request from any others.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for xthin
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "getdata");

    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    /******************************
     * Check a Xthin is is downloaded when thinblock timer is exceeded but then we get an announcement
     * from a thinblock peer, and then request from that thinblock peer before we request from any others.
     * However this time we already have an xthin in flight for this peer so we end up downloading a full block.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddPeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for xthin
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    thinrelay.AddBlockInFlight(&dummyNodeXthin, randhash, NetMsgType::XTHINBLOCK);
    BOOST_CHECK(requester.RequestBlock(&dummyNodeXthin, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg) == "getdata");


    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);

    /******************************
     * Check a full block is is downloaded when thinblock timer is exceeded but then we get an announcement
     * from a cmpctblock peer, and then request from that cmpctnblock peer before we request from any others.
     * However this time we already have an cmpctblk in flight for this peer so we end up downloading a full block.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Have Cmpct nodes, Thinblocks OFF, Graphene OFF,
    // Cmpct ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddPeers(&dummyNodeGraphene);
    thinrelay.AddPeers(&dummyNodeXthin);
    thinrelay.AddCompactBlockPeer(&dummyNodeCmpct);
    thinrelay.AddPeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for cmpctblock
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    thinrelay.AddBlockInFlight(&dummyNodeCmpct, randhash, NetMsgType::CMPCTBLOCK);
    BOOST_CHECK(requester.RequestBlock(&dummyNodeCmpct, inv) == true);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg) == "getdata");


    thinrelay.ClearBlockRelayTimer(inv.hash);
    CleanupAll(vNodes);


    // Final cleanup: Unset mocktime
    SetMockTime(0);
    requester.MapBlocksInFlightClear();

    // remove from vNodes
    vNodes.erase(remove(vNodes.begin(), vNodes.end(), &dummyNodeGraphene), vNodes.end());
    vNodes.erase(remove(vNodes.begin(), vNodes.end(), &dummyNodeNone), vNodes.end());
    vNodes.erase(remove(vNodes.begin(), vNodes.end(), &dummyNodeCmpct), vNodes.end());
    vNodes.erase(remove(vNodes.begin(), vNodes.end(), &dummyNodeXthin), vNodes.end());
}
BOOST_AUTO_TEST_SUITE_END()
