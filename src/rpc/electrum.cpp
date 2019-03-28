// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "electrum/electrs.h"
#include "rpc/server.h"
#include <univalue.h>

UniValue getelectruminfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw std::invalid_argument("getelectruminfo\n"
                                    "\nReturns status of the electrum server"
                                    "\nExamples:\n" +
                                    HelpExampleCli("getblockcount", "") + HelpExampleRpc("getblockcount", ""));
    }

    UniValue info(UniValue::VOBJ);
    for (auto &kv : electrum::fetch_electrs_info())
    {
        info.pushKV(kv.first, kv.second);
    }
    return info;
}

static const CRPCCommand commands[] = {
    //  category, name, function, okSafeMode
    {"electrum", "getelectruminfo", &getelectruminfo, true},
};

void RegisterElectrumRPC(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}
