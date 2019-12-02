// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// must include first to ensure FD_SETSIZE is correctly set
// otherwise the default of 64 may be used on Windows
#include "compat.h"

#include "netbase.h"

#include "random.h"
#include "sync.h"
#include "threadgroup.h"
#include "util.h"
#include "utilstrencodings.h"

#include <atomic>

#ifdef HAVE_GETADDRINFO_A
#include <netdb.h>
#endif

#ifndef WIN32
#if HAVE_INET_PTON
#include <arpa/inet.h>
#endif
#include <fcntl.h>
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Settings
// BU move to globals.cpp
extern proxyType proxyInfo[NET_MAX];
extern proxyType nameProxy;
extern CCriticalSection cs_proxyInfos;
int nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;
bool fNameLookup = DEFAULT_NAME_LOOKUP;

// Need ample time for negotiation for very slow proxies such as Tor (milliseconds)
static const int SOCKS5_RECV_TIMEOUT = 20 * 1000;

enum Network ParseNetwork(std::string net)
{
    boost::to_lower(net);
    if (net == "ipv4")
        return NET_IPV4;
    if (net == "ipv6")
        return NET_IPV6;
    if (net == "tor" || net == "onion")
        return NET_TOR;
    return NET_UNROUTABLE;
}

std::string GetNetworkName(enum Network net)
{
    switch (net)
    {
    case NET_IPV4:
        return "ipv4";
    case NET_IPV6:
        return "ipv6";
    case NET_TOR:
        return "onion";
    default:
        return "";
    }
}

void SplitHostPort(std::string in, int &portOut, std::string &hostOut)
{
    size_t colon = in.find_last_of(':');
    // if a : is found, and it either follows a [...], or no other : is in the string, treat it as port separator
    bool fHaveColon = colon != in.npos;
    // if there is a colon, and in[0]=='[', colon is not 0, so in[colon-1] is safe
    bool fBracketed = fHaveColon && (in[0] == '[' && in[colon - 1] == ']');
    bool fMultiColon = fHaveColon && (in.find_last_of(':', colon - 1) != in.npos);
    if (fHaveColon && (colon == 0 || fBracketed || !fMultiColon))
    {
        int32_t n;
        if (ParseInt32(in.substr(colon + 1), &n) && n > 0 && n < 0x10000)
        {
            in = in.substr(0, colon);
            portOut = n;
        }
    }
    if (in.size() > 0 && in[0] == '[' && in[in.size() - 1] == ']')
        hostOut = in.substr(1, in.size() - 2);
    else
        hostOut = in;
}

