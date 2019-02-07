// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"

#include "blockrelay/graphene.h"
#include "blockrelay/thinblock.h"
#include "chainparams.h"
#include "clientversion.h"
#include "dosman.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "protocol.h"
#include "sync.h"
#include "timedata.h"
#include "tweak.h"
#include "ui_interface.h"
#include "unlimited.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/lexical_cast.hpp>

#include <univalue.h>

extern CTweak<double> dMinLimiterTxFee;
extern CTweak<double> dMaxLimiterTxFee;

using namespace std;

UniValue getconnectioncount(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getconnectioncount\n"
                            "\nReturns the number of connections to other nodes.\n"
                            "\nResult:\n"
                            "n          (numeric) The connection count\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getconnectioncount", "") + HelpExampleRpc("getconnectioncount", ""));

    LOCK2(cs_main, cs_vNodes);

    return (int)vNodes.size();
}

UniValue ping(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("ping\n"
                            "\nRequests that a ping be sent to all other nodes, to measure ping time.\n"
                            "Results provided in getpeerinfo, pingtime and pingwait fields are decimal seconds.\n"
                            "Ping command is handled in queue with all other commands, so it measures processing "
                            "backlog, not just network ping.\n"
                            "\nExamples:\n" +
                            HelpExampleCli("ping", "") + HelpExampleRpc("ping", ""));

    // Request that each node send a ping during next message processing pass
    LOCK2(cs_main, cs_vNodes);

    for (CNode *pNode : vNodes)
    {
        pNode->fPingQueued = true;
    }

    return NullUniValue;
}

static void CopyNodeStats(std::vector<CNodeStats> &vstats)
{
    vstats.clear();

    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    for (CNode *pnode : vNodes)
    {
        CNodeStats stats;
        pnode->copyStats(stats);
        vstats.push_back(stats);
    }
}

