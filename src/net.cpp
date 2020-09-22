// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

// must include first to ensure FD_SETSIZE is correctly set
// otherwise the default of 64 may be used on Windows
#include "compat.h"

#include "net.h"

#include "addrman.h"
#include "blockrelay/blockrelay_common.h"
#include "blockrelay/graphene.h"
#include "blockrelay/mempool_sync.h"
#include "chainparams.h"
#include "connmgr.h"
#include "consensus/consensus.h"
#include "crypto/common.h"
#include "dosman.h"
#include "hashwrapper.h"
#include "iblt.h"
#include "primitives/transaction.h"
#include "requestManager.h"
#include "ui_interface.h"
#include "unlimited.h"
#include "utilstrencodings.h"
#include "xversionkeys.h"

extern CTweak<bool> ignoreNetTimeouts;

#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#ifdef USE_UPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/miniwget.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

#include <boost/filesystem.hpp>
#include <chrono>
#include <thread>

#include <math.h>

#include <bitnodes.h>

// Dump addresses to peers.dat and banlist.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

// Dump addresses to peers.dat and banlist.dat every 10 seconds
#define UPDATE_MISBEHAVIOR_INTERVAL 10

// We add a random period time (0 to 1 seconds) to feeler connections to prevent synchronization.
#define FEELER_SLEEP_WINDOW 1

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW version.
#ifdef WIN32
#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 23
#endif
#endif

extern std::atomic<bool> fRescan;
extern bool fReindex;
extern CTxMemPool mempool;
extern CTweak<uint64_t> grapheneMinVersionSupported;
extern CTweak<uint64_t> grapheneMaxVersionSupported;

bool ShutdownRequested();

using namespace std;

namespace
{
// BU replaced this with a configuration option: const int MAX_OUTBOUND_CONNECTIONS = 8;
const int MAX_FEELER_CONNECTIONS = 1;

struct ListenSocket
{
    SOCKET socket;
    bool whitelisted;
    ListenSocket(SOCKET _socket, bool _whitelisted) : socket(_socket), whitelisted(_whitelisted) {}
};
}

//
// Global state variables
//
bool fDiscover = true;
bool fListen = true;
uint64_t nLocalServices = NODE_NETWORK;
// BU moved to globals.cpp: CCriticalSection cs_mapLocalHost;
// BU moved to globals.cpp: map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfLimited[NET_MAX] = {};
static CNode *pnodeLocalHost = nullptr;
uint64_t nLocalHostNonce = 0;
static std::vector<ListenSocket> vhListenSocket;
extern CAddrMan addrman;
int nMaxConnections = DEFAULT_MAX_PEER_CONNECTIONS;
int nMinXthinNodes = MIN_XTHIN_NODES;

bool fAddressesInitialized = false;

// BU moved to global.cpp
// extern vector<CNode*> vNodes;
// extern CCriticalSection cs_vNodes;
// map<CInv, CDataStream> mapRelay;
// CCriticalSection cs_mapRelay;

extern deque<string> vOneShots;
extern CCriticalSection cs_vOneShots;

extern set<CNetAddr> setservAddNodeAddresses;
extern CCriticalSection cs_setservAddNodeAddresses;

extern vector<std::string> vAddedNodes;
extern CCriticalSection cs_vAddedNodes;

// BITCOINUNLIMITED START
extern vector<std::string> vUseDNSSeeds;
extern CCriticalSection cs_vUseDNSSeeds;
// BITCOINUNLIMITED END

extern CSemaphore *semOutbound;
extern CSemaphore *semOutboundAddNode; // BU: separate semaphore for -addnodes
std::condition_variable messageHandlerCondition;
std::mutex wakeableDelayMutex;

// BU  Connection Slot mitigation - used to determine how many connection attempts over time
extern std::map<CNetAddr, ConnectionHistory> mapInboundConnectionTracker;
extern CCriticalSection cs_mapInboundConnectionTracker;

// Mempool synchronization
extern uint64_t lastMempoolSync;
extern uint64_t lastMempoolSyncClear;

// Signals for message handling
extern CNodeSignals g_signals;
CNodeSignals &GetNodeSignals() { return g_signals; }
void AddOneShot(const std::string &strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

unsigned short GetListenPort() { return (unsigned short)(GetArg("-port", Params().GetDefaultPort())); }
// find 'best' local address for a particular peer
bool GetLocal(CService &addr, const CNetAddr *paddrPeer)
{
    if (!fListen)
        return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(cs_mapLocalHost);
        for (map<CNetAddr, LocalServiceInfo>::iterator it = mapLocalHost.begin(); it != mapLocalHost.end(); it++)
        {
            int nScore = (*it).second.nScore;
            int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore))
            {
                addr = CService((*it).first, (*it).second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

//! Convert the pnSeeds6 array into usable address objects.
static std::vector<CAddress> convertSeed6(const std::vector<SeedSpec6> &vSeedsIn)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7 * 24 * 60 * 60;
    std::vector<CAddress> vSeedsOut;
    vSeedsOut.reserve(vSeedsIn.size());
    FastRandomContext rng;
    for (std::vector<SeedSpec6>::const_iterator i(vSeedsIn.begin()); i != vSeedsIn.end(); ++i)
    {
        struct in6_addr ip;
        memcpy(&ip, i->addr, sizeof(ip));
        CAddress addr(CService(ip, i->port));
        addr.nTime = GetTime() - rng.randrange(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
    return vSeedsOut;
}

// get best local address for a particular peer as a CAddress
// Otherwise, return the unroutable 0.0.0.0 but filled in with
// the normal parameters, since the IP may be changed to a useful
// one by discovery.
CAddress GetLocalAddress(const CNetAddr *paddrPeer)
{
    CAddress ret(CService("0.0.0.0", GetListenPort()), 0);
    CService addr;
    if (GetLocal(addr, paddrPeer))
    {
        ret = CAddress(addr);
    }
    ret.nServices = nLocalServices;
    ret.nTime = GetAdjustedTime();
    return ret;
}

int GetnScore(const CService &addr)
{
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == LOCAL_NONE)
        return 0;
    return mapLocalHost[addr].nScore;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(CNode *pnode)
{
    return fDiscover && pnode->addr.IsRoutable() && pnode->addrLocal.IsRoutable() &&
           !IsLimited(pnode->addrLocal.GetNetwork());
}

// pushes our own address to a peer
void AdvertiseLocal(CNode *pnode)
{
    if (fListen && pnode->fSuccessfullyConnected)
    {
        CAddress addrLocal = GetLocalAddress(&pnode->addr);
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        FastRandomContext rng;
        if (IsPeerAddrLocalGood(pnode) &&
            (!addrLocal.IsRoutable() || rng.randbits((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8 : 2) == 0))
        {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable())
        {
            // BU logs too often: LOGA("AdvertiseLocal: advertising address %s\n", addrLocal.ToString());
            FastRandomContext insecure_rand;
            pnode->PushAddress(addrLocal, rng);
        }
    }
}

// learn a new local address
bool AddLocal(const CService &addr, int nScore)
{
    if (!addr.IsRoutable())
        return false;

    if (!fDiscover && nScore < LOCAL_MANUAL)
        return false;

    if (IsLimited(addr))
        return false;

    LOGA("AddLocal(%s,%i)\n", addr.ToString(), nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore)
        {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
    }

    return true;
}

bool AddLocal(const CNetAddr &addr, int nScore) { return AddLocal(CService(addr, GetListenPort()), nScore); }
bool RemoveLocal(const CService &addr)
{
    LOCK(cs_mapLocalHost);
    LOGA("RemoveLocal(%s)\n", addr.ToString());
    mapLocalHost.erase(addr);
    return true;
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void SetLimited(enum Network net, bool fLimited)
{
    if (net == NET_UNROUTABLE)
        return;
    LOCK(cs_mapLocalHost);
    vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return vfLimited[net];
}

bool IsLimited(const CNetAddr &addr) { return IsLimited(addr.GetNetwork()); }
/** vote for a local address */
bool SeenLocal(const CService &addr)
{
    {
        LOCK(cs_mapLocalHost);
        if (mapLocalHost.count(addr) == 0)
            return false;
        mapLocalHost[addr].nScore++;
    }
    return true;
}


/** check whether a given address is potentially local */
bool IsLocal(const CService &addr)
{
    LOCK(cs_mapLocalHost);
    return mapLocalHost.count(addr) > 0;
}

/** check whether a given network is one we can probably connect to */
bool IsReachable(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr &addr)
{
    enum Network net = addr.GetNetwork();
    return IsReachable(net);
}

// Initialize static CNode variables used in static CNode functions.
std::atomic<uint64_t> CNode::nTotalBytesRecv{0};
std::atomic<uint64_t> CNode::nTotalBytesSent{0};
std::atomic<uint64_t> CNode::nMaxOutboundLimit{0};
std::atomic<uint64_t> CNode::nMaxOutboundTimeframe{60 * 60 * 24}; // 1 day
std::atomic<uint64_t> CNode::nMaxOutboundCycleStartTime{0};
std::atomic<uint64_t> CNode::nMaxOutboundTotalBytesSentInCycle{0};

// BU: FindNode() functions enforce holding of cs_vNodes lock to prevent use-after-free errors
static CNode *FindNode(const CNetAddr &ip)
{
    AssertLockHeld(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if ((CNetAddr)pnode->addr == ip)
            return (pnode);
    }
    return nullptr;
}

static CNode *FindNode(const std::string &addrName)
{
    AssertLockHeld(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if (pnode->addrName == addrName)
            return (pnode);
    }
    return nullptr;
}

static CNode *FindNode(const CService &addr)
{
    AssertLockHeld(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if ((CService)pnode->addr == addr)
            return (pnode);
    }
    return nullptr;
}

CNodeRef FindNodeRef(const std::string &addrName)
{
    LOCK(cs_vNodes);
    return CNodeRef(FindNode(addrName));
}

CNodeRef FindNodeRef(const CNetAddr &ip)
{
    LOCK(cs_vNodes);
    return CNodeRef(FindNode(ip));
}


int DisconnectSubNetNodes(const CSubNet &subNet)
{
    int nDisconnected = 0;
    LOCK(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if (subNet.Match((CNetAddr)pnode->addr))
        {
            pnode->fDisconnect = true;
            nDisconnected++;
        }
    }

    // return the number of nodes in this subnet marked for disconnection
    return nDisconnected;
}

CNode *ConnectNode(CAddress addrConnect, const char *pszDest, bool fCountFailure)
{
    if (pszDest == nullptr)
    {
        if (IsLocal(addrConnect))
            return nullptr;

        // BU: Add lock on cs_vNodes as FindNode now requries it to prevent potential use-after-free errors
        LOCK(cs_vNodes);
        // Look for an existing connection
        CNode *pnode = FindNode((CService)addrConnect);
        if (pnode)
        {
            // NOTE: Because ConnectNode adds a reference, we don't have to protect the returned CNode* like for
            // FindNode
            pnode->AddRef();
            return pnode;
        }
    }

    /// debug print
    LOG(NET, "trying connection %s lastseen=%.1fhrs\n", pszDest ? pszDest : addrConnect.ToString(),
        pszDest ? 0.0 : (double)(GetAdjustedTime() - addrConnect.nTime) / 3600.0);

    // Connect
    SOCKET hSocket = INVALID_SOCKET;
    bool proxyConnectionFailed = false;
    if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, Params().GetDefaultPort(), nConnectTimeout,
                      &proxyConnectionFailed) :
                  ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed))
    {
        if (!IsSelectableSocket(hSocket))
        {
            LOG(NET, "Cannot create connection: non-selectable socket created (fd >= FD_SETSIZE ?)\n");
            CloseSocket(hSocket);
            return nullptr;
        }

        addrman.Attempt(addrConnect, fCountFailure);

        // Add node
        CNode *pnode = new CNode(hSocket, addrConnect, pszDest ? pszDest : "", false);
        pnode->AddRef();

        {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
        }

        pnode->nTimeConnected = GetTime();

        return pnode;
    }
    else if (!proxyConnectionFailed)
    {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        addrman.Attempt(addrConnect, fCountFailure);
    }

    return nullptr;
}

void CNode::CloseSocketDisconnect()
{
    // if this is an outbound node that was not added via addenode then decrement the counter.
    if (fAutoOutbound)
        requester.nOutbound--;

    fDisconnect = true;
    if (hSocket != INVALID_SOCKET)
    {
        LOG(NET, "disconnecting peer %s\n", GetLogName());
        CloseSocket(hSocket);
    }

    // Purge any noderef's in the priority message queues relating to this peer. If we don't
    // remove the node references here then we won't be able to complete the disconnection.
    {
        LOCK(cs_prioritySendQ);
        auto it = vPrioritySendQ.begin();
        while (it != vPrioritySendQ.end())
        {
            if (this == it->get())
                it = vPrioritySendQ.erase(it);
            else
                it++;
        }
    }
    {
        LOCK(cs_priorityRecvQ);
        auto it = vPriorityRecvQ.begin();
        while (it != vPriorityRecvQ.end())
        {
            if (this == it->first.get())
                it = vPriorityRecvQ.erase(it);
            else
                it++;
        }
    }

    // in case this fails, we'll empty the recv buffer when the CNode is deleted
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (lockRecv)
    {
        vRecvMsg.clear();
        vRecvMsg_handshake.clear();
    }
}

void CNode::PushVersion()
{
    int nBestHeight = g_signals.GetHeight().get_value_or(0);

    int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0", 0)));
    CAddress addrMe = GetLocalAddress(&addr);
    GetRandBytes((unsigned char *)&nLocalHostNonce, sizeof(nLocalHostNonce));
    if (fLogIPs)
    {
        LOG(NET, "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION,
            nBestHeight, addrMe.ToString(), addrYou.ToString(), id);
    }
    else
    {
        LOG(NET, "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight,
            addrMe.ToString(), id);
    }

    // BUIP005 add our special subversion string
    PushMessage(NetMsgType::VERSION, PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe, nLocalHostNonce,
        FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, BUComments), nBestHeight,
        !GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY));
    tVersionSent = GetTime();
}


#undef X
#define X(name) stats.name = name
void CNode::copyStats(CNodeStats &stats)
{
    stats.nodeid = this->GetId();
    X(nServices);
    X(fRelayTxes);
    X(nLastSend);
    X(nLastRecv);
    X(nTimeConnected);
    X(nStopwatchConnected);
    X(nTimeOffset);
    X(addrName);
    X(nVersion);
    X(cleanSubVer);
    X(fInbound);
    X(nStartingHeight);
    {
        LOCK(cs_vSend);
        X(nSendBytes);
    }
    {
        LOCK(cs_vRecvMsg);
        X(nRecvBytes);
    }
    X(fWhitelisted);
    X(fSupportsCompactBlocks);

    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer.
    // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
    // since pingtime does not update until the ping is complete, which might take a while.
    // So, if a ping is taking an unusually long time in flight,
    // the caller can immediately detect that this is happening.
    int64_t nPingUsecWait = 0;
    if ((0 != nPingNonceSent) && (0 != nPingUsecStart))
    {
        nPingUsecWait = GetStopwatchMicros() - nPingUsecStart;
    }

    // Raw ping time is in microseconds, but show it to user as whole seconds (Bitcoin users should be well used to
    // small numbers with many decimal places by now :)
    stats.dPingTime = (((double)nPingUsecTime) / 1e6);
    stats.dPingMin = (((double)nMinPingUsecTime) / 1e6);
    stats.dPingWait = (((double)nPingUsecWait) / 1e6);

    // Leave string empty if addrLocal invalid (not filled in yet)
    stats.addrLocal = addrLocal.IsValid() ? addrLocal.ToString() : "";
}
#undef X

static bool IsMessageOversized(CNetMessage &msg)
{
    if (maxMessageSizeMultiplier && msg.in_data && (msg.hdr.nMessageSize > BLOCKSTREAM_CORE_MAX_BLOCK_SIZE) &&
        (msg.hdr.nMessageSize > (maxMessageSizeMultiplier * excessiveBlockSize)))
    {
        // TODO: warn if too many nodes are doing this
        return true;
    }
    return false;
}

static bool IsPriorityMsg(std::string strCommand)
{
    if (!IsChainNearlySyncd())
        return false;

    // Most traffic is INV, TX or GETDATA so check that first to prevent us from having to
    // to evaluate, for every message, the long if statement that follows this one.
    if (strCommand == NetMsgType::INV || strCommand == NetMsgType::TX || strCommand == NetMsgType::GETDATA)
        return false;

    // Various messages types that are considered priority.
    // NOTE: The absence of BLOCK is not by accident. Full BLOCK messages are problematic for priority queuing.
    //       as it is difficult to know the state of the peer in terms of whether they are sync'd to the chain.
    //       We for instance don't want to be sending BLOCK's as priority messages if the peer is only in the process
    //       of initial sync. Also, BLOCK's can be quite large and we don't want them to be dominating our priority
    //       sending process. We prefer small objects that can be forwarded with one SockeSendData() attempt.
    if (strCommand == NetMsgType::HEADERS || strCommand == NetMsgType::GRAPHENEBLOCK ||
        strCommand == NetMsgType::GET_GRAPHENE || strCommand == NetMsgType::GRAPHENETX ||
        strCommand == NetMsgType::GET_GRAPHENE_RECOVERY || strCommand == NetMsgType::GRAPHENE_RECOVERY ||
        strCommand == NetMsgType::GET_GRAPHENETX || strCommand == NetMsgType::GET_XTHIN ||
        strCommand == NetMsgType::GET_THIN || strCommand == NetMsgType::XTHINBLOCK ||
        strCommand == NetMsgType::THINBLOCK || strCommand == NetMsgType::XBLOCKTX ||
        strCommand == NetMsgType::GET_XBLOCKTX || strCommand == NetMsgType::XPEDITEDREQUEST ||
        strCommand == NetMsgType::XPEDITEDBLK || strCommand == NetMsgType::XPEDITEDTXN ||
        strCommand == NetMsgType::CMPCTBLOCK || strCommand == NetMsgType::GETBLOCKTXN ||
        strCommand == NetMsgType::BLOCKTXN || strCommand == NetMsgType::BLOCK)
    {
        return true;
    }
    else
    {
        return false;
    }
}
bool CNode::ReceiveMsgBytes(const char *pch, unsigned int nBytes)
{
    AssertLockHeld(cs_vRecvMsg);
    while (nBytes > 0)
    {
        // Absorb network data.
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(pch, nBytes);
        else
            handled = msg.readData(pch, nBytes);

        if (handled < 0)
            return false;

        if (IsMessageOversized(msg))
        {
            fDisconnect = true;
            LOG(NET, "Oversized message from peer=%i, disconnecting\n", GetId());
            return false;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete())
        {
            bool fSendLowPriority = true;

            msg.nStopwatch = GetStopwatchMicros();
            msg.nTime = GetTimeMicros();

            // Connection slot attack mitigation.  We don't want to add useful bytes for outgoing INV, PING, ADDR,
            // VERSION or VERACK messages since attackers will often just connect and listen to INV messages.
            // We want to make sure that connected nodes are doing useful work in sending us data or requesting data.
            std::string strCommand = msg.hdr.GetCommand();
            if (strCommand != NetMsgType::PONG && strCommand != NetMsgType::PING && strCommand != NetMsgType::ADDR &&
                strCommand != NetMsgType::VERSION && strCommand != NetMsgType::VERACK)
            {
                nActivityBytes.fetch_add(msg.hdr.nMessageSize);

                // If the message is a priority message then move it into the priority queue.
                if (IsPriorityMsg(strCommand))
                {
                    LOCK(cs_priorityRecvQ);
                    // As a safeguard on some peer trying to dominate our networking
                    // don't allow unlimited simultaneous priority messages. There should in general only be
                    // one priority message at any one time with the exception of block HEADERS announcements
                    // which can come relatively close together in time.
                    if (vPriorityRecvQ.size() <= 5)
                    {
                        // Move the this message to the priority queue.
                        vPriorityRecvQ.push_back(std::make_pair<CNodeRef, CNetMessage>(CNodeRef(this), std::move(msg)));
                        msg = CNetMessage(GetMagic(Params()), SER_NETWORK, nRecvVersion);

                        LOG(PRIORITYQ, "Receive Queue: pushed %s to the priority queue, %d bytes, peer(%d)\n",
                            strCommand, vPriorityRecvQ.back().second.hdr.nMessageSize, this->GetId());
                        // Indicate we have a priority message to process
                        fPriorityRecvMsg.store(true);
                        fSendLowPriority = false;
                    }
                }
            }

            if (fSendLowPriority)
            {
                if (strCommand == NetMsgType::VERSION || strCommand == NetMsgType::XVERSION ||
                    strCommand == NetMsgType::XVERSION_OLD || strCommand == NetMsgType::XVERACK_OLD ||
                    strCommand == NetMsgType::VERACK)
                {
                    vRecvMsg_handshake.push_back(std::move(msg));
                }
                else
                {
                    vRecvMsg.push_back(std::move(msg));
                }
                msg = CNetMessage(GetMagic(Params()), SER_NETWORK, nRecvVersion);
            }
            messageHandlerCondition.notify_one();
        }
    }

    return true;
}

int CNetMessage::readHeader(const char *pch, unsigned int nBytes)
{
    // copy data to temporary parsing buffer
    unsigned int nRemaining = 24 - nHdrPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24)
        return nCopy;

    // deserialize to CMessageHeader
    try
    {
        hdrbuf >> hdr;
    }
    catch (const std::exception &)
    {
        return -1;
    }

    // BU this is handled in the readHeader caller
    // reject messages larger than MAX_SIZE
    // if (hdr.nMessageSize > MAX_SIZE)
    //    return -1;

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char *pch, unsigned int nBytes)
{
    unsigned int nRemaining = hdr.nMessageSize - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy)
    {
        // Allocate up to 256 KiB ahead, but never more than the total message size.
        vRecv.resize(std::min(hdr.nMessageSize, nDataPos + nCopy + 256 * 1024));
    }

    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}