bool static LookupIntern(const char *pszName, std::vector<CNetAddr> &vIP, unsigned int nMaxSolutions, bool fAllowLookup)
{
    vIP.clear();

    {
        CNetAddr addr;
        if (addr.SetSpecial(std::string(pszName)))
        {
            vIP.push_back(addr);
            return true;
        }
    }

#ifdef HAVE_GETADDRINFO_A
    struct in_addr ipv4_addr;
#ifdef HAVE_INET_PTON
    if (inet_pton(AF_INET, pszName, &ipv4_addr) > 0)
    {
        vIP.push_back(CNetAddr(ipv4_addr));
        return true;
    }

    struct in6_addr ipv6_addr;
    if (inet_pton(AF_INET6, pszName, &ipv6_addr) > 0)
    {
        vIP.push_back(CNetAddr(ipv6_addr));
        return true;
    }
#else
    ipv4_addr.s_addr = inet_addr(pszName);
    if (ipv4_addr.s_addr != INADDR_NONE)
    {
        vIP.push_back(CNetAddr(ipv4_addr));
        return true;
    }
#endif
#endif

    struct addrinfo aiHint;
    memset(&aiHint, 0, sizeof(struct addrinfo));
    aiHint.ai_socktype = SOCK_STREAM;
    aiHint.ai_protocol = IPPROTO_TCP;
    aiHint.ai_family = AF_UNSPEC;
#ifdef WIN32
    aiHint.ai_flags = fAllowLookup ? 0 : AI_NUMERICHOST;
#else
    aiHint.ai_flags = fAllowLookup ? AI_ADDRCONFIG : AI_NUMERICHOST;
#endif

    struct addrinfo *aiRes = nullptr;
#ifdef HAVE_GETADDRINFO_A
    struct gaicb gcb, *query = &gcb;
    memset(query, 0, sizeof(struct gaicb));
    gcb.ar_name = pszName;
    gcb.ar_request = &aiHint;
    int nErr = getaddrinfo_a(GAI_NOWAIT, &query, 1, nullptr);
    if (nErr)
        return false;

    do
    {
        // Should set the timeout limit to a reasonable value to avoid
        // generating unnecessary checking call during the polling loop,
        // while it can still response to stop request quick enough.
        // 2 seconds looks fine in our situation.
        struct timespec ts = {2, 0};
        gai_suspend(&query, 1, &ts);
        if (shutdown_threads.load() == true)
        {
            return false;
        }

        nErr = gai_error(query);
        if (0 == nErr)
            aiRes = query->ar_result;
    } while (nErr == EAI_INPROGRESS);
#else
    int nErr = getaddrinfo(pszName, nullptr, &aiHint, &aiRes);
#endif
    if (nErr)
        return false;

    struct addrinfo *aiTrav = aiRes;
    while (aiTrav != nullptr && (nMaxSolutions == 0 || vIP.size() < nMaxSolutions))
    {
        if (aiTrav->ai_family == AF_INET)
        {
            assert(aiTrav->ai_addrlen >= sizeof(sockaddr_in));
            vIP.push_back(CNetAddr(((struct sockaddr_in *)(aiTrav->ai_addr))->sin_addr));
        }

        if (aiTrav->ai_family == AF_INET6)
        {
            assert(aiTrav->ai_addrlen >= sizeof(sockaddr_in6));
            struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)aiTrav->ai_addr;
            vIP.push_back(CNetAddr(s6->sin6_addr, s6->sin6_scope_id));
        }

        aiTrav = aiTrav->ai_next;
    }

    freeaddrinfo(aiRes);

    return (vIP.size() > 0);
}

bool LookupHost(const char *pszName, std::vector<CNetAddr> &vIP, unsigned int nMaxSolutions, bool fAllowLookup)
{
    std::string strHost(pszName);
    if (strHost.empty())
        return false;
    if (boost::algorithm::starts_with(strHost, "[") && boost::algorithm::ends_with(strHost, "]"))
    {
        strHost = strHost.substr(1, strHost.size() - 2);
    }

    return LookupIntern(strHost.c_str(), vIP, nMaxSolutions, fAllowLookup);
}

bool Lookup(const char *pszName,
    std::vector<CService> &vAddr,
    int portDefault,
    unsigned int nMaxSolutions,
    bool fAllowLookup)
{
    if (pszName[0] == 0)
        return false;
    int port = portDefault;
    std::string hostname = "";
    SplitHostPort(std::string(pszName), port, hostname);

    std::vector<CNetAddr> vIP;
    bool fRet = LookupIntern(hostname.c_str(), vIP, nMaxSolutions, fAllowLookup);
    if (!fRet)
        return false;
    vAddr.resize(vIP.size());
    for (unsigned int i = 0; i < vIP.size(); i++)
        vAddr[i] = CService(vIP[i], port);
    return true;
}

bool Lookup(const char *pszName, CService &addr, int portDefault, bool fAllowLookup)
{
    std::vector<CService> vService;
    bool fRet = Lookup(pszName, vService, portDefault, 1, fAllowLookup);
    if (!fRet)
        return false;
    addr = vService[0];
    return true;
}

bool LookupNumeric(const char *pszName, CService &addr, int portDefault)
{
    return Lookup(pszName, addr, portDefault, false);
}

/** SOCKS version */
enum SOCKSVersion : uint8_t
{
    SOCKS4 = 0x04,
    SOCKS5 = 0x05
};

/** Values defined for METHOD in RFC1928 */
enum SOCKS5Method : uint8_t
{
    NOAUTH = 0x00, //! No authentication required
    GSSAPI = 0x01, //! GSSAPI
    USER_PASS = 0x02, //! Username/password
    NO_ACCEPTABLE = 0xff, //! No acceptable methods
};