UniValue getpeerinfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getpeerinfo [peer IP address]\n"
            "\nReturns data about each connected network node as a json array of objects.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"id\": n,                       (numeric) Peer index\n"
            "    \"addr\":\"host:port\",            (string) The ip address and port of the peer\n"
            "    \"addrlocal\":\"ip:port\",         (string) local address\n"
            "    \"services\":\"xxxxxxxxxxxxxxxx\", (string) The services offered\n"
            "    \"relaytxes\":true|false,        (boolean) Whether peer has asked us to relay transactions to it\n"
            "    \"lastsend\": ttt,               (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the "
            "last send\n"
            "    \"lastrecv\": ttt,               (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the "
            "last receive\n"
            "    \"bytessent\": n,                (numeric) The total bytes sent\n"
            "    \"bytesrecv\": n,                (numeric) The total bytes received\n"
            "    \"conntime\": ttt,               (numeric) The connection time in seconds since epoch (Jan 1 1970 "
            "GMT)\n"
            "    \"timeoffset\": ttt,             (numeric) The time offset in seconds\n"
            "    \"pingtime\": n,                 (numeric) ping time\n"
            "    \"minping\": n,                  (numeric) minimum observed ping time\n"
            "    \"pingwait\": n,                 (numeric) ping wait\n"
            "    \"version\": v,                  (numeric) The peer version, such as 7001\n"
            "    \"subver\": \"/BUCash:x.x.x/\",    (string) The string version\n"
            "    \"inbound\": true|false,         (boolean) Inbound (true) or Outbound (false)\n"
            "    \"startingheight\": n,           (numeric) The starting height (block) of the peer\n"
            "    \"banscore\": n,                 (numeric) The ban score\n"
            "    \"synced_headers\": n,           (numeric) The last header we have in common with this peer\n"
            "    \"synced_blocks\": n,            (numeric) The last block we have in common with this peer\n"
            "    \"inflight\": [\n"
            "       n,                            (numeric) The heights of blocks we're currently asking from this "
            "peer\n"
            "       ...\n"
            "    ]\n"
            "    \"whitelisted\": true|false,     (boolean) Whether we have whitelisted this peer, preventing us from "
            "banning the node due to misbehavior, though we may still disconnect it\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getpeerinfo", "") + HelpExampleRpc("getpeerinfo", ""));

    LOCK(cs_main);

    vector<CNodeStats> vstats;
    CopyNodeStats(vstats);

    UniValue ret(UniValue::VARR);
    CNodeRef node;
    if (params.size() > 0) // BU allow params to this RPC call
    {
        string nodeName = params[0].get_str();
        node = FindLikelyNode(nodeName);
        if (!node)
            throw runtime_error("Unknown node");
    }

    for (const CNodeStats &stats : vstats)
    {
        if (!node || (node->id == stats.nodeid))
        {
            UniValue obj(UniValue::VOBJ);
            CNodeStateStats statestats;
            bool fStateStats = GetNodeStateStats(stats.nodeid, statestats);
            obj.pushKV("id", stats.nodeid);
            obj.pushKV("addr", stats.addrName);
            if (!(stats.addrLocal.empty()))
                obj.pushKV("addrlocal", stats.addrLocal);
            obj.pushKV("services", strprintf("%016x", stats.nServices));
            obj.pushKV("relaytxes", stats.fRelayTxes);
            obj.pushKV("lastsend", stats.nLastSend);
            obj.pushKV("lastrecv", stats.nLastRecv);
            obj.pushKV("bytessent", stats.nSendBytes);
            obj.pushKV("bytesrecv", stats.nRecvBytes);
            obj.pushKV("conntime", stats.nTimeConnected);
            obj.pushKV("timeoffset", stats.nTimeOffset);
            obj.pushKV("pingtime", stats.dPingTime);
            obj.pushKV("minping", stats.dPingMin);
            if (stats.dPingWait > 0.0)
                obj.pushKV("pingwait", stats.dPingWait);
            obj.pushKV("version", stats.nVersion);
            // Use the sanitized form of subver here, to avoid tricksy remote peers from
            // corrupting or modifiying the JSON output by putting special characters in
            // their ver message.
            obj.pushKV("subver", stats.cleanSubVer);
            obj.pushKV("inbound", stats.fInbound);
            obj.pushKV("startingheight", stats.nStartingHeight);
            if (fStateStats)
            {
                obj.pushKV("banscore", statestats.nMisbehavior);
                obj.pushKV("synced_headers", statestats.nSyncHeight);
                obj.pushKV("synced_blocks", statestats.nCommonHeight);
                UniValue heights(UniValue::VARR);
                for (int height : statestats.vHeightInFlight)
                {
                    heights.push_back(height);
                }
                obj.pushKV("inflight", heights);
            }
            obj.pushKV("whitelisted", stats.fWhitelisted);

            CNodeRef snode = FindLikelyNode(stats.addrName);

            if (snode)
            {
                UniValue xmap_enc(UniValue::VOBJ);
                for (auto kv : snode->xVersion.xmap)
                {
                    xmap_enc.pushKV(strprintf("%016llx", kv.first), HexStr(kv.second).c_str());
                }
                obj.pushKV("xversion_map", xmap_enc);
            }
            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue addnode(const UniValue &params, bool fHelp)
{
    string strCommand;
    if (params.size() == 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() != 2 || (strCommand != "onetry" && strCommand != "add" && strCommand != "remove"))
        throw runtime_error("addnode \"node\" \"add|remove|onetry\"\n"
                            "\nAttempts add or remove a node from the addnode list.\n"
                            "Or try a connection to a node once.\n"
                            "\nArguments:\n"
                            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
                            "2. \"command\"  (string, required) 'add' to add a node to the list, 'remove' to remove a "
                            "node from the list, 'onetry' to try a connection to the node once\n"
                            "\nExamples:\n" +
                            HelpExampleCli("addnode", "\"192.168.0.6:8333\" \"onetry\"") +
                            HelpExampleRpc("addnode", "\"192.168.0.6:8333\", \"onetry\""));

    string strNode = params[0].get_str();

    if (strCommand == "onetry")
    {
        CAddress addr;
        // NOTE: Using RPC "addnode <node> onetry" ignores both the "maxconnections"
        //      and "maxoutconnections" limits and can cause both to be exceeded.
        OpenNetworkConnection(addr, false, NULL, strNode.c_str());
        return NullUniValue;
    }

    LOCK(cs_vAddedNodes);
    vector<string>::iterator it = vAddedNodes.begin();
    for (; it != vAddedNodes.end(); it++)
        if (strNode == *it)
            break;

    if (strCommand == "add")
    {
        if (it != vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Node already added");
        vAddedNodes.push_back(strNode);
    }
    else if (strCommand == "remove")
    {
        if (it == vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
        vAddedNodes.erase(it);
    }

    return NullUniValue;
}

UniValue disconnectnode(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("disconnectnode \"node\" \n"
                            "\nImmediately disconnects from the specified node.\n"
                            "\nArguments:\n"
                            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
                            "\nExamples:\n" +
                            HelpExampleCli("disconnectnode", "\"192.168.0.6:8333\"") +
                            HelpExampleRpc("disconnectnode", "\"192.168.0.6:8333\""));

    CNodeRef node = FindNodeRef(params[0].get_str());
    if (!node)
        throw JSONRPCError(RPC_CLIENT_NODE_NOT_CONNECTED, "Node not found in connected nodes");

    node->fDisconnect = true;

    return NullUniValue;
}

UniValue getaddednodeinfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getaddednodeinfo dns ( \"node\" )\n"
            "\nReturns information about the given added node, or all added nodes\n"
            "(note that onetry addnodes are not listed here)\n"
            "If dns is false, only a list of added nodes will be provided,\n"
            "otherwise connected information will also be available.\n"
            "\nArguments:\n"
            "1. dns        (boolean, required) If false, only a list of added nodes will be provided, otherwise "
            "connected information will also be available.\n"
            "2. \"node\"   (string, optional) If provided, return information about this specific node, otherwise all "
            "nodes are returned.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"addednode\" : \"192.168.0.201\",          (string) The node ip address\n"
            "    \"connected\" : true|false,               (boolean) If connected\n"
            "    \"addresses\" : [\n"
            "       {\n"
            "         \"address\" : \"192.168.0.201:8333\",  (string) The bitcoin server host and port\n"
            "         \"connected\" : \"outbound\"           (string) connection, inbound or outbound\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getaddednodeinfo", "true") + HelpExampleCli("getaddednodeinfo", "true \"192.168.0.201\"") +
            HelpExampleRpc("getaddednodeinfo", "true, \"192.168.0.201\""));

    bool fDns = params[0].get_bool();

    list<string> laddedNodes(0);
    if (params.size() == 1)
    {
        LOCK(cs_vAddedNodes);
        for (const std::string &strAddNode : vAddedNodes)
            laddedNodes.push_back(strAddNode);
    }
    else
    {
        string strNode = params[1].get_str();
        LOCK(cs_vAddedNodes);
        for (const std::string &strAddNode : vAddedNodes)
        {
            if (strAddNode == strNode)
            {
                laddedNodes.push_back(strAddNode);
                break;
            }
        }
        if (laddedNodes.size() == 0)
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
    }

    UniValue ret(UniValue::VARR);
    if (!fDns)
    {
        for (const std::string &strAddNode : laddedNodes)
        {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("addednode", strAddNode);
            ret.push_back(obj);
        }
        return ret;
    }

    list<pair<string, vector<CService> > > laddedAddreses(0);
    for (const std::string &strAddNode : laddedNodes)
    {
        vector<CService> vservNode(0);
        if (Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), 0, fNameLookup))
            laddedAddreses.push_back(make_pair(strAddNode, vservNode));
        else
        {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("addednode", strAddNode);
            obj.pushKV("connected", false);
            UniValue addresses(UniValue::VARR);
            obj.pushKV("addresses", addresses);
        }
    }

    LOCK(cs_vNodes);
    for (list<pair<string, vector<CService> > >::iterator it = laddedAddreses.begin(); it != laddedAddreses.end(); it++)
    {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("addednode", it->first);

        UniValue addresses(UniValue::VARR);
        bool fConnected = false;
        for (const CService &addrNode : it->second)
        {
            bool fFound = false;
            UniValue node(UniValue::VOBJ);
            node.pushKV("address", addrNode.ToString());
            for (CNode *pnode : vNodes)
            {
                if (pnode->addr == addrNode)
                {
                    fFound = true;
                    fConnected = true;
                    node.pushKV("connected", pnode->fInbound ? "inbound" : "outbound");
                    break;
                }
            }
            if (!fFound)
                node.pushKV("connected", "false");
            addresses.push_back(node);
        }
        obj.pushKV("connected", fConnected);
        obj.pushKV("addresses", addresses);
        ret.push_back(obj);
    }

    return ret;
}

