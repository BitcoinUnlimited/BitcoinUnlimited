#include "electrum/electrs.h"
#include "util.h"
#include "utilhttp.h"
#include "utilprocess.h"

#include <map>
#include <regex>
#include <sstream>

#include <boost/filesystem.hpp>

static std::string monitoring_port() { return GetArg("-electrummonitoringport", "4224"); }
namespace electrum
{
std::string electrs_path()
{
    // look for electrs in same path as bitcoind
    boost::filesystem::path bitcoind_dir(this_process_path());
    bitcoind_dir = bitcoind_dir.remove_filename();

    auto default_path = bitcoind_dir / "electrs";
    const std::string path = GetArg("-electrumexec", default_path.string());

    if (path.empty())
    {
        throw std::runtime_error("Path to electrum server executable not found. "
                                 "You can specify full path with -electrumexec");
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
        ss << "--daemon-rpc-addr=localhost:" << rpcport;
        args.push_back(ss.str());
    }

    const std::string electrumport = GetArg("-electrumport", "DEFAULT");
    if (electrumport != "DEFAULT")
    {
        args.push_back("--electrum-rpc-addr=127.0.0.1:" + electrumport);
    }

    // bitcoind data dir (for cookie file)
    args.push_back("--daemon-dir=" + GetDataDir(false).string());

    // Use rpc interface instead of attempting to parse *blk files
    args.push_back("--jsonrpc-import");

    // Where to store electrs database files.
    const std::string defaultDir = (GetDataDir() / "electrs").string();
    args.push_back("--db-dir=" + GetArg("-electrumdir", defaultDir));

    // Tell electrs what network we're on
    const std::map<std::string, std::string> netmapping = {
        {"main", "mainnet"}, {"test", "testnet"}, {"regtest", "regtest"}};
    if (!netmapping.count(network))
    {
        std::stringstream ss;
        ss << "Electrum server does not support '" << network << "' network.";
        throw std::invalid_argument(ss.str());
    }
    args.push_back("--network=" + netmapping.at(network));
    args.push_back("--monitoring-addr=127.0.0.1:" + monitoring_port());

    if (!GetArg("-rpcpassword", "").empty())
    {
        args.push_back("--cookie=" + GetArg("-rpcuser", "") + ":" + GetArg("-rpcpassword", ""));
    }

    // max txs to look up per address
    args.push_back("--txid-limit=500");

    return args;
}

std::map<std::string, int> fetch_electrs_info()
{
    if (!GetBoolArg("-electrum", false))
    {
        throw std::runtime_error("Electrum server is disabled");
    }

    std::stringstream infostream = http_get("127.0.0.1", std::stoi(monitoring_port()), "/");

    const std::regex keyval("^([a-z_]+)\\s(\\d+)\\s*$");
    std::map<std::string, int> info;
    std::string line;
    std::smatch match;
    while (std::getline(infostream, line, '\n'))
    {
        if (!std::regex_match(line, match, keyval))
        {
            continue;
        }
        info[match[1].str()] = std::stoi(match[2].str());
    }
    return info;
}

} // ns electrum