/** Values defined for CMD in RFC1928 */
enum SOCKS5Command : uint8_t
{
    CONNECT = 0x01,
    BIND = 0x02,
    UDP_ASSOCIATE = 0x03
};

/** Values defined for REP in RFC1928 */
enum SOCKS5Reply : uint8_t
{
    SUCCEEDED = 0x00, //! Succeeded
    GENFAILURE = 0x01, //! General failure
    NOTALLOWED = 0x02, //! Connection not allowed by ruleset
    NETUNREACHABLE = 0x03, //! Network unreachable
    HOSTUNREACHABLE = 0x04, //! Network unreachable
    CONNREFUSED = 0x05, //! Connection refused
    TTLEXPIRED = 0x06, //! TTL expired
    CMDUNSUPPORTED = 0x07, //! Command not supported
    ATYPEUNSUPPORTED = 0x08, //! Address type not supported
};

/** Values defined for ATYPE in RFC1928 */
enum SOCKS5Atyp : uint8_t
{
    IPV4 = 0x01,
    DOMAINNAME = 0x03,
    IPV6 = 0x04,
};

struct timeval MillisToTimeval(int64_t nTimeout)
{
    struct timeval timeout;
    timeout.tv_sec = nTimeout / 1000;
    timeout.tv_usec = (nTimeout % 1000) * 1000;
    return timeout;
}

/**
 * Read bytes from socket. This will either read the full number of bytes requested
 * or return False on error or timeout.
 * This function can be interrupted by boost thread interrupt.
 *
 * @param data Buffer to receive into
 * @param len  Length of data to receive
 * @param timeout  Timeout in milliseconds for receive operation
 *
 * @note This function requires that hSocket is in non-blocking mode.
 */