UniValue getnettotals(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
            "getnettotals\n"
            "\nReturns information about network traffic, including bytes in, bytes out,\n"
            "and current time.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalbytesrecv\": n,                      (numeric) Total bytes received\n"
            "  \"totalbytessent\": n,                      (numeric) Total bytes sent\n"
            "  \"timemillis\": t,                          (numeric) Total cpu time\n"
            "  \"uploadtarget\": {\n"
            "    \"timeframe\": n,                         (numeric) Length of the measuring timeframe in seconds\n"
            "    \"target\": n,                            (numeric) Target in bytes\n"
            "    \"target_reached\": true|false,           (boolean) True if target is reached\n"
            "    \"serve_historical_blocks\": true|false,  (boolean) True if serving historical blocks\n"
            "    \"bytes_left_in_cycle\": t,               (numeric) Bytes left in current time cycle\n"
            "    \"time_left_in_cycle\": t                 (numeric) Seconds left in current time cycle\n"
            "  }\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getnettotals", "") + HelpExampleRpc("getnettotals", ""));

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("totalbytesrecv", CNode::GetTotalBytesRecv());
    obj.pushKV("totalbytessent", CNode::GetTotalBytesSent());
    obj.pushKV("timemillis", GetTimeMillis());

    UniValue outboundLimit(UniValue::VOBJ);
    outboundLimit.pushKV("timeframe", CNode::GetMaxOutboundTimeframe());
    outboundLimit.pushKV("target", CNode::GetMaxOutboundTarget());
    outboundLimit.pushKV("target_reached", CNode::OutboundTargetReached(false));
    outboundLimit.pushKV("serve_historical_blocks", !CNode::OutboundTargetReached(true));
    outboundLimit.pushKV("bytes_left_in_cycle", CNode::GetOutboundTargetBytesLeft());
    outboundLimit.pushKV("time_left_in_cycle", CNode::GetMaxOutboundTimeLeftInCycle());
    obj.pushKV("uploadtarget", outboundLimit);
    return obj;
}

