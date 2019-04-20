// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "deltablocks.h"
#include "util.h"
#include "utilstrencodings.h"
#include "sync.h"
#include <stdint.h>
#include <boost/assign/list_of.hpp>
#include <univalue.h>

using namespace std;

extern CCriticalSection cs_db;

UniValue getDeltaInfo(const ConstCDeltaBlockRef& dbr) {
    LOCK(cs_db);
    UniValue res(UniValue::VOBJ);
    res.pushKV("blockhash", dbr->GetHash().GetHex());

    // this should always be true!
    res.pushKV("all_txn_known", dbr->allTransactionsKnown());
    UniValue ancestors(UniValue::VARR);
    for (auto anc : dbr->ancestors())
        ancestors.push_back(anc->GetHash().GetHex());
    res.pushKV("ancestors", ancestors);
    res.pushKV("delta_size", dbr->deltaSet().size());
    res.pushKV("full_size", dbr->numTransactions());
    res.pushKV("wpow", dbr->weakPOW());
    return res;
}

UniValue deltainfo(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() < 1)
        throw runtime_error("deltainfo \"hash\"\n");     // FIXME: docs
    uint256 hash(uint256S(params[0].get_str()));
    ConstCDeltaBlockRef dbr = CDeltaBlock::byHash(hash);
    LOG(WB, "Delta info for hash: %s, delta_ref=%p\n", hash.GetHex(), dbr);
    if (dbr != nullptr)
        return getDeltaInfo(dbr);
    else return NullUniValue;
}

UniValue deltalist(const UniValue &params, bool fHelp) {
    if (fHelp) throw std::runtime_error("deltalist\n"); // FIXME: docs
    LOCK(cs_db);
    std::map<uint256, std::vector<ConstCDeltaBlockRef> > all_known = CDeltaBlock::knownInReceiveOrder();
    UniValue res(UniValue::VOBJ);

    for (auto pair : all_known) {
        const uint256& stronghash = pair.first;
        const std::vector<ConstCDeltaBlockRef>& deltablocks = pair.second;
        LOG(WB, "Listing delta blocks, %d delta blocks for strong hash %s.\n",
            deltablocks.size(), stronghash.GetHex());
        UniValue entry(UniValue::VARR);
        for (auto db : deltablocks)
            entry.push_back(getDeltaInfo(db));
        res.pushKV(stronghash.GetHex(), entry);
    }
    return res;
}

UniValue deltatips(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() < 1) throw std::runtime_error("deltatips stronghash\n"); // FIXME: docs
    uint256 stronghash(uint256S(params[0].get_str()));
    LOCK(cs_db);
    std::vector<ConstCDeltaBlockRef> tips = CDeltaBlock::tips(stronghash);
    UniValue res(UniValue::VARR);
    for (auto tip : tips)
        res.push_back(getDeltaInfo(tip));
    return res;
}


static const CRPCCommand commands[] = {
    //  category              name                      actor (function)         okSafeMode
    //  --------------------- ------------------------  -----------------------  ----------
    {"delta", "deltalist", &deltalist, true},
    {"delta", "deltainfo", &deltainfo, true},
    {"delta", "deltatips", &deltatips, true}
};

void RegisterDeltaRPCCommands(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}
