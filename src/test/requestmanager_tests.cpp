// Copyright (c) 2016 Bitcoin Unlimited Developers
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

#include <atomic>
#include <boost/test/unit_test.hpp>
#include <sstream>
#include <string.h>

// Return the netmessage string for a block/xthin/graphene request
static std::string NetMessage(std::deque<CSerializeData> &_vSendMsg)
{
    BOOST_CHECK(_vSendMsg.size() != 0);
    if (_vSendMsg.size() == 0)
        return "none";

    CInv inv_result;
    CSerializeData &data = _vSendMsg.front();
    CDataStream ssCommand(SER_NETWORK, PROTOCOL_VERSION);
    ssCommand.insert(ssCommand.begin(), &data[4], &data[16]);
    _vSendMsg.pop_front();

    // if it's a getdata then we need to find out what type
    if (ssCommand.str().compare("getdata"))
    {
        CDataStream ssInv(SER_NETWORK, PROTOCOL_VERSION);
        ssInv.insert(ssInv.begin(), &data[24], &data[60]);

        CInv inv;
        ssInv >> inv;

        if (inv.type == MSG_BLOCK)
            return "getdata";
        if (inv.type == MSG_CMPCT_BLOCK)
            return "cmpctblock";
    }

    return ssCommand.str();
}