static UniValue GetNetworksInfo()
{
    UniValue networks(UniValue::VARR);
    for (int n = 0; n < NET_MAX; ++n)
    {
        enum Network network = static_cast<enum Network>(n);
        if (network == NET_UNROUTABLE)
            continue;
        proxyType proxy;
        UniValue obj(UniValue::VOBJ);
        GetProxy(network, proxy);
        obj.pushKV("name", GetNetworkName(network));
        obj.pushKV("limited", IsLimited(network));
        obj.pushKV("reachable", IsReachable(network));
        obj.pushKV("proxy", proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string());
        obj.pushKV("proxy_randomize_credentials", proxy.randomize_credentials);
        networks.push_back(obj);
    }
    return networks;
}

static UniValue GetThinBlockStats()
{
    UniValue obj(UniValue::VOBJ);
    bool enabled = IsThinBlocksEnabled();
    obj.pushKV("enabled", enabled);
    if (enabled)
    {
        obj.pushKV("summary", thindata.ToString());
        obj.pushKV("mempool_limiter", thindata.MempoolLimiterBytesSavedToString());
        obj.pushKV("inbound_percent", thindata.InBoundPercentToString());
        obj.pushKV("outbound_percent", thindata.OutBoundPercentToString());
        obj.pushKV("response_time", thindata.ResponseTimeToString());
        obj.pushKV("validation_time", thindata.ValidationTimeToString());
        obj.pushKV("outbound_bloom_filters", thindata.OutBoundBloomFiltersToString());
        obj.pushKV("inbound_bloom_filters", thindata.InBoundBloomFiltersToString());
        obj.pushKV("thin_block_size", thindata.ThinBlockToString());
        obj.pushKV("thin_full_tx", thindata.FullTxToString());
        obj.pushKV("rerequested", thindata.ReRequestedTxToString());
    }
    return obj;
}