// requires LOCK(cs_vSend), BU: returns > 0 if any data was sent, 0 if nothing accomplished.
int SocketSendData(CNode *pnode, bool fSendTwo = false) EXCLUSIVE_LOCKS_REQUIRED(pnode->cs_vSend)
{
    AssertLockHeld(pnode->cs_vSend);
    // BU This variable is incremented if something happens.  If it is zero at the bottom of the loop, we delay.  This
    // solves spin loop issues where the select does not block but no bytes can be transferred (traffic shaping limited,
    // for example).
    int progress = 0;
    uint32_t nMsgSent = 0;
    // Make sure we haven't already been asked to disconnect
    if (pnode->fDisconnect)
        return progress;

    std::deque<CSerializeData>::iterator it;
    while (!pnode->vSendMsg.empty() || !pnode->vLowPrioritySendMsg.empty())
    {
        if (!pnode->vSendMsg.empty())
        {
            it = pnode->vSendMsg.begin();
        }
        else
        {
            // Move a message from the lower priority queue to the higher priority queue
            // and then continue. This keeps all active message sending from the priority queue
            // only and prevents us from putting the next priority message in front of any that
            // has already been partially sent.
            pnode->vSendMsg.push_back(std::move(*pnode->vLowPrioritySendMsg.begin()));
            pnode->vLowPrioritySendMsg.pop_front();
            continue;
        }

        const CSerializeData &data = *it;
        if (data.size() <= 0)
        {
            pnode->vSendMsg.pop_front();
            LOGA("ERROR:  Trying to send message but data size was %d nSendOffset was %d nSendSize was %d\n",
                data.size(), pnode->nSendOffset, pnode->nSendSize);
            continue;
        }
        DbgAssert(data.size() > pnode->nSendOffset, );
        int amt2Send = min((int64_t)(data.size() - pnode->nSendOffset), sendShaper.available(SEND_SHAPER_MIN_FRAG));
        if (amt2Send == 0)
            break;
        SOCKET hSocket = pnode->hSocket;
        if (hSocket == INVALID_SOCKET)
            break;
        int nBytes = send(hSocket, &data[pnode->nSendOffset], amt2Send, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (nBytes > 0)
        {
            progress++; // BU
            pnode->bytesSent += nBytes; // BU stats
            int64_t tmp = GetTime();
            pnode->sendGap << (tmp - pnode->nLastSend);
            pnode->nLastSend = tmp;
            pnode->nSendBytes += nBytes;
            pnode->nSendOffset += nBytes;
            pnode->RecordBytesSent(nBytes);
            bool empty = !sendShaper.leak(nBytes);
            if (pnode->nSendOffset == data.size())
            {
                pnode->nSendOffset = 0;
                pnode->nSendSize.fetch_sub(data.size());
                pnode->vSendMsg.pop_front();

                // If this is a priority send then just send two messages, then stop sending more.
                nMsgSent++;
                if (fSendTwo && nMsgSent >= 2)
                    break;
            }
            else
            {
                // could not send full message; stop sending more
                break;
            }
            if (empty)
                break; // Exceeded our send budget, stop sending more
        }
        else
        {
            if (nBytes < 0)
            {
                // error
                int nErr = WSAGetLastError();
                if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                {
                    LOG(NET, "socket send error '%s' to %s\n", NetworkErrorString(nErr), pnode->GetLogName());
                    pnode->fDisconnect = true;
                }
            }
            // couldn't send anything at all
            break;
        }
    }

    if (pnode->vSendMsg.empty() && pnode->vLowPrioritySendMsg.empty())
    {
        if (pnode->nSendOffset != 0 || pnode->nSendSize != 0)
            LOGA("ERROR: One or more values were not Zero - nSendOffset was %d nSendSize was %d\n", pnode->nSendOffset,
                pnode->nSendSize);
        DbgAssert(pnode->nSendOffset == 0, );
        DbgAssert(pnode->nSendSize == 0, );
    }

    return progress;
}

#if 0 // Not currenly used
static bool ReverseCompareNodeMinPingTime(const CNodeRef &a, const CNodeRef &b)
{
    return a->nMinPingUsecTime > b->nMinPingUsecTime;
}

static bool ReverseCompareNodeTimeConnected(const CNodeRef &a, const CNodeRef &b)
{
    return a->nTimeConnected > b->nTimeConnected;
}
#endif

// BU: connection slot exhaustion mitigation
static bool CompareNodeActivityBytes(const CNodeRef &a, const CNodeRef &b)
{
    return a->nActivityBytes < b->nActivityBytes;
}

class CompareNetGroupKeyed
{
    std::vector<unsigned char> vchSecretKey;

public:
    CompareNetGroupKeyed()
    {
        vchSecretKey.resize(32, 0);
        GetRandBytes(vchSecretKey.data(), vchSecretKey.size());
    }

    bool operator()(const CNodeRef &a, const CNodeRef &b)
    {
        std::vector<unsigned char> vchGroupA, vchGroupB;
        CSHA256 hashA, hashB;
        std::vector<unsigned char> vchA(32), vchB(32);

        vchGroupA = a->addr.GetGroup();
        vchGroupB = b->addr.GetGroup();

        hashA.Write(begin_ptr(vchGroupA), vchGroupA.size());
        hashB.Write(begin_ptr(vchGroupB), vchGroupB.size());

        hashA.Write(begin_ptr(vchSecretKey), vchSecretKey.size());
        hashB.Write(begin_ptr(vchSecretKey), vchSecretKey.size());

        hashA.Finalize(begin_ptr(vchA));
        hashB.Finalize(begin_ptr(vchB));

        return vchA < vchB;
    }
};

static bool AttemptToEvictConnection(bool fPreferNewConnection)
{
    std::vector<CNodeRef> vEvictionCandidates;
    std::vector<CNodeRef> vEvictionCandidatesByActivity;
    {
        LOCK(cs_vNodes);

        static int64_t nLastTime = GetTime();
        for (CNode *node : vNodes)
        {
            // Decay the activity bytes for each node over a period of 2 hours.  This gradually de-prioritizes
            // a connection that was once active but has gone stale for some reason and allows lower priority
            // active nodes to climb the ladder.
            int64_t nNow = GetTime();

            while (true)
            {
                uint64_t nOldActivityBytes = node->nActivityBytes;
                uint64_t nNewActivityBytes = nOldActivityBytes * pow(1.0 - 1.0 / 7200, (double)(nNow - nLastTime));
                if (node->nActivityBytes.compare_exchange_weak(nOldActivityBytes, nNewActivityBytes))
                    break;
            }

            if (node->fWhitelisted)
                continue;
            if (!node->fInbound)
                continue;
            if (node->fDisconnect)
                continue;
            vEvictionCandidates.push_back(CNodeRef(node));

            // on occasion a node will connect but not complete it's initial ping/pong in a reasonable amount of time
            // and will therefore be the lowest priority connection and disconnected first.
            if (node->nPingNonceSent > 0 && node->nPingUsecTime == 0 &&
                ((GetStopwatchMicros() - node->nStopwatchConnected) > 60 * 1000000))
            {
                LOG(EVICT, "node %s evicted, slow ping\n", node->GetLogName());
                node->fDisconnect = true;
                return true;
            }
        }
        nLastTime = GetTime();
    }
    vEvictionCandidatesByActivity = vEvictionCandidates;


    if (vEvictionCandidates.empty())
        return false;


    // If we get here then we prioritize connections based on activity.  The least active incoming peer is
    // de-prioritized based on bytes in and bytes out.  A whitelisted peer will always get a connection and there is
    // no need here to check whether the peer is whitelisted or not.
    std::sort(vEvictionCandidatesByActivity.begin(), vEvictionCandidatesByActivity.end(), CompareNodeActivityBytes);
    vEvictionCandidatesByActivity[0]->fDisconnect = true;

    // BU - update the connection tracker
    {
        double nEvictions = 0;
        LOCK(cs_mapInboundConnectionTracker);
        CNetAddr ipAddress = (CNetAddr)vEvictionCandidatesByActivity[0]->addr;
        if (mapInboundConnectionTracker.count(ipAddress))
        {
            // Decay the current number of evictions (over 1800 seconds) depending on the last eviction
            int64_t nTimeElapsed = GetTime() - mapInboundConnectionTracker[ipAddress].nLastEvictionTime;
            double nRatioElapsed = (double)nTimeElapsed / 1800;
            nEvictions = mapInboundConnectionTracker[ipAddress].nEvictions -
                         (nRatioElapsed * mapInboundConnectionTracker[ipAddress].nEvictions);
            if (nEvictions < 0)
                nEvictions = 0;
        }

        nEvictions += 1;
        mapInboundConnectionTracker[ipAddress].nEvictions = nEvictions;
        mapInboundConnectionTracker[ipAddress].nLastEvictionTime = GetTime();

        LOG(EVICT, "Number of Evictions is %f for %s\n", nEvictions, vEvictionCandidatesByActivity[0]->addr.ToString());
        if (nEvictions > 15)
        {
            int nHoursToBan = 4;
            std::string userAgent = vEvictionCandidatesByActivity[0]->cleanSubVer;
            dosMan.Ban(ipAddress, userAgent, BanReasonTooManyEvictions, nHoursToBan * 60 * 60);
            LOGA("Banning %s for %d hours: Too many evictions - connection dropped\n",
                vEvictionCandidatesByActivity[0]->addr.ToString(), nHoursToBan);
        }
    }

    LOG(EVICT, "Node disconnected because too inactive:%d bytes of activity for peer %s\n",
        vEvictionCandidatesByActivity[0]->nActivityBytes, vEvictionCandidatesByActivity[0]->addrName);
    for (unsigned int i = 0; i < vEvictionCandidatesByActivity.size(); i++)
    {
        LOG(EVICT, "Node %s bytes %d candidate %d\n", vEvictionCandidatesByActivity[i]->addrName,
            vEvictionCandidatesByActivity[i]->nActivityBytes, i);
    }

    return true;
}

static void AcceptConnection(const ListenSocket &hListenSocket)
{
    // If a wallet rescan has started then do not accept any more connections until the rescan has completed.
    if (fRescan)
        return;

    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    SOCKET hSocket = accept(hListenSocket.socket, (struct sockaddr *)&sockaddr, &len);
    CAddress addr;

    if (hSocket != INVALID_SOCKET)
        if (!addr.SetSockAddr((const struct sockaddr *)&sockaddr))
            LOG(NET, "Warning: Unknown socket family\n");

    bool whitelisted = hListenSocket.whitelisted || dosMan.IsWhitelistedRange(addr);
    if (hSocket == INVALID_SOCKET)
    {
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK)
            LOG(NET, "socket error accept failed: %s\n", NetworkErrorString(nErr));
        return;
    }

    if (!IsSelectableSocket(hSocket))
    {
        LOG(NET, "connection from %s dropped: non-selectable socket\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    // According to the internet TCP_NODELAY is not carried into accepted sockets
    // on all platforms.  Set it again here just to be sure.
    int set = 1;
#ifdef WIN32
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&set, sizeof(int));
#else
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void *)&set, sizeof(int));
#endif

    if (dosMan.IsBanned(addr) && !whitelisted)
    {
        LOG(NET, "connection from %s dropped (banned)\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    // BU - Moved locks below checks above as they may return without us ever having to take these locks (esp. IsBanned
    // check)
    // NOTE: BU added separate tracking of outbound nodes added via the "-addnode" option.  This means that you actually
    //      may end up with up to 2 * nMaxOutConnections outbound connections due to the separate semaphores.
    //
    // 1. Limit the number of possible "-addnode" outbounds to not exceed nMaxOutConnections
    //   Otherwise we waste inbound connection slots on outbound addnodes that are blocked waiting on the semaphore.
    // 2. BUT, if less than nMaxOutConnections in vAddedNodes, open up any of the unreserved
    //   "-addnode" connection slots to the inbound pool to prevent holding presently unneeded outbound connection
    //   slots.
    int nMaxAddNodeOutbound = 0;
    {
        LOCK(cs_vAddedNodes);
        nMaxAddNodeOutbound = std::min((int)vAddedNodes.size(), nMaxOutConnections);
    }
    int nMaxInbound = nMaxConnections - (nMaxOutConnections + MAX_FEELER_CONNECTIONS) - nMaxAddNodeOutbound;

    // REVISIT: a. This doesn't take into account RPC "addnode <node> onetry" outbound connections as those aren't
    // tracked
    //         b. This also doesn't take into account whether or not the tracked vAddedNodes are valid or connected
    //         c. There is also an edge case where if less than nMaxOutConnections entries exist in vAddedNodes
    //            and there are already "maxconnections", between inbound and outbound nodes, the user can still use
    //            RPC "addnode <node> add" to successfully start additional outbound connections
    //         Points a. and c. can allow users to exceed "maxconnections"
    //         Point b. can cause us to waste slots holding them for invalid addnode entries that will never connect.

    int nInbound = 0;
    {
        LOCK(cs_vNodes);
        for (CNode *pnode : vNodes)
            if (pnode->fInbound)
                nInbound++;
    }
    // BU - end section

    if (nInbound >= nMaxInbound)
    {
        if (!AttemptToEvictConnection(whitelisted))
        {
            // No connection to evict, disconnect the new connection
            LOG(NET, "failed to find an eviction candidate - connection dropped (full)\n");
            CloseSocket(hSocket);
            return;
        }
    }

    // BU - add inbound connection to the ip tracker and increment counter
    // If connection attempts exceeded within allowable timeframe then ban peer
    {
        double nConnections = 0;
        LOCK(cs_mapInboundConnectionTracker);
        int64_t now = GetTime();
        CNetAddr ipAddress = (CNetAddr)addr;
        if (mapInboundConnectionTracker.count(ipAddress))
        {
            // Decay the current number of connections (over 60 seconds) depending on the last connection attempt
            int64_t nTimeElapsed = now - mapInboundConnectionTracker[ipAddress].nLastConnectionTime;
            if (nTimeElapsed < 0)
                nTimeElapsed = 0;
            double nRatioElapsed = (double)nTimeElapsed / 60;
            nConnections = mapInboundConnectionTracker[ipAddress].nConnections -
                           (nRatioElapsed * mapInboundConnectionTracker[ipAddress].nConnections);
            if (nConnections < 0)
                nConnections = 0;
        }
        else
        {
            ConnectionHistory ch;
            ch.nConnections = 0.0;
            ch.nLastConnectionTime = now;
            ch.nEvictions = 0.0;
            ch.nLastEvictionTime = now;
            mapInboundConnectionTracker[ipAddress] = ch;
        }

        nConnections += 1;
        mapInboundConnectionTracker[ipAddress].nConnections = nConnections;
        mapInboundConnectionTracker[ipAddress].nLastConnectionTime = GetTime();

        LOG(EVICT, "Number of connection attempts is %f for %s\n", nConnections, addr.ToString());
        if (nConnections > 4 && !whitelisted && !addr.IsLocal()) // local connections are auto-whitelisted
        {
            int nHoursToBan = 4;
            std::string userAgent = mapInboundConnectionTracker[ipAddress].userAgent;
            dosMan.Ban((CNetAddr)addr, userAgent, BanReasonTooManyConnectionAttempts, nHoursToBan * 60 * 60);
            LOGA("Banning %s for %d hours: Too many connection attempts - connection dropped\n", addr.ToString(),
                nHoursToBan);
            CloseSocket(hSocket);
            return;
        }
    }
    // BU - end section

    CNode *pnode = new CNode(hSocket, addr, "", true);
    pnode->AddRef();
    pnode->fWhitelisted = whitelisted;

    LOG(NET, "connection from %s accepted\n", addr.ToString());

    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }
}