static void ClearThinBlocksInFlight(CNode &node, CInv &inv) { thinrelay.ClearThinTypeBlockInFlight(&node, inv.hash); }
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
    dummyNodeXthin.id = 1;
    dummyNodeGraphene.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeGraphene.state_incoming = ConnectionStateIncoming::READY;
    dummyNodeGraphene.state_outgoing = ConnectionStateOutgoing::READY;
    dummyNodeGraphene.nServices |= NODE_GRAPHENE;
    dummyNodeGraphene.id = 2;
    dummyNodeCmpct.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeCmpct.state_incoming = ConnectionStateIncoming::READY;
    dummyNodeCmpct.state_outgoing = ConnectionStateOutgoing::READY;
    dummyNodeCmpct.fSupportsCompactBlocks = true;
    dummyNodeGraphene.id = 3;
    dummyNodeNone.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeNone.state_incoming = ConnectionStateIncoming::READY;
    dummyNodeNone.state_outgoing = ConnectionStateOutgoing::READY;
    dummyNodeNone.id = 4;

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

    uint64_t nTime = GetTime();
    dosMan.ClearBanned();

    // Chain NOT sync'd with any nodes, graphene ON, Thinblocks ON, Cmpct ON
    IsChainNearlySyncdSet(false);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeCmpct, true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("getdata") != 0);

    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("getdata") != 0);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    ClearThinBlocksInFlight(dummyNodeCmpct, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeCmpct);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd: No graphene nodes, No Thinblock nodes, No Cmpct nodes, Thinblocks OFF, Graphene OFF, CMPCT OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd: HAVE graphene nodes, NO Thinblock nodes, No Cmpt nodes, Graphene OFF, Thinblocks OFF,
    // Compactblocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpt nodes, Graphene OFF, Thinblocks ON, Cmpctblocks
    // OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpt nodes, Graphene OFF, Thinblocks OFF,
    // Cmpctblocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Graphene OFF, Thinblocks ON,
    // Cmpctblocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Graphene OFF, Thinblocks ON,
    // Cmpctblocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Graphene OFF, Thinblocks OFF,
    // Cmpctblocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene OFF, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks OFF, Graphene OFF, Cmpct
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);


    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks OFF, Graphene ON, Cmpt
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks OFF, Graphene ON, Cmpt
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks ON, Graphene ON, Cmpt
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, No Cmpctblock nodes, Thinblocks ON, Graphene ON, Cmpt
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Thinblocks ON, Graphene ON, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd, HAVE graphene nodes, NO Thinblock nodes, No Cmpct nodes, Thinblocks OFF, Graphene ON, Cmpct
    // blocks OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks ON, Graphene OFF, Cmpct
    // OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("get_xthin") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, No Cmpct Nodes, Thinblocks ON, Graphene ON, Cmpct ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("get_xthin") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, No Thinblock nodes, Have Cmpct Nodes, Thinblocks OFF, Graphene OFF, Cmpct
    // ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeCmpct, true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeCmpct, inv);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg).compare("cmpctblock") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeCmpct, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeCmpct);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    // Chains IS sync'd,  NO graphene nodes, No Thinblock nodes, Have Cmpct Nodes, Thinblocks ON, Graphene ON, Cmpct ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeCmpct, true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeCmpct, inv);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg).compare("cmpctblock") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeCmpct, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeCmpct);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);


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
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeCmpct, true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);
    thinrelay.RemoveThinTypePeers(&dummyNodeCmpct);


    /******************************
     * Check full blocks are downloaded when graphene is off but thin type timer is exceeded
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Have Cmpct nodes, Graphene OFF, Thinblocks ON,
    // Cmpct ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
thinrelay.AddThinTypePeers(&dummyNodeCmpct, true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the xthin timer should be triggered
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    ClearThinBlocksInFlight(dummyNodeCmpct, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeCmpct);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);


    /******************************
     * Check a full block is downloaded when Graphene timer is exceeded but then we get an announcement
     * from a graphene peer (thinblocks is OFF), and then request from that graphene peer before we
     * request from any others.
     * However this time we already have a grapheneblock in flight for this peer so we end up downloading a full block.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks OFF, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for graphene
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    thinrelay.AddThinTypeBlockInFlight(&dummyNodeGraphene, randhash, NetMsgType::GRAPHENEBLOCK);
    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("getdata") != 0);
    thinrelay.ClearThinTypeBlockInFlight(&dummyNodeGraphene, randhash);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    /******************************
     * Check an Xthin is downloaded when Graphene timer is exceeded but then we get an announcement
     * from a graphene peer (thinblocks is ON), and then request from that graphene peer before we
     * request from any others.
     * However this time we already have a grapheneblock in flight for this peer so we end up downloading a thinblock.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON, Cmpct OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    SetBoolArg("-use-compactblocks", false);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for both xthin and graphene
    randhash = GetRandHash();
    thinrelay.AddThinTypeBlockInFlight(&dummyNodeGraphene, randhash, NetMsgType::GRAPHENEBLOCK);
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == false);

    // Now move the clock ahead so that the timers are exceeded and we should now
    // download an xthin
    SetMockTime(nTime + 20);
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("get_xthin") != 0);
    thinrelay.ClearThinTypeBlockInFlight(&dummyNodeGraphene, randhash);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);


    /******************************
     * Check a Xthin is is downloaded when thinblock timer is exceeded but then we get an announcement
     * from a thinblock peer, and then request from that thinblock peer before we request from any others.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for xthin
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("get_xthin") != 0);

    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

    /******************************
     * Check a Xthin is is downloaded when thinblock timer is exceeded but then we get an announcement
     * from a thinblock peer, and then request from that thinblock peer before we request from any others.
     * However this time we already have an xthin in flight for this peer so we end up downloading a full block.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for xthin
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    thinrelay.AddThinTypeBlockInFlight(&dummyNodeXthin, randhash, NetMsgType::XTHINBLOCK);
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("getdata") != 0);


    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    requester.MapBlocksInFlightClear();
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);

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
    thinrelay.AddThinTypePeers(&dummyNodeGraphene);
    thinrelay.AddThinTypePeers(&dummyNodeXthin);
    thinrelay.AddThinTypePeers(&dummyNodeCmpct, true);
    thinrelay.AddThinTypePeers(&dummyNodeNone);

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for cmpctblock
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    thinrelay.AddThinTypeBlockInFlight(&dummyNodeCmpct, randhash, NetMsgType::CMPCTBLOCK);
    requester.RequestBlock(&dummyNodeCmpct, inv);
    BOOST_CHECK(NetMessage(dummyNodeCmpct.vSendMsg).compare("getdata") != 0);


    thinrelay.ClearBlockRelayTimer(inv.hash);
    ClearThinBlocksInFlight(dummyNodeGraphene, inv);
    ClearThinBlocksInFlight(dummyNodeNone, inv);
    ClearThinBlocksInFlight(dummyNodeCmpct, inv);
    ClearThinBlocksInFlight(dummyNodeXthin, inv);
    thinrelay.RemoveThinTypePeers(&dummyNodeGraphene);
    thinrelay.RemoveThinTypePeers(&dummyNodeCmpct);
    thinrelay.RemoveThinTypePeers(&dummyNodeXthin);
    thinrelay.RemoveThinTypePeers(&dummyNodeNone);


    // Final cleanup: Unset mocktime
    SetMockTime(0);
}
BOOST_AUTO_TEST_SUITE_END()