static UniValue GetGrapheneStats()
{
    UniValue obj(UniValue::VOBJ);
    bool enabled = IsGrapheneBlockEnabled();
    obj.pushKV("enabled", enabled);
    if (enabled)
    {
        obj.pushKV("summary", graphenedata.ToString());
        obj.pushKV("inbound_percent", graphenedata.InBoundPercentToString());
        obj.pushKV("outbound_percent", graphenedata.OutBoundPercentToString());
        obj.pushKV("response_time", graphenedata.ResponseTimeToString());
        obj.pushKV("validation_time", graphenedata.ValidationTimeToString());
        obj.pushKV("filter", graphenedata.FilterToString());
        obj.pushKV("iblt", graphenedata.IbltToString());
        obj.pushKV("rank", graphenedata.RankToString());
        obj.pushKV("graphene_block_size", graphenedata.GrapheneBlockToString());
        obj.pushKV("graphene_additional_tx_size", graphenedata.AdditionalTxToString());
        obj.pushKV("rerequested", graphenedata.ReRequestedTxToString());
    }
    return obj;
}

static UniValue GetCompactBlockStats()
{
    UniValue obj(UniValue::VOBJ);
    bool enabled = IsCompactBlocksEnabled();
    obj.pushKV("enabled", enabled);
    if (enabled)
    {
        obj.pushKV("summary", compactdata.ToString());
        obj.pushKV("mempool_limiter", compactdata.MempoolLimiterBytesSavedToString());
        obj.pushKV("inbound_percent", compactdata.InBoundPercentToString());
        obj.pushKV("outbound_percent", compactdata.OutBoundPercentToString());
        obj.pushKV("response_time", compactdata.ResponseTimeToString());
        obj.pushKV("validation_time", compactdata.ValidationTimeToString());
        obj.pushKV("compact_block_size", compactdata.CompactBlockToString());
        obj.pushKV("compact_full_tx", compactdata.FullTxToString());
        obj.pushKV("rerequested", compactdata.ReRequestedTxToString());
    }
    return obj;
}

