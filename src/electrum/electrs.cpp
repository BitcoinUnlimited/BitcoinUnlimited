// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "electrum/electrs.h"
#include "netaddress.h"
#include "util.h"
#include "utilhttp.h"
#include "utilprocess.h"
#include "xversionkeys.h"
#include "xversionmessage.h"

#include <map>
#include <regex>
#include <sstream>

#include <boost/filesystem.hpp>

constexpr char ELECTRSCASH_BIN[] = "electrscash";

static std::string monitoring_port() { return GetArg("-electrum.monitoring.port", "4224"); }
static std::string monitoring_host() { return GetArg("-electrum.monitoring.host", "127.0.0.1"); }
static std::string rpc_host() { return GetArg("-electrum.host", "127.0.0.1"); }
static std::string rpc_port(const std::string &network)
{
    std::map<std::string, std::string> portmap = {{"main", "50001"}, {"test", "60001"}, {"regtest", "60401"}};

    auto defaultPort = portmap.find(network);
    if (defaultPort == end(portmap))
    {
        std::stringstream ss;
        ss << "Electrum server does not support '" << network << "' network.";
        throw std::invalid_argument(ss.str());
    }

    return GetArg("-electrum.port", defaultPort->second);
}

static bool is_electrum_server_public()
{
    const auto host = rpc_host();

    // Special case, CNetAddr treats "0.0.0.0" as local, but electrs
    // treats it as listen on all IPs.
    if (host == "0.0.0.0")
    {
        return true;
    }

    // Assume the server is public if it's not listening on localhost and
    // not listening on a private network (RFC1918)
    const CNetAddr listenaddr(host);

    return !listenaddr.IsLocal() && !listenaddr.IsRFC1918();
}

static void remove_conflicting_arg(std::vector<std::string> &args, const std::string &override_arg)
{
    // special case: verboseness argument
    const std::regex verbose("^-v+$");
    if (std::regex_search(override_arg, verbose))
    {
        auto it = begin(args);
        while (it != end(args))
        {
            if (!std::regex_search(*it, verbose))
            {
                ++it;
                continue;
            }
            LOGA("Electrum: Argument '%s' overrides '%s'", override_arg, *it);
            it = args.erase(it);
        }
        return;
    }

    // normal case
    auto separator = override_arg.find_first_of("=");
    if (separator == std::string::npos)
    {
        // switch flag, for example "--disable-full-compaction".
        auto it = begin(args);
        while (it != end(args))
        {
            if (*it != override_arg)
            {
                ++it;
                continue;
            }
            // Remove duplicate.
            it = args.erase(it);
        }
        return;
    }
    separator++; // include '=' when matching argument names below

    auto it = begin(args);
    while (it != end(args))
    {
        if (it->size() < separator)
        {
            ++it;
            continue;
        }
        if (it->substr(0, separator) != override_arg.substr(0, separator))
        {
            ++it;
            continue;
        }
        LOGA("Electrum: Argument '%s' overrides '%s'", override_arg, *it);
        it = args.erase(it);
    }
}
namespace electrum
{
std::string electrs_path()
{
    // look for electrs in same path as bitcoind
    boost::filesystem::path bitcoind_dir(this_process_path());
    bitcoind_dir = bitcoind_dir.remove_filename();

    auto default_path = bitcoind_dir / ELECTRSCASH_BIN;
    const std::string path = GetArg("-electrum.exec", default_path.string());

    if (path.empty())
    {
        throw std::runtime_error("Path to electrum server executable not found. "
                                 "You can specify full path with -electrum.exec");
    }
    if (!boost::filesystem::exists(path))
    {
        std::stringstream ss;
        ss << "Cannot find electrum executable at " << path;
        throw std::runtime_error(ss.str());
    }
    return path;
}

//! Arguments to start electrs server with
std::vector<std::string> electrs_args(int rpcport, const std::string &network)
{
    std::vector<std::string> args;

    if (Logging::LogAcceptCategory(ELECTRUM))
    {
        // increase verboseness when electrum logging is enabled
        args.push_back("-vvvv");
    }

    // address to bitcoind rpc interface
    {
        rpcport = GetArg("-rpcport", rpcport);
        std::stringstream ss;
        ss << "--daemon-rpc-addr=" << GetArg("-electrum.daemon.host", "127.0.0.1") << ":" << rpcport;
        args.push_back(ss.str());
    }

    args.push_back("--electrum-rpc-addr=" + rpc_host() + ":" + rpc_port(network));

    // bitcoind data dir (for cookie file)
    args.push_back("--daemon-dir=" + GetDataDir(false).string());

    // Use rpc interface instead of attempting to parse *blk files
    args.push_back("--jsonrpc-import");

    // Where to store electrs database files.
    const std::string defaultDir = (GetDataDir() / ELECTRSCASH_BIN).string();
    args.push_back("--db-dir=" + GetArg("-electrum.dir", defaultDir));

    // Tell electrs what network we're on
    const std::map<std::string, std::string> netmapping = {
        {"main", "bitcoin"}, {"test", "testnet"}, {"regtest", "regtest"}};
    if (!netmapping.count(network))
    {
        std::stringstream ss;
        ss << "Electrum server does not support '" << network << "' network.";
        throw std::invalid_argument(ss.str());
    }
    args.push_back("--network=" + netmapping.at(network));
    args.push_back("--monitoring-addr=" + monitoring_host() + ":" + monitoring_port());

    if (!GetArg("-rpcpassword", "").empty())
    {
        args.push_back("--cookie=" + GetArg("-rpcuser", "") + ":" + GetArg("-rpcpassword", ""));
    }

    // max txs to look up per address
    args.push_back("--txid-limit=" + GetArg("-electrum.addr.limit", "500"));

    for (auto &a : mapMultiArgs["-electrum.rawarg"])
    {
        remove_conflicting_arg(args, a);
        args.push_back(a);
    }

    return args;
}

std::map<std::string, int64_t> fetch_electrs_info()
{
    if (!GetBoolArg("-electrum", false))
    {
        throw std::runtime_error("Electrum server is disabled");
    }

    std::stringstream infostream = http_get(monitoring_host(), std::stoi(monitoring_port()), "/");

    const std::regex keyval("^([a-z_{}=\"\\+]+)\\s(\\d+)\\s*$");
    std::map<std::string, int64_t> info;
    std::string line;
    std::smatch match;
    while (std::getline(infostream, line, '\n'))
    {
        if (!std::regex_match(line, match, keyval))
        {
            continue;
        }
        try
        {
            info[match[1].str()] = std::stol(match[2].str());
        }
        catch (const std::exception &e)
        {
            LOG(ELECTRUM, "%s error: %s", __func__, e.what());
        }
    }
    return info;
}

void set_xversion_flags(CXVersionMessage &xver, const std::string &network)
{
    if (!GetBoolArg("-electrum", false))
    {
        return;
    }
    if (!is_electrum_server_public())
    {
        return;
    }

    constexpr double ELECTRUM_PROTOCOL_VERSION = 1.4;

    xver.set_u64c(XVer::BU_ELECTRUM_SERVER_PORT_TCP, std::stoul(rpc_port(network)));
    xver.set_u64c(
        XVer::BU_ELECTRUM_SERVER_PROTOCOL_VERSION, static_cast<uint64_t>(ELECTRUM_PROTOCOL_VERSION * 1000000));
}
} // ns electrum