bool static InterruptibleRecv(uint8_t *data, size_t len, int timeout, SOCKET &hSocket)
{
    int64_t curTime = GetTimeMillis();
    int64_t endTime = curTime + timeout;
    // Maximum time to wait in one select call. It will take up until this time (in millis)
    // to break off in case of an interruption.
    const int64_t maxWait = 1000;
    while (len > 0 && curTime < endTime)
    {
        ssize_t ret = recv(hSocket, (char *)data, len, 0); // Optimistically try the recv first
        if (ret > 0)
        {
            len -= ret;
            data += ret;
        }
        else if (ret == 0)
        { // Unexpected disconnection
            return false;
        }
        else
        { // Other error or blocking
            int nErr = WSAGetLastError();
            if (nErr == WSAEINPROGRESS || nErr == WSAEWOULDBLOCK || nErr == WSAEINVAL)
            {
                if (!IsSelectableSocket(hSocket))
                {
                    return false;
                }
                struct timeval tval = MillisToTimeval(std::min(endTime - curTime, maxWait));
                fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(hSocket, &fdset);
                int nRet = select(hSocket + 1, &fdset, nullptr, nullptr, &tval);
                if (nRet == SOCKET_ERROR)
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
        if (shutdown_threads.load() == true)
        {
            return false;
        }
        curTime = GetTimeMillis();
    }
    return len == 0;
}

/** Credentials for proxy authentication */
struct ProxyCredentials
{
    std::string username;
    std::string password;
};

std::string Socks5ErrorString(uint8_t err)
{
    switch (err)
    {
    case SOCKS5Reply::GENFAILURE:
        return "general failure";
    case SOCKS5Reply::NOTALLOWED:
        return "connection not allowed";
    case SOCKS5Reply::NETUNREACHABLE:
        return "network unreachable";
    case SOCKS5Reply::HOSTUNREACHABLE:
        return "host unreachable";
    case SOCKS5Reply::CONNREFUSED:
        return "connection refused";
    case SOCKS5Reply::TTLEXPIRED:
        return "TTL expired";
    case SOCKS5Reply::CMDUNSUPPORTED:
        return "protocol error";
    case SOCKS5Reply::ATYPEUNSUPPORTED:
        return "address type not supported";
    default:
        return "unknown";
    }
}

/** Connect using SOCKS5 (as described in RFC1928) */
static bool Socks5(const std::string &strDest, int port, const ProxyCredentials *auth, SOCKET &hSocket)
{
    LOG(NET, "SOCKS5 connecting %s\n", strDest);
    if (strDest.size() > 255)
    {
        CloseSocket(hSocket);
        return error("Hostname too long");
    }
    // Accepted authentication methods
    std::vector<uint8_t> vSocks5Init;
    vSocks5Init.push_back(SOCKSVersion::SOCKS5);
    if (auth)
    {
        vSocks5Init.push_back(0x02); // # Number of methods
        vSocks5Init.push_back(SOCKS5Method::NOAUTH);
        vSocks5Init.push_back(SOCKS5Method::USER_PASS);
    }
    else
    {
        vSocks5Init.push_back(0x01); // # Number of methods
        vSocks5Init.push_back(SOCKS5Method::NOAUTH);
    }
    ssize_t ret = send(hSocket, (const char *)begin_ptr(vSocks5Init), vSocks5Init.size(), MSG_NOSIGNAL);
    if (ret != (ssize_t)vSocks5Init.size())
    {
        CloseSocket(hSocket);
        return error("Error sending to proxy");
    }
    uint8_t pchRet1[2];
    if (!InterruptibleRecv(pchRet1, 2, SOCKS5_RECV_TIMEOUT, hSocket))
    {
        CloseSocket(hSocket);
        LOGA("Socks5() connect to %s:%d failed: InterruptibleRecv() timeout or other failure\n", strDest, port);
        return false;
    }
    if (pchRet1[0] != SOCKSVersion::SOCKS5)
    {
        CloseSocket(hSocket);
        return error("Proxy failed to initialize");
    }
    if (pchRet1[1] == SOCKS5Method::USER_PASS && auth)
    {
        // Perform username/password authentication (as described in RFC1929)
        std::vector<uint8_t> vAuth;
        vAuth.push_back(0x01); // Current (and only) version of user/pass subnegotiation
        if (auth->username.size() > 255 || auth->password.size() > 255)
        {
            CloseSocket(hSocket);
            return error("Proxy username or password too long");
        }
        vAuth.push_back(auth->username.size());
        vAuth.insert(vAuth.end(), auth->username.begin(), auth->username.end());
        vAuth.push_back(auth->password.size());
        vAuth.insert(vAuth.end(), auth->password.begin(), auth->password.end());
        ret = send(hSocket, (const char *)begin_ptr(vAuth), vAuth.size(), MSG_NOSIGNAL);
        if (ret != (ssize_t)vAuth.size())
        {
            CloseSocket(hSocket);
            return error("Error sending authentication to proxy");
        }
        LOG(PROXY, "SOCKS5 sending proxy authentication %s:%s\n", auth->username, auth->password);
        uint8_t pchRetA[2];
        if (!InterruptibleRecv(pchRetA, 2, SOCKS5_RECV_TIMEOUT, hSocket))
        {
            CloseSocket(hSocket);
            return error("Error reading proxy authentication response");
        }
        if (pchRetA[0] != 0x01 || pchRetA[1] != 0x00)
        {
            CloseSocket(hSocket);
            return error("Proxy authentication unsuccessful");
        }
    }
    else if (pchRet1[1] == SOCKS5Method::NOAUTH)
    {
        // Perform no authentication
    }
    else
    {
        CloseSocket(hSocket);
        return error("Proxy requested wrong authentication method %02x", pchRet1[1]);
    }
    std::vector<uint8_t> vSocks5;
    vSocks5.push_back(SOCKSVersion::SOCKS5); // VER protocol version
    vSocks5.push_back(SOCKS5Command::CONNECT); // CMD CONNECT
    vSocks5.push_back(0x00); // RSV Reserved
    vSocks5.push_back(SOCKS5Atyp::DOMAINNAME); // ATYP DOMAINNAME
    vSocks5.push_back(strDest.size()); // Length<=255 is checked at beginning of function
    vSocks5.insert(vSocks5.end(), strDest.begin(), strDest.end());
    vSocks5.push_back((port >> 8) & 0xFF);
    vSocks5.push_back((port >> 0) & 0xFF);
    ret = send(hSocket, (const char *)begin_ptr(vSocks5), vSocks5.size(), MSG_NOSIGNAL);
    if (ret != (ssize_t)vSocks5.size())
    {
        CloseSocket(hSocket);
        return error("Error sending to proxy");
    }
    uint8_t pchRet2[4];
    if (!InterruptibleRecv(pchRet2, 4, SOCKS5_RECV_TIMEOUT, hSocket))
    {
        CloseSocket(hSocket);
        return error("Error reading proxy response");
    }
    if (pchRet2[0] != SOCKSVersion::SOCKS5)
    {
        CloseSocket(hSocket);
        return error("Proxy failed to accept request");
    }
    if (pchRet2[1] != SOCKS5Reply::SUCCEEDED)
    {
        // Failures to connect to a peer that are not proxy errors
        CloseSocket(hSocket);
        LOGA("Socks5() connect to %s:%d failed: %s\n", strDest, port, Socks5ErrorString(pchRet2[1]));
        return false;
    }
    if (pchRet2[2] != 0x00) // Reserved field must be 0
    {
        CloseSocket(hSocket);
        return error("Error: malformed proxy response");
    }
    uint8_t pchRet3[256];
    switch (pchRet2[3])
    {
    case SOCKS5Atyp::IPV4:
        ret = InterruptibleRecv(pchRet3, 4, SOCKS5_RECV_TIMEOUT, hSocket);
        break;
    case SOCKS5Atyp::IPV6:
        ret = InterruptibleRecv(pchRet3, 16, SOCKS5_RECV_TIMEOUT, hSocket);
        break;
    case SOCKS5Atyp::DOMAINNAME:
    {
        ret = InterruptibleRecv(pchRet3, 1, SOCKS5_RECV_TIMEOUT, hSocket);
        if (!ret)
        {
            CloseSocket(hSocket);
            return error("Error reading from proxy");
        }
        int nRecv = pchRet3[0];
        ret = InterruptibleRecv(pchRet3, nRecv, SOCKS5_RECV_TIMEOUT, hSocket);
        break;
    }
    default:
        CloseSocket(hSocket);
        return error("Error: malformed proxy response");
    }
    if (!ret)
    {
        CloseSocket(hSocket);
        return error("Error reading from proxy");
    }
    if (!InterruptibleRecv(pchRet3, 2, SOCKS5_RECV_TIMEOUT, hSocket))
    {
        CloseSocket(hSocket);
        return error("Error reading from proxy");
    }
    LOG(NET, "SOCKS5 connected %s\n", strDest);
    return true;
}

bool static ConnectSocketDirectly(const CService &addrConnect, SOCKET &hSocketRet, int nTimeout)
{
    hSocketRet = INVALID_SOCKET;

    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrConnect.GetSockAddr((struct sockaddr *)&sockaddr, &len))
    {
        LOGA("Cannot connect to %s: unsupported network\n", addrConnect.ToString());
        return false;
    }

    SOCKET hSocket = socket(((struct sockaddr *)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hSocket == INVALID_SOCKET)
        return false;

    int set = 1;
#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hSocket, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
#endif

// Disable Nagle's algorithm
#ifdef WIN32
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&set, sizeof(int));
#else
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void *)&set, sizeof(int));
#endif

    // Set to non-blocking
    if (!SetSocketNonBlocking(hSocket, true))
        return error("ConnectSocketDirectly: Setting socket to non-blocking failed, error %s\n",
            NetworkErrorString(WSAGetLastError()));

    if (connect(hSocket, (struct sockaddr *)&sockaddr, len) == SOCKET_ERROR)
    {
        int nErr = WSAGetLastError();
        // WSAEINVAL is here because some legacy version of winsock uses it
        if (nErr == WSAEINPROGRESS || nErr == WSAEWOULDBLOCK || nErr == WSAEINVAL)
        {
            struct timeval timeout = MillisToTimeval(nTimeout);
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(hSocket, &fdset);
            int nRet = select(hSocket + 1, nullptr, &fdset, nullptr, &timeout);
            if (nRet == 0)
            {
                LOG(NET, "connection to %s timeout\n", addrConnect.ToString());
                CloseSocket(hSocket);
                return false;
            }
            if (nRet == SOCKET_ERROR)
            {
                LOGA("select() for %s failed: %s\n", addrConnect.ToString(), NetworkErrorString(WSAGetLastError()));
                CloseSocket(hSocket);
                return false;
            }
            socklen_t nRetSize = sizeof(nRet);
#ifdef WIN32
            if (getsockopt(hSocket, SOL_SOCKET, SO_ERROR, (char *)(&nRet), &nRetSize) == SOCKET_ERROR)
#else
            if (getsockopt(hSocket, SOL_SOCKET, SO_ERROR, &nRet, &nRetSize) == SOCKET_ERROR)
#endif
            {
                LOG(NET, "getsockopt() for %s failed: %s\n", addrConnect.ToString(),
                    NetworkErrorString(WSAGetLastError()));
                CloseSocket(hSocket);
                return false;
            }
            if (nRet != 0)
            {
                LOG(NET, "connect() to %s failed after select(): %s\n", addrConnect.ToString(),
                    NetworkErrorString(nRet));
                CloseSocket(hSocket);
                return false;
            }
        }
#ifdef WIN32
        else if (WSAGetLastError() != WSAEISCONN)
#else
        else
#endif
        {
            LOGA("connect() to %s failed: %s\n", addrConnect.ToString(), NetworkErrorString(WSAGetLastError()));
            CloseSocket(hSocket);
            return false;
        }
    }

    hSocketRet = hSocket;
    return true;
}