UniValue getnetworkinfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getnetworkinfo\n"
            "Returns an object containing various state info regarding P2P networking.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,                    (numeric) the server version\n"
            "  \"subversion\": \"/BUCash:x.x.x/\",      (string) the server subversion string\n"
            "  \"protocolversion\": xxxxx,            (numeric) the protocol version\n"
            "  \"localservices\": \"xxxxxxxxxxxxxxxx\", (string) the services we offer to the network\n"
            "  \"timeoffset\": xxxxx,                 (numeric) the time offset\n"
            "  \"connections\": xxxxx,                (numeric) the number of connections\n"
            "  \"networks\": [                        (array) information per network\n"
            "    {\n"
            "      \"name\": \"xxx\",                   (string) network (ipv4, ipv6 or onion)\n"
            "      \"limited\": true|false,           (boolean) is the network limited using -onlynet?\n"
            "      \"reachable\": true|false,         (boolean) is the network reachable?\n"
            "      \"proxy\": \"host:port\"             (string) the proxy that is used for this network, or empty if "
            "none\n"
            "      \"proxy_randomize_credentials\": true|false,  (string) Whether randomized credentials are used\n"
            "    }\n"
            "  ,...\n"
            "  ],\n"
            "  \"relayfee\": x.xxxxxxxx,              (numeric) minimum relay fee for non-free transactions in " +
            CURRENCY_UNIT +
            "/kB\n"
            "  \"minlimitertxfee\": x.xxxx,           (numeric) fee (in satoshi/byte) below which transactions are "
            "considered free and subject to limitfreerelay\n"
            "  \"maxlimitertxfee\": x.xxxx,           (numeric) fee (in satoshi/byte) above which transactions are "
            "always relayed\n"
            "  \"localaddresses\": [                  (array) list of local addresses\n"
            "    {\n"
            "      \"address\": \"xxxx\",               (string) network address\n"
            "      \"port\": xxx,                     (numeric) network port\n"
            "      \"score\": xxx                     (numeric) relative score\n"
            "    }\n"
            "  ,...\n"
            "  ]\n"
            "  \"thinblockstats\": \"...\"              (string) thin block related statistics \n"
            "  \"compactblockstats\": \"...\"           (string) compact block related statistics \n"
            "  \"grapheneblockstats\": \"...\"          (string) graphene block related statistics \n"
            "  \"warnings\": \"...\"                    (string) any network warnings (such as alert messages) \n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getnetworkinfo", "") + HelpExampleRpc("getnetworkinfo", ""));

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", CLIENT_VERSION);
    // BUIP005: special subversion
    obj.pushKV("subversion", FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, BUComments));
    obj.pushKV("protocolversion", PROTOCOL_VERSION);
    obj.pushKV("localservices", strprintf("%016x", nLocalServices));
    obj.pushKV("timeoffset", GetTimeOffset());
    obj.pushKV("connections", (int)vNodes.size());
    obj.pushKV("networks", GetNetworksInfo());
    obj.pushKV("relayfee", ValueFromAmount(::minRelayTxFee.GetFeePerK()));
    obj.pushKV("minlimitertxfee", strprintf("%.4f", dMinLimiterTxFee.Value()));
    obj.pushKV("maxlimitertxfee", strprintf("%.4f", dMaxLimiterTxFee.Value()));
    UniValue localAddresses(UniValue::VARR);
    {
        LOCK(cs_mapLocalHost);
        for (const PAIRTYPE(CNetAddr, LocalServiceInfo) & item : mapLocalHost)
        {
            UniValue rec(UniValue::VOBJ);
            rec.pushKV("address", item.first.ToString());
            rec.pushKV("port", item.second.nPort);
            rec.pushKV("score", item.second.nScore);
            localAddresses.push_back(rec);
        }
    }
    obj.pushKV("localaddresses", localAddresses);
    obj.pushKV("thinblockstats", GetThinBlockStats());
    obj.pushKV("compactblockstats", GetCompactBlockStats());
    obj.pushKV("grapheneblockstats", GetGrapheneStats());
    obj.pushKV("warnings", GetWarnings("statusbar"));
    return obj;
}


UniValue clearblockstats(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error("clearblockstats\n"
                            "\nClears statistics related to compression blocks such as xthin or graphene.\n"
                            "\nArguments: None\n"
                            "\nExample:\n" +
                            HelpExampleCli("clearblockstats", ""));

    if (IsThinBlocksEnabled())
        thindata.ClearThinBlockStats();
    if (IsGrapheneBlockEnabled())
        graphenedata.ClearGrapheneBlockStats();

    return NullUniValue;
}


UniValue setban(const UniValue &params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() < 2 || (strCommand != "add" && strCommand != "remove"))
        throw runtime_error("setban \"ip(/netmask)\" \"add|remove\" (bantime) (absolute)\n"
                            "\nAttempts add or remove a IP/Subnet from the banned list.\n"
                            "\nArguments:\n"
                            "1. \"ip(/netmask)\" (string, required) The IP/Subnet (see getpeerinfo for nodes ip) with "
                            "a optional netmask (default is /32 = single ip)\n"
                            "2. \"command\"      (string, required) 'add' to add a IP/Subnet to the list, 'remove' to "
                            "remove a IP/Subnet from the list\n"
                            "3. \"bantime\"      (numeric, optional) time in seconds how long (or until when if "
                            "[absolute] is set) the ip is banned (0 or empty means using the default time of 24h which "
                            "can also be overwritten by the -bantime startup argument)\n"
                            "4. \"absolute\"     (boolean, optional) If set, the bantime must be a absolute timestamp "
                            "in seconds since epoch (Jan 1 1970 GMT)\n"
                            "\nExamples:\n" +
                            HelpExampleCli("setban", "\"192.168.0.6\" \"add\" 86400") +
                            HelpExampleCli("setban", "\"192.168.0.0/24\" \"add\"") +
                            HelpExampleRpc("setban", "\"192.168.0.6\", \"add\" 86400"));

    CSubNet subNet;
    CNetAddr netAddr;
    bool isSubnet = false;

    if (params[0].get_str().find("/") != string::npos)
        isSubnet = true;

    if (!isSubnet)
        netAddr = CNetAddr(params[0].get_str());
    else
        subNet = CSubNet(params[0].get_str());

    if (!(isSubnet ? subNet.IsValid() : netAddr.IsValid()))
        throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Invalid IP/Subnet");

    if (strCommand == "add")
    {
        if (isSubnet ? dosMan.IsBanned(subNet) : dosMan.IsBanned(netAddr))
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: IP/Subnet already banned");

        int64_t banTime = 0; // use standard bantime if not specified
        if (params.size() >= 3 && !params[2].isNull())
            banTime = params[2].get_int64();

        bool absolute = false;
        if (params.size() == 4 && params[3].isTrue())
            absolute = true;

        isSubnet ? dosMan.Ban(subNet, BanReasonManuallyAdded, banTime, absolute) :
                   dosMan.Ban(netAddr, BanReasonManuallyAdded, banTime, absolute);

        // disconnect possible nodes
        if (!isSubnet)
            subNet = CSubNet(netAddr);
        // BU: Since we need to mark any nodes in subNet for disconnect, atomically mark all nodes at once
        DisconnectSubNetNodes(subNet);
    }
    else if (strCommand == "remove")
    {
        if (!(isSubnet ? dosMan.Unban(subNet) : dosMan.Unban(netAddr)))
            throw JSONRPCError(RPC_MISC_ERROR, "Error: Unban failed");
    }

    return NullUniValue;
}