char recvMsgBuf[MAX_RECV_CHUNK]; // Messages are first pulled into this buffer

void CleanupDisconnectedNodes()
{
    //
    // Disconnect nodes
    //
    list<CNode *> vNodesDisconnectedCopy;
    {
        LOCK2(cs_vNodes, cs_vNodesDisconnected);
        // Disconnect unused nodes
        vector<CNode *> vNodesCopy = vNodes;
        for (CNode *pnode : vNodesCopy)
        {
            if (pnode->fDisconnect || pnode->GetRefCount() <= 0)
            {
                // remove from vNodes
                vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

                // inform connection manager
                connmgr->RemovedNode(pnode);

                // release outbound grant (if any)
                pnode->grantOutbound.Release();

                // close socket and cleanup
                pnode->CloseSocketDisconnect();

                // Release this one reference.
                pnode->Release();

                // hold in disconnected pool until all other refs are released
                vNodesDisconnected.push_back(pnode);
            }
        }
        vNodesDisconnectedCopy = vNodesDisconnected;
    }

    // Delete disconnected nodes
    for (CNode *pnode : vNodesDisconnectedCopy)
    {
        // wait until threads are done using it
        if (pnode->GetRefCount() <= 0)
        {
            bool fDelete = false;
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                {
                    TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                    if (lockRecv)
                    {
                        TRY_LOCK(pnode->cs_inventory, lockInv);
                        if (lockInv)
                            fDelete = true;
                    }
                }
            }
            if (fDelete)
            {
                LOCK(cs_vNodesDisconnected);
                vNodesDisconnected.remove(pnode);
                // no need to remove from vNodes. we know pnode has already been removed from vNodes since that
                // occurred prior to insertion into vNodesDisconnected
                delete pnode;
            }
        }
    }
}