bool SetProxy(enum Network net, const proxyType &addrProxy)
{
    assert(net >= 0 && net < NET_MAX);
    if (!addrProxy.IsValid())
        return false;
    LOCK(cs_proxyInfos);
    proxyInfo[net] = addrProxy;
    return true;
}

bool GetProxy(enum Network net, proxyType &proxyInfoOut)
{
    assert(net >= 0 && net < NET_MAX);
    LOCK(cs_proxyInfos);
    if (!proxyInfo[net].IsValid())
        return false;
    proxyInfoOut = proxyInfo[net];
    return true;
}

bool SetNameProxy(const proxyType &addrProxy)
{
    if (!addrProxy.IsValid())
        return false;
    LOCK(cs_proxyInfos);
    nameProxy = addrProxy;
    return true;
}

bool GetNameProxy(proxyType &nameProxyOut)
{
    LOCK(cs_proxyInfos);
    if (!nameProxy.IsValid())
        return false;
    nameProxyOut = nameProxy;
    return true;
}

bool HaveNameProxy()
{
    LOCK(cs_proxyInfos);
    return nameProxy.IsValid();
}

bool IsProxy(const CNetAddr &addr)
{
    LOCK(cs_proxyInfos);
    for (int i = 0; i < NET_MAX; i++)
    {
        if (addr == (CNetAddr)proxyInfo[i].proxy)
            return true;
    }
    return false;
}