UniValue listbanned(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "listbanned\n"
            "\nList all banned IPs/Subnets.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\" : \"192.168.0.201/32\",    (string) The banned IP/Subnet with netmask (/32 = single ip)\n"
            "    \"banned_until\" : ttt,              (numeric) The ban expiration time in seconds since epoch (Jan 1 "
            "1970 GMT)\n"
            "    \"ban_created\" : ttt                (numeric) The ban creation time in seconds since epoch (Jan 1 "
            "1970 GMT)\n"
            "    \"ban_reason\" : \"node misbehaving\"  (string) The reason the ban was created\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("listbanned", "") + HelpExampleRpc("listbanned", ""));

    banmap_t banMap;
    dosMan.GetBanned(banMap);

    UniValue bannedAddresses(UniValue::VARR);
    for (banmap_t::iterator it = banMap.begin(); it != banMap.end(); it++)
    {
        CBanEntry banEntry = (*it).second;
        UniValue rec(UniValue::VOBJ);
        rec.pushKV("address", (*it).first.ToString());
        rec.pushKV("banned_until", banEntry.nBanUntil);
        rec.pushKV("ban_created", banEntry.nCreateTime);
        rec.pushKV("ban_reason", banEntry.banReasonToString());

        bannedAddresses.push_back(rec);
    }

    return bannedAddresses;
}

UniValue clearbanned(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("clearbanned\n"
                            "\nClear all banned IPs.\n"
                            "\nExamples:\n" +
                            HelpExampleCli("clearbanned", "") + HelpExampleRpc("clearbanned", ""));

    dosMan.ClearBanned();
    // We also need to clear the number of incoming reqs from this node, or we'll just instantly ban again
    LOCK(cs_mapInboundConnectionTracker);
    mapInboundConnectionTracker.clear();
    return NullUniValue;
}

static const CRPCCommand commands[] = {
    //  category              name                      actor (function)         okSafeMode
    //  --------------------- ------------------------  -----------------------  ----------
    {"network", "getconnectioncount", &getconnectioncount, true}, {"network", "ping", &ping, true},
    {"network", "getpeerinfo", &getpeerinfo, true}, {"network", "addnode", &addnode, true},
    {"network", "disconnectnode", &disconnectnode, true}, {"network", "getaddednodeinfo", &getaddednodeinfo, true},
    {"network", "getnettotals", &getnettotals, true}, {"network", "getnetworkinfo", &getnetworkinfo, true},
    {"network", "setban", &setban, true}, {"network", "listbanned", &listbanned, true},
    {"network", "clearblockstats", &clearblockstats, true}, {"network", "clearbanned", &clearbanned, true},
};

void RegisterNetRPCCommands(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}