void ThreadSocketHandler()
{
    unsigned int nPrevNodeCount = 0;
    // This variable is incremented if something happens.  If it is zero at the bottom of the loop, we delay.  This
    // solves spin loop issues where the select does not block but no bytes can be transferred (traffic shaping limited,
    // for example).
    int progress;
    bool fAquiredAllRecvLocks;
    while (true)
    {
        progress = 0;
        fAquiredAllRecvLocks = true;
        stat_io_service.poll(); // BU instrumentation
        CleanupDisconnectedNodes();
        if (vNodes.size() != nPrevNodeCount)
        {
            nPrevNodeCount = vNodes.size();
            uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
        }

        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;
        std::set<SOCKET> setSocket;

        for (const ListenSocket &hListenSocket : vhListenSocket)
        {
            FD_SET(hListenSocket.socket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket.socket);
            have_fds = true;
            setSocket.insert(hListenSocket.socket);
        }

        {
            LOCK(cs_vNodes);
            for (CNode *pnode : vNodes)
            {
                // It is necessary to use a temporary variable to ensure that pnode->hSocket is not changed by another
                // thread during execution.
                // If the socket is closed and even reopened for some unrelated connection, the worst case is that we
                // get a spurious wakeup, so a mutex is not needed to protect the entire use of the socket.
                SOCKET hSocket = pnode->hSocket;
                if (hSocket == INVALID_SOCKET)
                    continue;
                FD_SET(hSocket, &fdsetError);
                hSocketMax = max(hSocketMax, hSocket);
                have_fds = true;
                setSocket.insert(hSocket);

                // Implement the following logic:
                // * If there is data to send, select() for sending data. As this only
                //   happens when optimistic write failed, we choose to first drain the
                //   write buffer in this case before receiving more. This avoids
                //   needlessly queueing received data, if the remote peer is not themselves
                //   receiving data. This means properly utilizing TCP flow control signalling.
                // * Otherwise, if there is no (complete) message in the receive buffer,
                //   or there is space left in the buffer, select() for receiving data.
                // * (if neither of the above applies, there is certainly one message
                //   in the receiver buffer ready to be processed).
                // Together, that means that at least one of the following is always possible,
                // so we don't deadlock:
                // * We send some data.
                // * We wait for data to be received (and disconnect after timeout).
                // * We process a message in the buffer (message handler thread).
                {
                    TRY_LOCK(pnode->cs_vSend, lockSend);
                    if (lockSend && (!pnode->vSendMsg.empty() || !pnode->vLowPrioritySendMsg.empty()))
                    {
                        FD_SET(hSocket, &fdsetSend);
                        continue;
                    }
                }
                {
                    TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                    if (lockRecv && (pnode->vRecvMsg.empty() || pnode->GetTotalRecvSize() <= ReceiveFloodSize()))
                    {
                        FD_SET(hSocket, &fdsetRecv);
                    }
                }
            }
        }

        int nSelect = select(have_fds ? hSocketMax + 1 : 0, &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
        if (shutdown_threads.load() == true)
        {
            return;
        }

        if (nSelect == SOCKET_ERROR)
        {
            if (have_fds)
            {
                int nErr = WSAGetLastError();
                LOG(NET, "socket select error %s\n", NetworkErrorString(nErr));

                for (SOCKET hSocket : setSocket)
                    FD_SET(hSocket, &fdsetRecv);
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            MilliSleep(timeout.tv_usec / 1000);
        }

        //
        // Accept new connections
        //
        for (const ListenSocket &hListenSocket : vhListenSocket)
        {
            if (hListenSocket.socket != INVALID_SOCKET && FD_ISSET(hListenSocket.socket, &fdsetRecv))
            {
                AcceptConnection(hListenSocket);
            }
        }

        //
        // Service each socket
        //
        vector<CNode *> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            for (CNode *pnode : vNodesCopy)
                pnode->AddRef();
        }

        for (CNode *pnode : vNodesCopy)
        {
            if (shutdown_threads.load() == true)
            {
                break; // drop out of this loop so we can quickly release node refs and return
            }

            //
            // Receive
            //
            // temporary used to make sure that pnode->hSocket isn't closed by another thread during processing here.
            SOCKET hSocket = pnode->hSocket;
            if (hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(hSocket, &fdsetRecv) || FD_ISSET(hSocket, &fdsetError))
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                int64_t amt2Recv = receiveShaper.available(RECV_SHAPER_MIN_FRAG);
                if (!lockRecv)
                {
                    fAquiredAllRecvLocks = false;
                }
                else if (amt2Recv > 0)
                {
                    {
                        progress++;
                        hSocket = pnode->hSocket; // get it again inside the lock
                        if (hSocket == INVALID_SOCKET)
                            continue;
                        // max of min makes sure amt is in a range reasonable for buffer allocation
                        int64_t amt = max((int64_t)1, min(amt2Recv, MAX_RECV_CHUNK));
                        int nBytes = recv(hSocket, recvMsgBuf, amt, MSG_DONTWAIT);
                        if (nBytes > 0)
                        {
                            receiveShaper.leak(nBytes);
                            if (!pnode->ReceiveMsgBytes(recvMsgBuf, nBytes))
                                pnode->fDisconnect = true;
                            int64_t tmp = GetTime();
                            pnode->recvGap << (tmp - pnode->nLastRecv);
                            pnode->nLastRecv = tmp;
                            pnode->nRecvBytes += nBytes;
                            pnode->bytesReceived += nBytes; // BU stats
                            pnode->RecordBytesRecv(nBytes);
                        }
                        else if (nBytes == 0)
                        {
                            // socket closed gracefully
                            if (!pnode->fDisconnect)
                                LOG(NET, "Node %s socket closed\n", pnode->GetLogName());
                            pnode->fDisconnect = true;
                            continue;
                        }
                        else if (nBytes < 0)
                        {
                            // error
                            int nErr = WSAGetLastError();
                            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR &&
                                nErr != WSAEINPROGRESS)
                            {
                                if (!pnode->fDisconnect)
                                    LOG(NET, "Node %s socket recv error '%s'\n", pnode->GetLogName(),
                                        NetworkErrorString(nErr));
                                pnode->fDisconnect = true;
                                continue;
                            }
                        }
                    }
                }
            }

            //
            // Send
            //
            hSocket = pnode->hSocket;
            if (hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(hSocket, &fdsetSend))
            {
                // Send priority messages if there any regardless of which peer, taking care to maintain
                // locking orders.
                //
                // Only send two messages, the first two in the send queue by setting the fSendTwo flag, since the
                // priority message will be the first or second one in the queue. If there happen to be multiple
                // priority messages stacked in the same queue then we will loop around sending one message each time.
                // This way we don't end up draining each queue for a peer before being able to send another priority
                // message from another peer which would happen in the case of block announcements.
                //
                // The following presents a more difficult issue in maintaining locking orders. cs_vSend must be
                // taken before cs_prioritySendQ and hence the following blocks of code needed to preserve that order.
                while (fPrioritySendMsg)
                {
                    // Check if anything is really in queue and pop the noderef. If we're empty then set the
                    // priority flag to false. Do it here so we don't have to check at the end again and take a lock
                    // twice.
                    CNodeRef noderef;
                    {
                        LOCK(cs_prioritySendQ);
                        if (!vPrioritySendQ.empty())
                        {
                            noderef = vPrioritySendQ.front();
                            vPrioritySendQ.pop_front();
                        }
                        else
                        {
                            fPrioritySendMsg = false;
                            break;
                        }
                    }

                    // Send the first two messages in the send queue. We send two because
                    // the first message may be a partial message and as a result may not be
                    // a priority message; the priorty message may be the one behind this partial message.
                    CNode *pfrom = noderef.get();
                    if (pfrom != nullptr)
                    {
                        bool fEmpty = false;
                        TRY_LOCK(pfrom->cs_vSend, lock_sendtwo);
                        if (lock_sendtwo)
                        {
                            if (!pfrom->vSendMsg.empty())
                            {
                                bool fSendTwo = true;
                                progress += SocketSendData(pfrom, fSendTwo);
                            }
                            else
                                fEmpty = true;
                        }

                        if (!pfrom->fDisconnect && !lock_sendtwo)
                        {
                            // Only if we failed to lock, then push to the back of the queue an try later.
                            //
                            // NOTE: If priority messages failed to send or are not sent in their entirety then do "NOT"
                            //       try again, just let the normal queuing take care of sending the rest. We don't want
                            //       to start a possible infinite loop where a socket could be hung or network could
                            //       be backed up. The remainder of the message is at the front of the queue so
                            //       it will get sent (if it can) at some point.
                            LOCK(cs_prioritySendQ);
                            vPrioritySendQ.push_back(noderef);
                            fPrioritySendMsg = true;
                            break;
                        }
                        else if (fEmpty)
                            break;
                    }
                }

                // Send messages from this pnode's send queue
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend && sendShaper.try_leak(0))
                {
                    progress += SocketSendData(pnode);
                }
            }

            //
            // Inactivity checking every TIMEOUT_INTERVAL
            //
            int64_t stopwatchTime = GetStopwatchMicros();
            if (stopwatchTime - pnode->nStopwatchConnected > TIMEOUT_INTERVAL * 1000000)
            {
                pnode->nStopwatchConnected = GetTimeMicros();
                if (ignoreNetTimeouts.Value() == false)
                {
                    int64_t nTime = GetTime();
                    if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                    {
                        LOG(NET, "Node %s: no message sent or received after startup, %d %d from %d\n",
                            pnode->GetLogName(), pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->id);
                        pnode->fDisconnect = true;
                    }
                    else if (nTime - pnode->nLastSend > TIMEOUT_INTERVAL)
                    {
                        LOG(NET, "Node %s: socket sending timeout: %is\n", pnode->GetLogName(),
                            nTime - pnode->nLastSend);
                        pnode->fDisconnect = true;
                    }
                    else if (nTime - pnode->nLastRecv > TIMEOUT_INTERVAL)
                    {
                        LOG(NET, "Node %s: socket receive timeout: %is\n", pnode->GetLogName(),
                            nTime - pnode->nLastRecv);
                        pnode->fDisconnect = true;
                    }
                    else if (pnode->nPingNonceSent &&
                             pnode->nPingUsecStart + (TIMEOUT_INTERVAL * 1000000) < (int64_t)GetStopwatchMicros())
                    {
                        LOG(NET, "Node %s: ping timeout: %fs\n", pnode->GetLogName(),
                            0.000001 * (GetStopwatchMicros() - pnode->nPingUsecStart));
                        pnode->fDisconnect = true;
                    }
                }
            }
        }
        // A cs_vNodes lock is not required here when releasing refs for two reasons: one, this only decrements
        // an atomic counter, and two, the counter will always be > 0 at this point, so we don't have to worry
        // that a pnode could be disconnected and no longer exist before the decrement takes place.
        for (CNode *pnode : vNodesCopy)
        {
            pnode->Release();
        }

        // BU: Nothing happened even though select did not block.  So slow us down.
        if (progress == 0 && fAquiredAllRecvLocks)
            MilliSleep(5);
    }
}

#ifdef USE_UPNP
static bool fShutdownUPnP = false;
void ThreadMapPort()
{
    std::string port = strprintf("%u", GetListenPort());
    const char *multicastif = nullptr;
    const char *minissdpdpath = nullptr;
    struct UPNPDev *devlist = nullptr;
    char lanaddr[64];

#ifndef UPNPDISCOVER_SUCCESS
    /* miniupnpc 1.5 */
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0);
#elif MINIUPNPC_API_VERSION < 14
    /* miniupnpc 1.6 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
    /* miniupnpc 1.9.20150730 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

    struct UPNPUrls urls;
    struct IGDdatas data;
    int r;

    r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1)
    {
        if (fDiscover)
        {
            char externalIPAddress[40];
            r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
            if (r != UPNPCOMMAND_SUCCESS)
                LOGA("UPnP: GetExternalIPAddress() returned %d\n", r);
            else
            {
                if (externalIPAddress[0])
                {
                    LOGA("UPnP: ExternalIPAddress = %s\n", externalIPAddress);
                    AddLocal(CNetAddr(externalIPAddress), LOCAL_UPNP);
                }
                else
                    LOGA("UPnP: GetExternalIPAddress failed.\n");
            }
        }

        string strDesc = "Bitcoin " + FormatFullVersion();

        while (true)
        {
#ifndef UPNPDISCOVER_SUCCESS
            /* miniupnpc 1.5 */
            r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port.c_str(), port.c_str(), lanaddr,
                strDesc.c_str(), "TCP", 0);
#else
            /* miniupnpc 1.6 */
            r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port.c_str(), port.c_str(), lanaddr,
                strDesc.c_str(), "TCP", 0, "0");
#endif
            if (r != UPNPCOMMAND_SUCCESS)
                LOGA("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n", port, port, lanaddr, r, strupnperror(r));
            else
                LOGA("UPnP Port Mapping successful.\n");

            // Refresh every 20 minutes
            for (int i = 1; i < 20 * 60; i++)
            {
                MilliSleep(1000);
                if (ShutdownRequested() || fShutdownUPnP)
                {
                    LOGA("interrupt caught and deleting portmapping\n");
                    r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
                    LOGA("UPNP_DeletePortMapping() returned: %d\n", r);
                    freeUPNPDevlist(devlist);
                    devlist = nullptr;
                    FreeUPNPUrls(&urls);
                    return;
                }
            }
        }
    }
    else
    {
        LOGA("No valid UPnP IGDs found\n");
        freeUPNPDevlist(devlist);
        devlist = nullptr;
        if (r != 0)
            FreeUPNPUrls(&urls);
    }
}

void MapPort(bool fUseUPnP)
{
    static std::thread *upnp_thread = nullptr;

    if (fUseUPnP)
    {
        if (upnp_thread)
        {
            fShutdownUPnP = true;
            upnp_thread->join();
            delete upnp_thread;
        }
        fShutdownUPnP = false;
        upnp_thread = new std::thread(std::bind(&TraceThread<void (*)()>, "upnp", &ThreadMapPort));
    }
    else if (upnp_thread)
    {
        fShutdownUPnP = true;
        upnp_thread->join();
        delete upnp_thread;
        upnp_thread = nullptr;
    }
}

#else
void MapPort(bool)
{
    // Intentionally left blank.
}
#endif

static std::string GetDNSHost(const CDNSSeedData &data, uint64_t requiredServiceBits)
{
    // use default host for non-filter-capable seeds or if we use the default service bits (NODE_NETWORK)
    if (!data.supportsServiceBitsFiltering || requiredServiceBits == NODE_NETWORK)
    {
        return data.host;
    }

    return strprintf("x%x.%s", requiredServiceBits, data.host);
}

