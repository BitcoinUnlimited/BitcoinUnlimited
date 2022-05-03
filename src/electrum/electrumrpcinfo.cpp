// Copyright (c) 2019-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "electrum/electrumrpcinfo.h"
#include "electrum/electrumserver.h"
#include "electrum/rostrum.h"
#include "main.h" // chainActive
#include "rpc/server.h" // HelpExampleCli
#include "unlimited.h" // IsInitialBlockDownload
#include <cstdint>

namespace electrum
{
static int64_t get_index_height(const std::map<std::string, int64_t> &info)
{
    auto height_it = info.find(INDEX_HEIGHT_KEY);
    if (height_it == end(info))
    {
        return -1;
    }
    return height_it->second;
}

UniValue ElectrumRPCInfo::GetElectrumInfo() const
{
    std::map<std::string, int64_t> rostruminfo;
    try
    {
        rostruminfo = FetchRostrumInfo();
    }
    catch (const std::runtime_error &e)
    {
        LOGA("Electrum: %s: Failed to fetch electrs info %s", __func__, e.what());
    }
    int64_t index_height = get_index_height(rostruminfo);

    UniValue info(UniValue::VOBJ);
    info.pushKV("status", GetStatus(index_height));
    info.pushKV("index_progress", GetIndexingProgress(index_height));
    info.pushKV("index_height", index_height);

    UniValue debuginfo(UniValue::VOBJ);
    for (auto &kv : rostruminfo)
    {
        if (kv.first == INDEX_HEIGHT_KEY)
        {
            continue;
        }
        debuginfo.pushKV(kv.first, kv.second);
    }
    info.pushKV("debuginfo", debuginfo);
    return info;
}

void ElectrumRPCInfo::ThrowHelp()
{
    throw std::invalid_argument("getelectruminfo\n"
                                "Returns the status of the integrated electrum server.\n"
                                "\nResult:\n"
                                "{ (json object)\n"
                                "    \"status\" (string) status description\n"
                                "    \"index_height\" (numeric) block height of last indexed block\n"
                                "    \"index_progress\" (numeric) index progress as percentage\n"
                                "    \"debug\" (json object)\n"
                                "    {\n"
                                "      ... debug information, subject to change"
                                "    }\n"
                                "}\n" +
                                HelpExampleCli("getelectruminfo", "") + HelpExampleRpc("getelectruminfo", ""));
}

int ElectrumRPCInfo::ActiveTipHeight() const { return chainActive.Height(); }
bool ElectrumRPCInfo::IsInitialBlockDownload() const { return ::IsInitialBlockDownload(); }
bool ElectrumRPCInfo::IsRunning() const { return ElectrumServer::Instance().IsRunning(); }
std::map<std::string, int64_t> ElectrumRPCInfo::FetchRostrumInfo() const { return fetch_rostrum_info(); }
std::string ElectrumRPCInfo::GetStatus(int64_t index_height) const
{
    if (!this->IsRunning())
    {
        return "stopped";
    }

    if (this->IsInitialBlockDownload())
    {
        return "waiting for initial block download";
    }

    if (index_height == -1)
    {
        return "initializing";
    }

    if (index_height < ActiveTipHeight())
    {
        return "indexing";
    }
    return "ok";
}

double ElectrumRPCInfo::GetIndexingProgress(int64_t index_height) const
{
    if (index_height == -1)
    {
        return 0.;
    }
    int tip_height = ActiveTipHeight();
    if (tip_height == -1)
    {
        return 0.;
    }
    return (double(index_height) / double(tip_height)) * 100.;
}

} // namespace electrum