static bool ConnectThroughProxy(const proxyType &proxy,
    const std::string &strDest,
    int port,
    SOCKET &hSocketRet,
    int nTimeout,
    bool *outProxyConnectionFailed)
{
    SOCKET hSocket = INVALID_SOCKET;
    // first connect to proxy server
    if (!ConnectSocketDirectly(proxy.proxy, hSocket, nTimeout))
    {
        if (outProxyConnectionFailed)
            *outProxyConnectionFailed = true;
        return false;
    }
    // do socks negotiation
    if (proxy.randomize_credentials)
    {
        FastRandomContext insecure_rand;
        ProxyCredentials random_auth;
        static std::atomic_int counter = {(int)insecure_rand.rand32()};
        random_auth.username = random_auth.password = strprintf("%i", counter++);
        if (!Socks5(strDest, (unsigned short)port, &random_auth, hSocket))
            return false;
    }
    else
    {
        if (!Socks5(strDest, (unsigned short)port, 0, hSocket))
            return false;
    }

    hSocketRet = hSocket;
    return true;
}

bool ConnectSocket(const CService &addrDest, SOCKET &hSocketRet, int nTimeout, bool *outProxyConnectionFailed)
{
    proxyType proxy;
    if (outProxyConnectionFailed)
        *outProxyConnectionFailed = false;

    if (GetProxy(addrDest.GetNetwork(), proxy))
        return ConnectThroughProxy(
            proxy, addrDest.ToStringIP(), addrDest.GetPort(), hSocketRet, nTimeout, outProxyConnectionFailed);
    else // no proxy needed (none set for target network)
        return ConnectSocketDirectly(addrDest, hSocketRet, nTimeout);
}