static void DNSAddressSeed()
{
    // goal: only query DNS seeds if address need is acute
    if ((addrman.size() > 0) && (!GetBoolArg("-forcednsseed", DEFAULT_FORCEDNSSEED)))
    {
        for (int j = 0; j < 22; j++)
        {
            MilliSleep(500);
            if (shutdown_threads.load() == true)
                return;
        }

        LOCK(cs_vNodes);
        if (vNodes.size() >= 2)
        {
            LOGA("P2P peers available. Skipped DNS seeding.\n");
            return;
        }
    }

    // BITCOINUNLIMITED START
    // If user specifies custom DNS seeds, do not use hard-coded defaults.
    vector<CDNSSeedData> vSeeds;
    {
        LOCK(cs_vUseDNSSeeds);
        vUseDNSSeeds = mapMultiArgs["-usednsseed"];
    }
    if (vUseDNSSeeds.size() == 0)
    {
        vSeeds = Params().DNSSeeds();
        LOGA("Using default DNS seeds.\n");
    }
    else
    {
        for (const string &seed : vUseDNSSeeds)
        {
            vSeeds.push_back(CDNSSeedData(seed, seed));
        }
        LOGA("Using %d user defined DNS seeds.\n", vSeeds.size());
    }
    // BITCOINUNLIMITED END

    int found = 0;

    LOGA("Loading addresses from DNS seeds (could take a while)\n");

    for (const CDNSSeedData &seed : vSeeds)
    {
        if (HaveNameProxy())
        {
            AddOneShot(seed.host);
        }
        else
        {
            vector<CNetAddr> vIPs;
            vector<CAddress> vAdd;
            uint64_t requiredServiceBits = NODE_NETWORK;
            if (LookupHost(GetDNSHost(seed, requiredServiceBits).c_str(), vIPs, MAX_DNS_SEEDED_IPS, true))
            {
                for (const CNetAddr &ip : vIPs)
                {
                    int nOneDay = 24 * 3600;
                    CAddress addr = CAddress(CService(ip, Params().GetDefaultPort()), requiredServiceBits);
                    // use a random age between 3 and 7 days old
                    addr.nTime = GetTime() - 3 * nOneDay - GetRand(4 * nOneDay);
                    vAdd.push_back(addr);
                    found++;
                }
            }
            // TODO: The seed name resolve may fail, yielding an IP of [::], which results in
            // addrman assigning the same source to results from different seeds.
            // This should switch to a hard-coded stable dummy IP for each seed name, so that the
            // resolve is not required at all.
            if (!vIPs.empty())
            {
                CService seedSource;
                Lookup(seed.name.c_str(), seedSource, 0, true);
                addrman.Add(vAdd, seedSource);
            }
        }
    }

    LOGA("%d addresses found from DNS seeds\n", found);
}

#if 0 // Disabled until a Bitcoin Cash compatible "bitnodes" site becomes available
static void BitnodesAddressSeed()
{
    // Get nodes from websites offering Bitnodes API
    if ((addrman.size() > 0) && (!GetBoolArg("-forcebitnodes", DEFAULT_FORCEBITNODES)))
    {
        MilliSleep(11 * 1000);
        LOCK(cs_vNodes);
        if (vNodes.size() >= 2)
        {
            LOGA("P2P peers available. Skipped Bitnodes seeding.\n");
            return;
        }
    }

    LOGA("Loading addresses from Bitnodes API\n");

    vector<string> vIPs;
    vector<CAddress> vAdd;
    bool success = GetLeaderboardFromBitnodes(vIPs);
    if (success)
    {
        int portOut;
        std::string hostOut = "";
        for (const string &seed : vIPs)
        {
            SplitHostPort(seed, portOut, hostOut);
            CNetAddr ip(hostOut);
            CAddress addr = CAddress(CService(ip, portOut));
            addr.nTime = GetTime();
            vAdd.push_back(addr);
        }
        CService bitnodes;
        if (Lookup("bitnodes.21.co", bitnodes, 0, true))
            addrman.Add(vAdd, bitnodes);
    }

    LOGA("%d addresses found from Bitnodes API\n", vAdd.size());
}
#endif

void ThreadAddressSeeding()
{
    if (!GetBoolArg("-dnsseed", true))
        LOGA("DNS seeding disabled\n");
    else
    {
        DNSAddressSeed();
    }

    // Bitnodes seeding is intended as a backup in the event that DNS seeding fails and a such is run after.
    if ((!GetBoolArg("-bitnodes", true)) || (Params().NetworkIDString() != "main"))
        LOGA("Bitnodes API seeding disabled\n");
    else
    {
        // TODO: re-enable bitnodes seeding once a site is available for the BitcoinCash chain.
        // BitnodesAddressSeed();
        LOGA("Bitnodes API seeding temporarily disabled\n");
    }
}


void DumpAddresses()
{
    int64_t nStart = GetTimeMillis();

    CAddrDB adb;
    adb.Write(addrman);

    LOG(NET, "Flushed %d addresses to peers.dat  %dms\n", addrman.size(), GetTimeMillis() - nStart);
}

void _DumpData()
{
    DumpAddresses();

    // Request dos manager to write it's ban list to disk
    dosMan.DumpBanlist();
}

void DumpData(int64_t seconds_between_runs)
{
    if (seconds_between_runs == 0)
    {
        _DumpData();
        return;
    }
    while (shutdown_threads.load() == false)
    {
        // this has the potential to be a long sleep. so do it in chunks incase of node shutdown
        int64_t nStart = GetTime();
        int64_t nEnd = nStart + seconds_between_runs;
        while (nStart < nEnd)
        {
            if (shutdown_threads.load() == true)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            nStart = GetTime();
        }
        _DumpData();
    }
}

void UpdateMisbehaviorPoints(int64_t seconds_between_runs)
{
    // Update all misbehaviors
    while (shutdown_threads.load() == false)
    {
        // this has the potential to be a long sleep. so do it in chunks incase of node shutdown
        int64_t nStart = GetTime();
        int64_t nEnd = nStart + seconds_between_runs;
        while (nStart < nEnd)
        {
            if (shutdown_threads.load() == true)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            nStart = GetTime();
        }

        // Update each peers misbehavior
        {
            LOCK(cs_vNodes);
            for (auto pnode : vNodes)
            {
                dosMan.UpdateMisbehavior(pnode);
            }
        }
    }
}

void static ProcessOneShot()
{
    string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty())
            return;
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    CSemaphoreGrant grant(*semOutbound, true);
    // Seeding nodes track against the original outbound semaphore.
    // Uses try-wait methodology because if a grant is given, there are outbound
    // slots to fill, and if the grant isn't given, there's no seeding to do.
    if (grant)
    {
        if (!OpenNetworkConnection(addr, false, &grant, strDest.c_str(), true))
            AddOneShot(strDest);
    }
}

void ThreadOpenConnections()
{
    // Connect to all "connect" peers
    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
    {
        for (int64_t nLoop = 0;; nLoop++)
        {
            ProcessOneShot();
            for (const std::string &strAddr : mapMultiArgs["-connect"])
            {
                CAddress addr;
                // NOTE: Because the only nodes we are connecting to here are the ones the user put in their
                //      bitcoin.conf/commandline args as "-connect", we don't use the semaphore to limit outbound
                //      connections
                OpenNetworkConnection(addr, false, nullptr, strAddr.c_str());
                for (int i = 0; i < 10 && i < nLoop; i++)
                {
                    MilliSleep(500);
                }
            }
            MilliSleep(500);
            if (shutdown_threads.load() == true)
            {
                return;
            }
        }
    }

    // NOTE: If we are in the block above, then no seeding should occur as "-connect""
    // is intended as "only make outbound connections to the configured nodes".

    // Initiate network connections
    int64_t nStart = GetStopwatchMicros();
    unsigned int nDisconnects = 0;
    // Minimum time before next feeler connection (in microseconds).
    int64_t nNextFeeler = PoissonNextSend(nStart, FEELER_INTERVAL);

    while (shutdown_threads.load() == false)
    {
        ProcessOneShot();

        MilliSleep(500);

        // Only connect out to one peer per network group (/16 for IPv4).
        // Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
        // And also must do this before the semaphore grant so that we don't have to block
        // if the grants are all taken and we want to disconnect a node in the event that
        // we don't have enough connections to XTHIN capable nodes yet.
        int nOutbound = 0;
        int nThinBlockCapable = 0;
        set<vector<unsigned char> > setConnected;
        CNode *pNonXthinNode = nullptr;
        CNode *pNonNodeNetwork = nullptr;
        bool fDisconnected = false;
        {
            LOCK(cs_vNodes);
            for (CNode *pnode : vNodes)
            {
                if (pnode->fAutoOutbound) // only count outgoing connections.
                {
                    setConnected.insert(pnode->addr.GetGroup());
                    nOutbound++;

                    if (pnode->ThinBlockCapable())
                        nThinBlockCapable++;
                    else
                        pNonXthinNode = pnode;

                    // If sync is not yet complete then disconnect any pruned outbound connections
                    if (IsInitialBlockDownload() && !(pnode->nServices & NODE_NETWORK))
                        pNonNodeNetwork = pnode;
                }
            }
            // Disconnect a node that is not XTHIN capable if all outbound slots are full and we
            // have not yet connected to enough XTHIN nodes.
            if (!fReindex)
            {
                nMinXthinNodes = GetArg("-min-xthin-nodes", MIN_XTHIN_NODES);
                if (nOutbound >= nMaxOutConnections && nThinBlockCapable <= min(nMinXthinNodes, nMaxOutConnections) &&
                    nDisconnects < MAX_DISCONNECTS && IsThinBlocksEnabled() && IsChainNearlySyncd())
                {
                    if (pNonXthinNode != nullptr)
                    {
                        pNonXthinNode->fDisconnect = true;
                        fDisconnected = true;
                        nDisconnects++;
                    }
                }
                else if (IsInitialBlockDownload())
                {
                    if (pNonNodeNetwork != nullptr)
                    {
                        pNonNodeNetwork->fDisconnect = true;
                        fDisconnected = true;
                        nDisconnects++;
                    }
                }
            }
            // In the event that outbound nodes restart or drop off the network over time we need to
            // replenish the number of disconnects allowed once per day.
            if (GetStopwatchMicros() - nStart > 86400UL * 1000000UL)
            {
                nDisconnects = 0;
                nStart = GetStopwatchMicros();
            }
        }

        // If disconnected then wait for disconnection completion
        if (fDisconnected)
        {
            while (true)
            {
                MilliSleep(500);
                {
                    LOCK(cs_vNodes);
                    if (find(vNodes.begin(), vNodes.end(), pNonXthinNode) == vNodes.end() ||
                        find(vNodes.begin(), vNodes.end(), pNonNodeNetwork) == vNodes.end())
                    {
                        nOutbound--;
                        break;
                    }
                }
                if (shutdown_threads.load() == true)
                {
                    return;
                }
            }
        }

        // During IBD we do not actively disconnect and search for XTHIN capable nodes therefore
        // we need to check occasionally whether IBD is complete, meaning IsChainNearlySynd() returns true.
        // Therefore we do a try_wait() rather than wait() when aquiring the semaphore. A try_wait() is
        // indicated by passing "true" to CSemaphore grant().
        CSemaphoreGrant grant(*semOutbound, true);
        if (!grant)
        {
            // If the try_wait() fails, meaning all grants are currently in use, then we wait for one minute
            // to check again whether we should disconnect any nodes.  We don't have to check this too often
            // as this is most relevant during IBD.
            for (auto j = 0; (j < 120) && (shutdown_threads.load() == false); j++)
                MilliSleep(500);
            continue;
        }
        if (shutdown_threads.load() == true)
        {
            return;
        }

        // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
        if (addrman.size() == 0 && (GetTime() - nStart > 60))
        {
            static bool done = false;
            if (!done)
            {
                LOGA("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
                addrman.Add(convertSeed6(Params().FixedSeeds()), CNetAddr("127.0.0.1"));
                done = true;
            }
        }

        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Feeler Connections
        //
        // Design goals:
        //  * Increase the number of connectable addresses in the tried table.
        //
        // Method:
        //  * Choose a random address from new and attempt to connect to it if we can connect
        //    successfully it is added to tried.
        //  * Start attempting feeler connections only after node finishes making outbound
        //    connections.
        //  * Only make a feeler connection once every few minutes.
        //

        bool fFeeler = false;
        if (nOutbound >= nMaxOutConnections)
        {
            int64_t nTime = GetStopwatchMicros(); // The current time right now (in microseconds).
            if (nTime > nNextFeeler)
            {
                nNextFeeler = PoissonNextSend(nTime, FEELER_INTERVAL);
                fFeeler = true;
            }
            else
            {
                continue;
            }
        }

        addrman.ResolveCollisions();

        int64_t nANow = GetAdjustedTime();
        int nTries = 0;
        while (shutdown_threads.load() == false)
        {
            CAddrInfo addr = addrman.SelectTriedCollision();

            // SelectTriedCollision returns an invalid address if it is empty.
            if (!fFeeler || !addr.IsValid())
            {
                addr = addrman.Select(fFeeler);
            }

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
                break;

            // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman addresses.
            nTries++;
            if (nTries > 100)
                break;

            if (IsLimited(addr))
                continue;

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30)
                continue;

            // do not allow non-default ports, unless after 50 invalid addresses selected already
            if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
                continue;

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid())
        {
            if (fFeeler)
            {
                // Add small amount of random noise before connection to avoid synchronization.
                int randsleep = GetRandInt(FEELER_SLEEP_WINDOW * 1000);
                MilliSleep(randsleep);
                LOG(NET, "Making feeler connection to %s\n", addrConnect.ToString());
            }

            // Seeded outbound connections track against the original semaphore
            if (OpenNetworkConnection(addrConnect, (int)setConnected.size() >= std::min(nMaxConnections - 1, 2), &grant,
                    nullptr, false, fFeeler))
            {
                LOCK(cs_vNodes);
                CNode *pnode = FindNode((CService)addrConnect);
                // We need to use a separate outbound flag so as not to differentiate these outbound
                // nodes with ones that were added using -addnode -connect-thinblock or -connect.
                if (pnode)
                {
                    pnode->fAutoOutbound = true;
                    requester.nOutbound++;
                }
            }
        }
    }
}

