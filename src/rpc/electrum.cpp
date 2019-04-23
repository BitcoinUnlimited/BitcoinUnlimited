// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "electrum/electrumrpcinfo.h"
#include "rpc/server.h"
#include <univalue.h>

UniValue getelectruminfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        electrum::ElectrumRPCInfo::ThrowHelp();
    }
    return electrum::ElectrumRPCInfo().GetElectrumInfo();
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