bool ConnectSocketByName(CService &addr,
    SOCKET &hSocketRet,
    const char *pszDest,
    int portDefault,
    int nTimeout,
    bool *outProxyConnectionFailed)
{
    std::string strDest;
    int port = portDefault;

    if (outProxyConnectionFailed)
        *outProxyConnectionFailed = false;

    SplitHostPort(std::string(pszDest), port, strDest);

    proxyType _nameProxy;
    GetNameProxy(_nameProxy);

    CService addrResolved;
    if (Lookup(strDest.c_str(), addrResolved, port, fNameLookup && !HaveNameProxy()))
    {
        if (addrResolved.IsValid())
        {
            addr = addrResolved;
            return ConnectSocket(addr, hSocketRet, nTimeout);
        }
    }

    addr = CService("0.0.0.0:0");

    if (!HaveNameProxy())
        return false;
    return ConnectThroughProxy(_nameProxy, strDest, port, hSocketRet, nTimeout, outProxyConnectionFailed);
}

#ifdef WIN32
std::string NetworkErrorString(int err)
{
    char buf[256];
    buf[0] = 0;
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), nullptr))
    {
        return strprintf("%s (%d)", buf, err);
    }
    else
    {
        return strprintf("Unknown error (%d)", err);
    }
}
#else
std::string NetworkErrorString(int err)
{
    char buf[256];
    const char *s = nullptr;
    buf[0] = 0;
/* Too bad there are two incompatible implementations of the
 * thread-safe strerror. */
#ifdef STRERROR_R_CHAR_P /* GNU variant can return a pointer outside the passed buffer */
    s = strerror_r(err, buf, sizeof(buf));
#else /* POSIX variant always returns message in buffer */
    if (strerror_r(err, buf, sizeof(buf)))
        buf[0] = 0;
    s = buf;
#endif
    return strprintf("%s (%d)", s, err);
}
#endif

bool CloseSocket(SOCKET &hSocket)
{
    if (hSocket == INVALID_SOCKET)
        return false;
#ifdef WIN32
    int ret = closesocket(hSocket);
#else
    int ret = close(hSocket);
#endif
    if (ret)
    {
        LOG(NET, "Socket close failed: %d. Error: %s\n", hSocket, NetworkErrorString(WSAGetLastError()));
    }
    hSocket = INVALID_SOCKET;
    return ret != SOCKET_ERROR;
}

bool SetSocketNonBlocking(SOCKET &hSocket, bool fNonBlocking)
{
    if (fNonBlocking)
    {
#ifdef WIN32
        u_long nOne = 1;
        if (ioctlsocket(hSocket, FIONBIO, &nOne) == SOCKET_ERROR)
        {
#else
        int fFlags = fcntl(hSocket, F_GETFL, 0);
        if (fcntl(hSocket, F_SETFL, fFlags | O_NONBLOCK) == SOCKET_ERROR)
        {
#endif
            CloseSocket(hSocket);
            return false;
        }
    }
    else
    {
#ifdef WIN32
        u_long nZero = 0;
        if (ioctlsocket(hSocket, FIONBIO, &nZero) == SOCKET_ERROR)
        {
#else
        int fFlags = fcntl(hSocket, F_GETFL, 0);
        if (fcntl(hSocket, F_SETFL, fFlags & ~O_NONBLOCK) == SOCKET_ERROR)
        {
#endif
            CloseSocket(hSocket);
            return false;
        }
    }

    return true;
}