void ThreadOpenAddedConnections()
{
    // BU: This intial sleep fixes a timing issue where a remote peer may be trying to connect using addnode
    //     at the same time this thread is starting up causing both an outbound and an inbound -addnode connection
    //     to be possible, when it should not be.
    for (int j = 0; j < 30; j++)
    {
        MilliSleep(500);
        if (shutdown_threads.load() == true)
            return;
    }

    // BU: we need our own separate semaphore for -addnodes otherwise we won't be able to reconnect
    //     after a remote node restarts, becuase all the outgoing connection slots will already be filled.
    if (semOutboundAddNode == nullptr)
    {
        // NOTE: Because the number of "-addnode" values can be changed via RPC calls to "addnode add|remove"
        //      we should always set the semaphore to have a count of nMaxOutConnections, otherwise
        //      the user's configuration could unintentionally limit the number of "-addnode" connections
        //      that can be made through RPC.  In the worst case, if no "-addnode" options were configured,
        //      this would break the RPC addnode functionality as the semaphore would have an initial count
        //      condition of 0 and grants would never succeed.
        semOutboundAddNode = new CSemaphore(nMaxOutConnections);
    }

    if (HaveNameProxy())
    {
        while (shutdown_threads.load() == false)
        {
            list<string> lAddresses(0);
            {
                LOCK(cs_vAddedNodes);
                for (const std::string &strAddNode : vAddedNodes)
                    lAddresses.push_back(strAddNode);
            }
            for (const std::string &strAddNode : lAddresses)
            {
                CAddress addr;
                // BU: always allow us to add a node manually. Whenever we use -addnode the maximum InBound connections
                // are reduced by
                //     the same number.  Here we use our own semaphore to ensure we have the outbound slots we need and
                //     can reconnect to
                //     nodes that have restarted.
                CSemaphoreGrant grant(*semOutboundAddNode);
                OpenNetworkConnection(addr, false, &grant, strAddNode.c_str());
                MilliSleep(500);
            }
            // Retry every 15 seconds.  It is important to check often to make sure the Xpedited Relay network
            // nodes reconnect quickly after the remote peers restart
            for (int j = 0; j < 30; j++)
            {
                MilliSleep(500);
                if (shutdown_threads.load() == true)
                    return;
            }
        }
    }

    for (unsigned int i = 0; true; i++)
    {
        list<string> lAddresses(0);
        {
            LOCK(cs_vAddedNodes);
            for (const std::string &strAddNode : vAddedNodes)
                lAddresses.push_back(strAddNode);
        }

        list<vector<CService> > lservAddressesToAdd(0);
        for (const std::string &strAddNode : lAddresses)
        {
            vector<CService> vservNode(0);
            if (Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), 0, fNameLookup))
            {
                lservAddressesToAdd.push_back(vservNode);
                {
                    LOCK(cs_setservAddNodeAddresses);
                    for (const CService &serv : vservNode)
                        setservAddNodeAddresses.insert(serv);
                }
            }
        }
        // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
        // (keeping in mind that addnode entries can have many IPs if fNameLookup)
        {
            LOCK(cs_vNodes);
            for (CNode *pnode : vNodes)
            {
                for (list<vector<CService> >::iterator it = lservAddressesToAdd.begin();
                     it != lservAddressesToAdd.end(); it++)
                {
                    for (const CService &addrNode : *(it))
                    {
                        if (pnode->addr == addrNode)
                        {
                            it = lservAddressesToAdd.erase(it);
                            it--;
                            break;
                        }
                    }
                }
            }
        }

        for (vector<CService> &vserv : lservAddressesToAdd)
        {
            // BU: always allow us to add a node manually. Whenever we use -addnode the maximum InBound connections are
            // reduced by
            //     the same number.  Here we use our own semaphore to ensure we have the outbound slots we need and can
            //     reconnect to
            //     nodes that have restarted.
            CSemaphoreGrant grant(*semOutboundAddNode);
            OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), false, &grant);
            MilliSleep(500);
        }
        if (shutdown_threads.load() == true)
        {
            return;
        }
        // Retry every 15 seconds.  It is important to check often to make sure the Xpedited Relay network
        // nodes reconnect quickly after the remote peers restart
        for (int j = 0; j < 30; j++)
        {
            MilliSleep(500);
            if (shutdown_threads.load() == true)
                return;
        }
    }
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress &addrConnect,
    bool fCountFailure,
    CSemaphoreGrant *grantOutbound,
    const char *pszDest,
    bool fOneShot,
    bool fFeeler)
{
    //
    // Initiate outbound network connection
    //
    if (shutdown_threads.load() == true)
    {
        return false;
    }
    {
        // BU: Add lock on cs_vNodes as FindNode now requries it to prevent potential use-after-free errors
        LOCK(cs_vNodes);
        if (!pszDest)
        {
            if (IsLocal(addrConnect) || FindNode((CNetAddr)addrConnect) || dosMan.IsBanned(addrConnect) ||
                FindNode(addrConnect.ToStringIPPort()))
                return false;
        }
        else if (FindNode(std::string(pszDest)))
            return false;
    }

    CNode *pnode = ConnectNode(addrConnect, pszDest, fCountFailure);
    if (shutdown_threads.load() == true)
    {
        return false;
    }

    if (!pnode)
        return false;
    if (grantOutbound)
        grantOutbound->MoveTo(pnode->grantOutbound);
    pnode->fNetworkNode = true;
    if (fOneShot)
        pnode->fOneShot = true;
    if (fFeeler)
        pnode->fFeeler = true;

    return true;
}


static bool threadProcessMessages(CNode *pnode)
{
    bool fSleep = true;
    // Receive messages from the net layer and put them into the receive queue.
    if (!g_signals.ProcessMessages(pnode))
        pnode->fDisconnect = true;

    // Discover if there's more work to be done
    if (pnode->nSendSize < SendBufferSize())
    {
        {
            // If already locked some other thread is working on it, so no work for this thread
            TRY_LOCK(pnode->csRecvGetData, lockRecv);
            if (lockRecv && (!pnode->vRecvGetData.empty()))
                fSleep = false;
        }
        if (fSleep)
        {
            // If already locked some other thread is working on it, so no work for this thread
            TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
            if (lockRecv && (!pnode->vRecvMsg.empty() || fPriorityRecvMsg.load()))
                fSleep = false;
        }
    }
    return fSleep;
}

void ThreadMessageHandler()
{
    while (shutdown_threads.load() == false)
    {
        // Start or Stop threads as determined by the numMsgHandlerThreads tweak
        {
            static CCriticalSection cs_threads;
            static uint32_t numThreads GUARDED_BY(cs_threads) = numMsgHandlerThreads.Value();
            LOCK(cs_threads);
            if (numMsgHandlerThreads.Value() >= 1 && numThreads > numMsgHandlerThreads.Value())
            {
                // Kill this thread
                numThreads--;
                LOGA("Stopping a message handler thread: Current handler threads are %d\n", numThreads);

                return;
            }
            else if (numThreads < numMsgHandlerThreads.Value())
            {
                // Launch another thread
                numThreads++;
                threadGroup.create_thread(&ThreadMessageHandler);
                LOGA("Starting a new message handler thread: Current handler threads are %d\n", numThreads);
            }
        }

        vector<CNode *> vNodesCopy;
        {
            // We require the vNodes lock here, throughout, even though we are only incrementing
            // an atomic counter when we AddRef(). We have to be aware that a socket disconnection
            // could occur if we don't take the lock.
            LOCK(cs_vNodes);

            // During IBD and because of the multithreading of PV we end up favoring the first peer that
            // connected and end up downloading a disproportionate amount of data from that first peer.
            // By rotating vNodes evertime we send messages we can alleviate this problem.
            // Rotate every 60 seconds so we don't do this too often.
            static int64_t nLastRotation = GetTime();
            if (IsInitialBlockDownload() && vNodes.size() > 0 && GetTime() - nLastRotation > 60)
            {
                std::rotate(vNodes.begin(), vNodes.end() - 1, vNodes.end());
                nLastRotation = GetTime();
            }

            vNodesCopy = vNodes;
            for (CNode *pnode : vNodes)
            {
                pnode->AddRef();
            }
        }

        bool fSleep = true;

        if (((GetStopwatchMicros() - lastMempoolSync) > MEMPOOLSYNC_FREQ_US) && vNodesCopy.size() > 0)
        {
            // select node from whom to request mempool sync
            CNode *syncPeer = SelectMempoolSyncPeer(vNodesCopy);
            if (syncPeer && IsChainNearlySyncd())
                requester.RequestMempoolSync(syncPeer);
        }

        for (CNode *pnode : vNodesCopy)
        {
            if (pnode->fDisconnect)
                continue;

            if (pnode->fSuccessfullyConnected)
            {
                // parallel processing
                fSleep &= threadProcessMessages(pnode);
            }
            else
            {
                // serial processing during setup
                TRY_LOCK(pnode->csSerialPhase, lockSerial);
                if (lockSerial)
                    fSleep &= threadProcessMessages(pnode);
            }
            if (shutdown_threads.load() == true)
            {
                break; // skip down to where we release the node refs
            }

            // Put transaction and block requests into the request manager
            // and all other requests into the send queue.
            if (pnode->fSuccessfullyConnected)
            {
                // parallel processing
                g_signals.SendMessages(pnode);
            }
            else
            {
                // serial processing during setup
                TRY_LOCK(pnode->csSerialPhase, lockSerial);
                if (lockSerial)
                    g_signals.SendMessages(pnode);
            }
            if (shutdown_threads.load() == true)
            {
                break; // skip down to where we release the node refs
            }
        }

        // From the request manager, make requests for transactions and blocks. We do this before potentially
        // sleeping in the step below so as to allow requests to return during the sleep time.
        if (shutdown_threads.load() == false)
            requester.SendRequests();

        // A cs_vNodes lock is not required here when releasing refs for two reasons: one, this only decrements
        // an atomic counter, and two, the counter will always be > 0 at this point, so we don't have to worry
        // that a pnode could be disconnected and no longer exist before the decrement takes place.
        for (CNode *pnode : vNodesCopy)
        {
            pnode->Release();
        }

        if (fSleep)
        {
            std::unique_lock<std::mutex> lock(wakeableDelayMutex);
            messageHandlerCondition.wait_until(lock, std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
        }
    }
}


bool BindListenPort(const CService &addrBind, string &strError, bool fWhitelisted)
{
    strError = "";
    int nOne = 1;

    // Create socket for listening for incoming connections
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr *)&sockaddr, &len))
    {
        strError = strprintf("Error: Bind address family for %s not supported", addrBind.ToString());
        LOGA("%s\n", strError);
        return false;
    }

    SOCKET hListenSocket = socket(((struct sockaddr *)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET)
    {
        strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %s)",
            NetworkErrorString(WSAGetLastError()));
        LOGA("%s\n", strError);
        return false;
    }
    if (!IsSelectableSocket(hListenSocket))
    {
        strError = "Error: Couldn't create a listenable socket for incoming connections";
        LOGA("%s\n", strError);
        return false;
    }


#ifndef WIN32
#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void *)&nOne, sizeof(int));
#endif
    // Allow binding if the port is still in TIME_WAIT state after
    // the program was closed and restarted.
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void *)&nOne, sizeof(int));
    // Disable Nagle's algorithm
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (void *)&nOne, sizeof(int));
#else
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&nOne, sizeof(int));
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&nOne, sizeof(int));
#endif

    // Set to non-blocking, incoming connections will also inherit this
    if (!SetSocketNonBlocking(hListenSocket, true))
    {
        strError = strprintf("BindListenPort: Setting listening socket to non-blocking failed, error %s\n",
            NetworkErrorString(WSAGetLastError()));
        LOGA("%s\n", strError);
        return false;
    }

    // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
    // and enable it by default or not. Try to enable it, if possible.
    if (addrBind.IsIPv6())
    {
#ifdef IPV6_V6ONLY
#ifdef WIN32
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&nOne, sizeof(int));
#else
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&nOne, sizeof(int));
#endif
#endif
#ifdef WIN32
        int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL, (const char *)&nProtLevel, sizeof(int));
