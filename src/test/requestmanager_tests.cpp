// Copyright (c) 2016 Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
    const CSerializeData &data = _vSendMsg.front();
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.insert(ss.begin(), &data[4], &data[16]);

    _vSendMsg.pop_front();
    return ss.str();
}

static void ClearBlocksInFlight(CNode &node)
{
    node.mapGrapheneBlocksInFlight.clear();
    node.mapThinBlocksInFlight.clear();
}

BOOST_FIXTURE_TEST_SUITE(requestmanager_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(blockrequest_tests)
{
    // Test the requesting of blocks/graphenblocks/thinblocks with varying node configurations.
    // This tests all the code paths within RequestBlock() in the request manager.

    // create dummy test addrs
    CAddress addr_xthin(ipaddress(0xa0b0c001, 10000));
    CAddress addr_graphene(ipaddress(0xa0b0c002, 10001));
    CAddress addr_none(ipaddress(0xa0b0c003, 10002));

    // create nodes
    CNode dummyNodeXthin(INVALID_SOCKET, addr_xthin, "", true);
    CNode dummyNodeGraphene(INVALID_SOCKET, addr_graphene, "", true);
    CNode dummyNodeNone(INVALID_SOCKET, addr_none, "", true);
    dummyNodeXthin.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeXthin.fSuccessfullyConnected = true;
    dummyNodeXthin.nServices |= NODE_XTHIN;
    dummyNodeXthin.id = 1;
    dummyNodeGraphene.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeGraphene.fSuccessfullyConnected = true;
    dummyNodeGraphene.nServices |= NODE_GRAPHENE;
    dummyNodeGraphene.id = 2;
    dummyNodeNone.nVersion = MIN_PEER_PROTO_VERSION;
    dummyNodeNone.fSuccessfullyConnected = true;
    dummyNodeNone.id = 3;

    // Initialize Nodes
    GetNodeSignals().InitializeNode(&dummyNodeXthin);
    GetNodeSignals().InitializeNode(&dummyNodeGraphene);
    GetNodeSignals().InitializeNode(&dummyNodeNone);

    // Create basic Inv for requesting blocks. This simulates an entry in the request manager for a block
    // download.
    uint256 hash = GetRandHash();
    uint256 randhash = GetRandHash();
    CInv inv(MSG_BLOCK, hash);

    uint64_t nTime = GetTime();
    dosMan.ClearBanned();

    // Chain NOT sync'd with any nodes, graphene ON, Thinblocks ON
    IsChainNearlySyncdSet(false);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("getdata") != 0);

    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("getdata") != 0);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }
    // Chains IS sync'd,  No graphene nodes, No Thinblock nodes, Thinblocks OFF, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
    }
    // Chains IS sync'd,  HAVE graphene nodes, NO Thinblock nodes, Thinblocks OFF, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", false);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
    }
    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
    }
    // Chains IS sync'd,  HAVE graphene nodes, NO Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
    }
    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, Thinblocks OFF, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
    }
    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, Thinblocks OFF, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
    }
    // Chains IS sync'd,  NO graphene nodes, NO Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
    }
    // Chains IS sync'd,  HAVE graphene nodes, NO Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    vNodes.push_back(&dummyNodeGraphene);
    vNodes.push_back(&dummyNodeNone);
    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
    }
    // Chains IS sync'd,  HAVE graphene nodes, NO Thinblock nodes, Thinblocks OFF, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", false);
    BOOST_CHECK(IsThinBlocksEnabled() == false);
    vNodes.push_back(&dummyNodeGraphene);
    vNodes.push_back(&dummyNodeNone);
    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
    }
    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    vNodes.push_back(&dummyNodeGraphene);
    vNodes.push_back(&dummyNodeXthin);
    vNodes.push_back(&dummyNodeNone);

    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }

    // Chains IS sync'd,  NO graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("get_xthin") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
    }

    /******************************
     * Check full blocks are downloaded when no block announcements come from a graphene or thinblock peer
     * before the timers hit their limit.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }
    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for graphene
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the graphene timer is exceeded
    SetMockTime(nTime + 20);

    // The second request should fail because but the thinblock timer will be tripped
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the thinblock timer is exceeded
    SetMockTime(nTime + 40);

    requester.RequestBlock(&dummyNodeNone, inv);
    BOOST_CHECK(NetMessage(dummyNodeNone.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }

    /******************************
     * Check full blocks are downloaded when graphene is off but thinblock timer is exceeded
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }

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

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }

    /******************************
     * Check Xthin is downloaded when graphene timer is exceeded and we have a block available from an
     * Xthin peer
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for both xthin and graphene
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the graphene timer is exceeded and we should now
    // download an Xthin
    SetMockTime(nTime + 20);
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("get_xthin") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }

    /******************************
     * Check a Graphene block is downloaded when Graphene timer is exceeded but then we get an announcement
     * from a graphene peer, and then request from that graphene peer before we request from any others.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for both xthin and graphene
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timers are exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("get_graphene") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }

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
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for graphene
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    AddGrapheneBlockInFlight(&dummyNodeGraphene, randhash);
    requester.RequestBlock(&dummyNodeGraphene, inv);
    BOOST_CHECK(NetMessage(dummyNodeGraphene.vSendMsg).compare("getdata") != 0);
    ClearGrapheneBlockInFlight(&dummyNodeGraphene, randhash);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }

    /******************************
     * Check an Xthin is downloaded when Graphene timer is exceeded but then we get an announcement
     * from a graphene peer (thinblocks is ON), and then request from that graphene peer before we
     * request from any others.
     * However this time we already have a grapheneblock in flight for this peer so we end up downloading a thinblock.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene ON
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", true);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }
    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for both xthin and graphene
    randhash = GetRandHash();
    AddGrapheneBlockInFlight(&dummyNodeGraphene, randhash);
    BOOST_CHECK(requester.RequestBlock(&dummyNodeGraphene, inv) == false);

    // Now move the clock ahead so that the timers are exceeded and we should now
    // download an xthin
    SetMockTime(nTime + 20);
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("get_xthin") != 0);
    ClearGrapheneBlockInFlight(&dummyNodeGraphene, randhash);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }

    /******************************
     * Check a Xthin is is downloaded when thinblock timer is exceeded but then we get an announcement
     * from a thinblock peer, and then request from that thinblock peer before we request from any others.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }
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

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }
    /******************************
     * Check a Xthin is is downloaded when thinblock timer is exceeded but then we get an announcement
     * from a thinblock peer, and then request from that thinblock peer before we request from any others.
     * However this time we already have an xthin in flight for this peer so we end up downloading a full block.
     */

    // Chains IS sync'd,  HAVE graphene nodes, HAVE Thinblock nodes, Thinblocks ON, Graphene OFF
    IsChainNearlySyncdSet(true);
    SetBoolArg("-use-grapheneblocks", false);
    SetBoolArg("-use-thinblocks", true);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(&dummyNodeGraphene);
        vNodes.push_back(&dummyNodeXthin);
        vNodes.push_back(&dummyNodeNone);
    }

    // Set mocktime
    nTime = GetTime();
    SetMockTime(nTime);

    // The first request should fail but the timers should be triggered for xthin
    BOOST_CHECK(requester.RequestBlock(&dummyNodeNone, inv) == false);

    // Now move the clock ahead so that the timer is exceeded and we should now
    // download a full block
    SetMockTime(nTime + 20);
    randhash = GetRandHash();
    AddThinBlockInFlight(&dummyNodeXthin, randhash);
    requester.RequestBlock(&dummyNodeXthin, inv);
    BOOST_CHECK(NetMessage(dummyNodeXthin.vSendMsg).compare("getdata") != 0);

    thindata.ClearThinBlockTimer(inv.hash);
    graphenedata.ClearGrapheneBlockTimer(inv.hash);
    ClearBlocksInFlight(dummyNodeGraphene);
    ClearBlocksInFlight(dummyNodeNone);
    ClearBlocksInFlight(dummyNodeXthin);
    {
        LOCK(cs_vNodes);
        vNodes.pop_back();
        vNodes.pop_back();
        vNodes.pop_back();
    }


    // Final cleanup: Unset mocktime
    SetMockTime(0);
}
BOOST_AUTO_TEST_SUITE_END()