#endif
    }

    if (::bind(hListenSocket, (struct sockaddr *)&sockaddr, len) == SOCKET_ERROR)
    {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE)
            strError = strprintf(_("Unable to bind to %s on this computer. %s is probably already running."),
                addrBind.ToString(), _(PACKAGE_NAME));
        else
            strError = strprintf(_("Unable to bind to %s on this computer (bind returned error %s)"),
                addrBind.ToString(), NetworkErrorString(nErr));
        LOGA("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }
    LOGA("Bound to %s\n", addrBind.ToString());

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        strError = strprintf(_("Error: Listening for incoming connections failed (listen returned error %s)"),
            NetworkErrorString(WSAGetLastError()));
        LOGA("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }

    vhListenSocket.push_back(ListenSocket(hListenSocket, fWhitelisted));

    if (addrBind.IsRoutable() && fDiscover && !fWhitelisted)
        AddLocal(addrBind, LOCAL_BIND);

    return true;
}

void static Discover()
{
    if (!fDiscover)
        return;

#ifdef WIN32
    // Get local host IP
    char pszHostName[256] = "";
    if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR)
    {
        vector<CNetAddr> vaddr;
        if (LookupHost(pszHostName, vaddr, 0, true))
        {
            for (const CNetAddr &addr : vaddr)
            {
                if (AddLocal(addr, LOCAL_IF))
                    LOGA("%s: %s - %s\n", __func__, pszHostName, addr.ToString());
            }
        }
    }
#else
    // Get local host ip
    struct ifaddrs *myaddrs;
    if (getifaddrs(&myaddrs) == 0)
    {
        for (struct ifaddrs *ifa = myaddrs; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == nullptr)
                continue;
            if ((ifa->ifa_flags & IFF_UP) == 0)
                continue;
            if (strcmp(ifa->ifa_name, "lo") == 0)
                continue;
            if (strcmp(ifa->ifa_name, "lo0") == 0)
                continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            {
                struct sockaddr_in *s4 = (struct sockaddr_in *)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LOGA("%s: IPv4 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6)
            {
                struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr, s6->sin6_scope_id);
                if (AddLocal(addr, LOCAL_IF))
                    LOGA("%s: IPv6 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
        }
        freeifaddrs(myaddrs);
    }
#endif
}

void StartNode()
{
    uiInterface.InitMessage(_("Loading addresses..."));
    // Load addresses from peers.dat
    int64_t nStart = GetTimeMillis();
    {
        CAddrDB adb;
        if (adb.Read(addrman))
        {
            LOGA("Loaded %i addresses from peers.dat  %dms\n", addrman.size(), GetTimeMillis() - nStart);
        }
        else
        {
            // Addrman can be in an inconsistent state after failure, reset it
            addrman.Clear();
            LOGA("Invalid or missing peers.dat; recreating\n");
        }
    }

    // ask dos manager to load banlist from disk (or recreate if missing/corrupt)
    dosMan.LoadBanlist();

    fAddressesInitialized = true;

    if (semOutbound == nullptr)
    {
        // initialize semaphore
        int nMaxOutbound = std::min((nMaxOutConnections + MAX_FEELER_CONNECTIONS), nMaxConnections);
        semOutbound = new CSemaphore(nMaxOutbound);
    }

    // We need to initialize vAddedNodes here.  It is now used in AcceptConnection to limit the number of inbound
    // connections based on the configured "addnode" options from bitcoin.conf/command line, however the old
    // initialization location in ThreadOpenAddedConnections was both started after ThreadSocketHandler, which
    // calls AcceptConnection, and has an explicit 15 second delay to the start of ThreadOpenAddedConnections
    // which allows any nodes actively trying to connect to this node during startup to exceed the inbound connection
    // limit
    {
        LOCK(cs_vAddedNodes);
        vAddedNodes = mapMultiArgs["-addnode"];
    }

    if (pnodeLocalHost == nullptr)
        pnodeLocalHost = new CNode(INVALID_SOCKET, CAddress(CService("127.0.0.1", 0), nLocalServices));

    Discover();

    //
    // Start threads
    //

    threadGroup.create_thread(&ThreadAddressSeeding);

    // Map ports with UPnP
    MapPort(GetBoolArg("-upnp", DEFAULT_UPNP));

    // Send and receive from sockets, accept connections
    threadGroup.create_thread(&ThreadSocketHandler);

    // Initiate outbound connections from -addnode
    threadGroup.create_thread(&ThreadOpenAddedConnections);

    // Initiate outbound connections
    threadGroup.create_thread(&ThreadOpenConnections);

    // Process messages
    for (unsigned int i = 0; i < numMsgHandlerThreads.Value(); i++)
    {
        threadGroup.create_thread(&ThreadMessageHandler);
    }

    // Dump network addresses
    threadGroup.create_thread(&DumpData, DUMP_ADDRESSES_INTERVAL);

    // Update peer misbehavior points
    threadGroup.create_thread(&UpdateMisbehaviorPoints, UPDATE_MISBEHAVIOR_INTERVAL);
}

bool StopNode()
{
    LOGA("StopNode()\n");
    MapPort(false);
    if (semOutbound)
        for (int i = 0; i < (nMaxOutConnections + MAX_FEELER_CONNECTIONS); i++)
            semOutbound->post();

    if (fAddressesInitialized)
    {
        DumpData(0);
        fAddressesInitialized = false;
    }

    return true;
}

void NetCleanup()
{
    // clean up some globals (to help leak detection)
    {
        LOCK(cs_vNodes);

        // Close sockets
        for (CNode *pnode : vNodes)
        {
            // Since we are quitting, disconnect abruptly from the node rather than finishing up our conversation
            // with it.
            {
                LOCK(pnode->cs_vRecvMsg);
                pnode->vRecvMsg.clear();
                pnode->vRecvMsg_handshake.clear();
            }
            {
                LOCK(pnode->cs_vSend);
                pnode->ssSend.clear();
            }
            pnode->nSendSize.store(0);
            // Now close communications with the other node
            pnode->CloseSocketDisconnect();
        }
        for (ListenSocket &hListenSocket : vhListenSocket)
        {
            if (hListenSocket.socket != INVALID_SOCKET)
                if (!CloseSocket(hListenSocket.socket))
                    LOG(NET, "CloseSocket(hListenSocket) failed with error %s\n",
                        NetworkErrorString(WSAGetLastError()));
        }
    }

    // Try to let nodes be cleaned up for a while, but ultimately give up because we are shutting down.
    for (int iters = 0; iters < 20; iters++)
    {
        CleanupDisconnectedNodes();
        {
            LOCK2(cs_vNodes, cs_vNodesDisconnected);
            if ((vNodes.size() == 0) && (vNodesDisconnected.size() == 0))
                break; // every node is properly disconnected
        }
        MilliSleep(100); // Give other threads a chance to finish up using the node.
    }


    {
        LOCK2(cs_vNodes, cs_vNodesDisconnected);
        if (!((vNodes.size() == 0) && (vNodesDisconnected.size() == 0)))
        {
            LOG(NET, "Some node objects were not properly cleaned up.\n");
        }

        // If the nodes were not properly shut down, remove them from the vNodes list now, so the vNode item
        // is not leaked.
        // The node memory itself will be leaked but since we are quitting this is not a big issue.
        // We cannot just delete them because some other thread still has a reference.
        vNodes.clear();
        vNodesDisconnected.clear();
        vhListenSocket.clear();
        if (semOutbound)
            delete semOutbound;
        semOutbound = nullptr;
        // BU: clean up the "-addnode" semaphore
        if (semOutboundAddNode)
            delete semOutboundAddNode;
        semOutboundAddNode = nullptr;
        if (pnodeLocalHost)
            delete pnodeLocalHost;
        pnodeLocalHost = nullptr;

#ifdef WIN32
        // Shutdown Windows Sockets
        WSACleanup();
#endif
    }
}


void RelayTransaction(const CTransactionRef ptx, const CTxProperties *txProperties)
{
    if (ptx->GetTxSize() > maxTxSize.Value())
    {
        LOGA("Will not announce (INV) excessive transaction %s.  Size: %llu, Limit: %llu\n", ptx->GetHash().ToString(),
            ptx->GetTxSize(), (uint64_t)maxTxSize.Value());
        return;
    }

    CInv inv(MSG_TX, ptx->GetHash());
    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay.insert(std::make_pair(inv, ptx));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }

    LOCK(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if (!pnode->fRelayTxes)
        {
            continue;
        }
        // If the transaction won't be acceptable to the target node, then don't send it.  This avoids poisoning
        // the node against this tx (via the node's alreadyHave() logic), so that it can be sent later.
        if (txProperties && (!pnode->IsTxAcceptable(*txProperties)))
        {
            continue;
        }

        LOCK(pnode->cs_filter);
        // If the bloom filter is not empty then a peer must have sent us a filter
        // and we can assume this node is an SPV node.
        if (pnode->pfilter && !pnode->pfilter->IsEmpty())
        {
            if (pnode->pfilter->IsRelevantAndUpdate(ptx))
            {
                pnode->PushInventory(inv);
            }
        }
        else
        {
            pnode->PushInventory(inv);
        }
    }
}

void CNode::RecordBytesRecv(uint64_t bytes) { nTotalBytesRecv.fetch_add(bytes); }
void CNode::RecordBytesSent(uint64_t bytes)
{
    nTotalBytesSent.fetch_add(bytes);

    uint64_t now = GetTime();
    if (nMaxOutboundCycleStartTime + nMaxOutboundTimeframe < now)
    {
        // timeframe expired, reset cycle
        nMaxOutboundCycleStartTime = now;
        nMaxOutboundTotalBytesSentInCycle = 0;
    }

    // TODO, exclude whitebind peers
    nMaxOutboundTotalBytesSentInCycle.fetch_add(bytes);
}

void CNode::SetMaxOutboundTarget(uint64_t limit)
{
    uint64_t nRecommendedMinimum = (nMaxOutboundTimeframe * excessiveBlockSize) / 600;
    nMaxOutboundLimit = limit;

    if (limit > 0 && limit < nRecommendedMinimum)
        LOGA("Max outbound target is very small (%s bytes) and will be overshot. Recommended minimum is %s bytes.\n",
            nMaxOutboundLimit, nRecommendedMinimum);
}

uint64_t CNode::GetMaxOutboundTarget() { return nMaxOutboundLimit; }
uint64_t CNode::GetMaxOutboundTimeframe() { return nMaxOutboundTimeframe; }
uint64_t CNode::GetMaxOutboundTimeLeftInCycle()
{
    if (nMaxOutboundLimit == 0)
        return 0;

    if (nMaxOutboundCycleStartTime == 0)
        return nMaxOutboundTimeframe;

    uint64_t cycleEndTime = nMaxOutboundCycleStartTime + nMaxOutboundTimeframe;
    uint64_t now = GetTime();
    return (cycleEndTime < now) ? 0 : cycleEndTime - GetTime();
}

void CNode::SetMaxOutboundTimeframe(uint64_t timeframe)
{
    if (nMaxOutboundTimeframe != timeframe)
    {
        // reset measure-cycle in case of changing
        // the timeframe
        nMaxOutboundCycleStartTime = GetTime();
    }
    nMaxOutboundTimeframe = timeframe;
}

bool CNode::OutboundTargetReached(bool fHistoricalBlockServingLimit)
{
    if (nMaxOutboundLimit == 0)
        return false;

    if (fHistoricalBlockServingLimit)
    {
        // keep a large enough buffer to at least relay each block once
        uint64_t timeLeftInCycle = GetMaxOutboundTimeLeftInCycle();
        uint64_t buffer = (timeLeftInCycle * excessiveBlockSize) / 600;
        if (buffer >= nMaxOutboundLimit || nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit - buffer)
            return true;
    }
    else if (nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit)
        return true;

    return false;
}

uint64_t CNode::GetOutboundTargetBytesLeft()
{
    if (nMaxOutboundLimit == 0)
        return 0;

    return (nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit) ? 0 : nMaxOutboundLimit -
                                                                              nMaxOutboundTotalBytesSentInCycle;
}

uint64_t CNode::GetTotalBytesRecv() { return nTotalBytesRecv; }
uint64_t CNode::GetTotalBytesSent() { return nTotalBytesSent; }
void CNode::Fuzz(int nChance)
{
    AssertLockHeld(cs_vSend);
    if (!fSuccessfullyConnected)
        return; // Don't fuzz initial handshake
    if (GetRand(nChance) != 0)
        return; // Fuzz 1 of every nChance messages

    switch (GetRand(3))
    {
    case 0:
        // xor a random byte with a random value:
        if (!ssSend.empty())
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend[pos] ^= (unsigned char)(GetRand(256));
        }
        break;
    case 1:
        // delete a random byte:
        if (!ssSend.empty())
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend.erase(ssSend.begin() + pos);
        }
        break;
    case 2:
        // insert a random byte at a random position
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            char ch = (char)GetRand(256);
            ssSend.insert(ssSend.begin() + pos, ch);
        }
        break;
    }
    // Chance of more than one change half the time:
    // (more changes exponentially less likely):
    Fuzz(2);
}

//
// CAddrDB
//

CAddrDB::CAddrDB() { pathAddr = GetDataDir() / "peers.dat"; }
bool CAddrDB::Write(const CAddrMan &addr)
{
    // Generate random temporary filename
    unsigned short randv = 0;
    GetRandBytes((unsigned char *)&randv, sizeof(randv));
    std::string tmpfn = strprintf("peers.dat.%04x", randv);

    // serialize addresses, checksum data up to that point, then append csum
    CDataStream ssPeers(SER_DISK, CLIENT_VERSION);
    ssPeers << FLATDATA(Params().MessageStart());
    ssPeers << addr;
    uint256 hash = Hash(ssPeers.begin(), ssPeers.end());
    ssPeers << hash;

    // open temp output file, and associate with CAutoFile
    boost::filesystem::path pathTmp = GetDataDir() / tmpfn;
    FILE *file = fopen(pathTmp.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: Failed to open file %s", __func__, pathTmp.string());

    // Write and commit header, data
    try
    {
        fileout << ssPeers;
    }
    catch (const std::exception &e)
    {
        return error("%s: Serialize or I/O error - %s", __func__, e.what());
    }
    FileCommit(fileout.Get());
    fileout.fclose();

    // replace existing peers.dat, if any, with new peers.dat.XXXX
    if (!RenameOver(pathTmp, pathAddr))
        return error("%s: Rename-into-place failed", __func__);

    return true;
}

bool CAddrDB::Read(CAddrMan &addr)
{
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathAddr.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: Failed to open file %s", __func__, pathAddr.string());

    // use file size to size memory buffer
    uint64_t fileSize = boost::filesystem::file_size(pathAddr);
    uint64_t dataSize = 0;
    // Don't try to resize to a negative number if file is small
    if (fileSize >= sizeof(uint256))
        dataSize = fileSize - sizeof(uint256);
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try
    {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (const std::exception &e)
    {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }
    filein.fclose();

    CDataStream ssPeers(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPeers.begin(), ssPeers.end());
    if (hashIn != hashTmp)
        return error("%s: Checksum mismatch, data corrupted", __func__);

    return Read(addr, ssPeers);
}

bool CAddrDB::Read(CAddrMan &addr, CDataStream &ssPeers)
{
    unsigned char pchMsgTmp[4];
    try
    {
        // de-serialize file header (network specific magic number) and ..
        ssPeers >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
            return error("%s: Invalid network magic number", __func__);

        // de-serialize address data into one CAddrMan object
        ssPeers >> addr;
    }
    catch (const std::exception &e)
    {
        // de-serialization has failed, ensure addrman is left in a clean state
        addr.Clear();
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    return true;
}

unsigned int ReceiveFloodSize() { return 1000 * GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER); }
unsigned int SendBufferSize() { return 1000 * GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER); }
CNode::CNode(SOCKET hSocketIn, const CAddress &addrIn, const std::string &addrNameIn, bool fInboundIn)
    : xVersionEnabled(false), skipChecksum(false), ssSend(SER_NETWORK, INIT_PROTO_VERSION), id(connmgr->NextNodeId()),
      addrKnown(5000, 0.001)
{
    nServices = 0;
    hSocket = hSocketIn;
    nRecvVersion = INIT_PROTO_VERSION;
    nSendBytes = 0;
    nRecvBytes = 0;
    nTimeConnected = GetTime();
    nStopwatchConnected = GetStopwatchMicros();
    nTimeOffset = 0;
    addr = addrIn;
    addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
    nVersion = 0;
    strSubVer = "";
    fWhitelisted = false;
    fOneShot = false;
    fClient = false; // set by version message
    m_limited_node = false; // set by version message
    fFeeler = false;
    fInbound = fInboundIn;
    fAutoOutbound = false;
    fNetworkNode = false;
    tVersionSent = -1;
    fSuccessfullyConnected = false;
    fDisconnect = false;
    fDisconnectRequest = false;
    nRefCount = 0;
    nSendSize = 0;
    nSendOffset = 0;
    hashContinue = uint256();
    nStartingHeight = -1;
    filterInventoryKnown.reset();
    fGetAddr = false;
    nNextLocalAddrSend = 0;
    nNextAddrSend = 0;
    nNextInvSend = 0;
    fRelayTxes = false;
    fSentAddr = false;
    pfilter = new CBloomFilter();
    pThinBlockFilter = new CBloomFilter(); // BUIP010 - Xtreme Thinblocks
    nPingNonceSent = 0;
    nPingUsecStart = 0;
    nPingUsecTime = 0;
    fPingQueued = false;
    nMinPingUsecTime = std::numeric_limits<int64_t>::max();

    msg = CNetMessage(GetMagic(Params()), SER_NETWORK, nRecvVersion);

    // xthinblocks
    nXthinBloomfilterSize = 0;
    addrFromPort = 0;

    // graphene
    gr_shorttxidk0 = 0;
    gr_shorttxidk1 = 0;

    // compact blocks
    shorttxidk0 = 0;
    shorttxidk1 = 0;

    // performance tracking
    nAvgBlkResponseTime = -1.0;
    nMaxBlocksInTransit = 16;

    // For statistics only, BU doesn't support CB protocol
    fSupportsCompactBlocks = false;

    // BU instrumentation
    std::string xmledName;
    if (addrNameIn != "")
        xmledName = addrNameIn;
    else
    {
        xmledName = "ip" + addr.ToStringIP() + "p" + addr.ToStringPort();
    }
    bytesSent.init("node/" + xmledName + "/bytesSent");
    bytesReceived.init("node/" + xmledName + "/bytesReceived");
    txReqLatency.init("node/" + xmledName + "/txLatency", STAT_OP_AVE);
    firstTx.init("node/" + xmledName + "/firstTx");
    firstBlock.init("node/" + xmledName + "/firstBlock");
    blocksSent.init("node/" + xmledName + "/blocksSent");
    txsSent.init("node/" + xmledName + "/txsSent");

    sendGap.init("node/" + xmledName + "/sendGap", STAT_OP_MAX);
    recvGap.init("node/" + xmledName + "/recvGap", STAT_OP_MAX);

    if (fLogIPs)
    {
        LOG(NET, "Added connection to %s (%d)\n", addrName, id);
    }
    else
    {
        LOG(NET, "Added connection peer=%d\n", id);
    }

    // Be shy and don't send version until we hear
    if (hSocket != INVALID_SOCKET && !fInbound)
        PushVersion();

    GetNodeSignals().InitializeNode(this);
}

CNode::~CNode()
{
    CloseSocket(hSocket);

    { // locking should be unnecessary because nothing is holding a reference to this node anymore, so single-threaded
        // however, lock here for static analysis correctness.
        LOCK(cs_filter);
        if (pfilter)
        {
            delete pfilter;
            pfilter = nullptr; // BU
        }

        if (pThinBlockFilter)
        {
            delete pThinBlockFilter;
            pThinBlockFilter = nullptr;
        }
    }

    // We must set this to false on disconnect otherwise we will have trouble reconnecting -addnode nodes
    // if the remote peer restarts.
    fAutoOutbound = false;

    addrFromPort = 0;

    // Update addrman timestamp
    if (nMisbehavior == 0 && fSuccessfullyConnected)
        addrman.Connected(addr);

    // Decrement thintype peer counters
    thinrelay.RemovePeers(this);

    GetNodeSignals().FinalizeNode(GetId());
}

void CNode::BeginMessage(const char *pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend)
{
    ENTER_CRITICAL_SECTION(cs_vSend);
    assert(ssSend.size() == 0);
    ssSend << CMessageHeader(GetMagic(Params()), pszCommand, 0);
    LOG(NET, "sending msg: %s to %s\n", SanitizeString(pszCommand), GetLogName());
    currentCommand = pszCommand;
}

void CNode::AbortMessage() UNLOCK_FUNCTION(cs_vSend)
{
    ssSend.clear();
    LEAVE_CRITICAL_SECTION(cs_vSend);
    LOG(NET, "(aborted)\n");
}

void CNode::EndMessage() UNLOCK_FUNCTION(cs_vSend)
{
    // The -*messagestest options are intentionally not documented in the help message,
    // since they are only used during development to debug the networking code and are
    // not intended for end-users.
    if (mapArgs.count("-dropmessagestest") && GetRand(GetArg("-dropmessagestest", 2)) == 0)
    {
        LOG(NET, "dropmessages DROPPING SEND MESSAGE\n");
        AbortMessage();
        return;
    }
    if (mapArgs.count("-fuzzmessagestest"))
        Fuzz(GetArg("-fuzzmessagestest", 10));

    if (ssSend.size() == 0)
    {
        LEAVE_CRITICAL_SECTION(cs_vSend);
        return;
    }
    // Set the size
    unsigned int nSize = ssSend.size() - CMessageHeader::HEADER_SIZE;
    WriteLE32((uint8_t *)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], nSize);

    UpdateSendStats(this, currentCommand, nSize + CMessageHeader::HEADER_SIZE, GetTimeMicros());

    // Set the checksum
    uint32_t nChecksum = 0; // If we can skip the checksum, we send 0 instead
    if (!skipChecksum)
    {
        uint256 hash = Hash(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
        memcpy(&nChecksum, &hash, sizeof(nChecksum));
    }
    assert(ssSend.size() >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
    memcpy((char *)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

    LOG(NET, "(%d bytes) peer=%d\n", nSize, id);

    // Connection slot attack mitigation.  We don't want to add useful bytes for outgoing INV, PING, ADDR,
    // VERSION or VERACK messages since attackers will often just connect and listen to INV messages.
    // We want to make sure that connected nodes are doing useful work in sending us data or requesting data.
    std::deque<CSerializeData>::iterator it;
    char strCommand[CMessageHeader::COMMAND_SIZE + 1];
    strncpy(strCommand, &(*(ssSend.begin() + MESSAGE_START_SIZE)), CMessageHeader::COMMAND_SIZE);
    strCommand[CMessageHeader::COMMAND_SIZE] = '\0';
    if (strcmp(strCommand, NetMsgType::PING) != 0 && strcmp(strCommand, NetMsgType::PONG) != 0 &&
        strcmp(strCommand, NetMsgType::ADDR) != 0 && strcmp(strCommand, NetMsgType::VERSION) != 0 &&
        strcmp(strCommand, NetMsgType::VERACK) != 0 && strcmp(strCommand, NetMsgType::INV) != 0)
    {
        nActivityBytes.fetch_add(nSize);
    }

    // If the message is a priority message then move it to priority queue.
    if (IsPriorityMsg(strCommand))
    {
        it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
        ssSend.GetAndClear(*it);
        nSendSize.fetch_add((*it).size());
        LOG(PRIORITYQ, "Send Queue: pushed %s to the priority queue, peer(%d)\n", strCommand, this->GetId());

        LOCK(cs_prioritySendQ);
        vPrioritySendQ.push_back(CNodeRef(this));
        fPrioritySendMsg = true;
    }
    else
    {
        it = vLowPrioritySendMsg.insert(vLowPrioritySendMsg.end(), CSerializeData());
        ssSend.GetAndClear(*it);
        nSendSize.fetch_add((*it).size());
    }

    // if only 1 message is in queue then attempt and "optimistic" send
    if (vSendMsg.size() == 1)
    {
        SocketSendData(this);
    }
    else if (vSendMsg.empty() && (vLowPrioritySendMsg.size() == 1))
    {
        SocketSendData(this);
    }

    LEAVE_CRITICAL_SECTION(cs_vSend);
}

/**
 * Check if it is flagged for banning, and if so ban it and disconnect.
 */
void CNode::DisconnectIfBanned()
{
    if (fShouldBan)
    {
        fShouldBan = false;

        if (fWhitelisted)
        {
            LOGA("Warning: not banning whitelisted peer %s!\n", GetLogName());
        }
        else if (connmgr->IsExpeditedUpstream(this))
        {
            LOG(THIN, "Warning: not banning expedited peer %s!\n", GetLogName());
        }
        else if (addr.IsLocal())
        {
            nMisbehavior.store(0);
            nBanType.store(-1);
            LOGA("Warning: not banning local peer %s!\n", GetLogName());
        }
        else
        {
            fDisconnect = true;
            dosMan.Ban(addr, cleanSubVer, (BanReason)nBanType.load());
        }
    }
}

void CNode::ReadConfigFromXVersion_OLD()
{
    xVersionEnabled = true;
    LOCK(cs_xversion);
    skipChecksum = (xVersion.as_u64c(XVer::BU_MSG_IGNORE_CHECKSUM_OLD) == 1);
    if (addrFromPort == 0)
    {
        addrFromPort = xVersion.as_u64c(XVer::BU_LISTEN_PORT_OLD) & 0xffff;
    }

    uint64_t num = xVersion.as_u64c(XVer::BU_MEMPOOL_ANCESTOR_COUNT_LIMIT_OLD);
    if (num)
        nLimitAncestorCount = num; // num == 0 means the field was not provided.
    num = xVersion.as_u64c(XVer::BU_MEMPOOL_ANCESTOR_SIZE_LIMIT_OLD);
    if (num)
        nLimitAncestorSize = num;

    num = xVersion.as_u64c(XVer::BU_MEMPOOL_DESCENDANT_COUNT_LIMIT_OLD);
    if (num)
        nLimitDescendantCount = num;
    num = xVersion.as_u64c(XVer::BU_MEMPOOL_DESCENDANT_SIZE_LIMIT_OLD);
    if (num)
        nLimitDescendantSize = num;

    canSyncMempoolWithPeers = (xVersion.as_u64c(XVer::BU_MEMPOOL_SYNC_OLD) == 1);
    nMempoolSyncMinVersionSupported = xVersion.as_u64c(XVer::BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED_OLD);
    nMempoolSyncMaxVersionSupported = xVersion.as_u64c(XVer::BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED_OLD);
    txConcat = xVersion.as_u64c(XVer::BU_TXN_CONCATENATION_OLD);
    minGrapheneVersion = xVersion.as_u64c(XVer::BU_GRAPHENE_MIN_VERSION_SUPPORTED_OLD);
    maxGrapheneVersion = xVersion.as_u64c(XVer::BU_GRAPHENE_MAX_VERSION_SUPPORTED_OLD);

    {
        uint64_t selfMax = grapheneMaxVersionSupported.Value();
        uint64_t selfMin = grapheneMinVersionSupported.Value();

        uint64_t upper = (uint64_t)std::min(maxGrapheneVersion, selfMax);
        uint64_t lower = (uint64_t)std::max(minGrapheneVersion, selfMin);
        if (lower > upper)
            negotiatedGrapheneVersion = GRAPHENE_NO_VERSION_SUPPORTED;
        else
            negotiatedGrapheneVersion = upper;
    }
}

void CNode::ReadConfigFromXVersion()
{
    xVersionEnabled = true;
    LOCK(cs_xversion);
    skipChecksum = (xVersion.as_u64c(XVer::BU_MSG_IGNORE_CHECKSUM) == 1);
    if (addrFromPort == 0)
    {
        addrFromPort = xVersion.as_u64c(XVer::BU_LISTEN_PORT) & 0xffff;
    }

    uint64_t num = xVersion.as_u64c(XVer::BU_MEMPOOL_ANCESTOR_COUNT_LIMIT);
    if (num)
        nLimitAncestorCount = num; // num == 0 means the field was not provided.
    num = xVersion.as_u64c(XVer::BU_MEMPOOL_ANCESTOR_SIZE_LIMIT);
    if (num)
        nLimitAncestorSize = num;

    num = xVersion.as_u64c(XVer::BU_MEMPOOL_DESCENDANT_COUNT_LIMIT);
    if (num)
        nLimitDescendantCount = num;
    num = xVersion.as_u64c(XVer::BU_MEMPOOL_DESCENDANT_SIZE_LIMIT);
    if (num)
        nLimitDescendantSize = num;

    canSyncMempoolWithPeers = (xVersion.as_u64c(XVer::BU_MEMPOOL_SYNC) == 1);
    nMempoolSyncMinVersionSupported = xVersion.as_u64c(XVer::BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED);
    nMempoolSyncMaxVersionSupported = xVersion.as_u64c(XVer::BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED);
    txConcat = xVersion.as_u64c(XVer::BU_TXN_CONCATENATION);
    minGrapheneVersion = xVersion.as_u64c(XVer::BU_GRAPHENE_MIN_VERSION_SUPPORTED);
    maxGrapheneVersion = xVersion.as_u64c(XVer::BU_GRAPHENE_MAX_VERSION_SUPPORTED);

    {
        uint64_t selfMax = grapheneMaxVersionSupported.Value();
        uint64_t selfMin = grapheneMinVersionSupported.Value();

        uint64_t upper = (uint64_t)std::min(maxGrapheneVersion, selfMax);
        uint64_t lower = (uint64_t)std::max(minGrapheneVersion, selfMin);
        if (lower > upper)
            negotiatedGrapheneVersion = GRAPHENE_NO_VERSION_SUPPORTED;
        else
            negotiatedGrapheneVersion = upper;
    }
}


int64_t PoissonNextSend(int64_t nNow, int average_interval_seconds)
{
    return nNow + (int64_t)(log1p(GetRand(1ULL << 48) * -0.0000000000000035527136788 /* -1/2^48 */) *
                                average_interval_seconds * -1000000.0 +
                            0.5);
}
