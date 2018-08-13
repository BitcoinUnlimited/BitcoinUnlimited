// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "weakblock.h"
#include "rpc/server.h"
#include "utilstrencodings.h"
#include "connmgr.h"
#include <univalue.h>

using namespace std;

UniValue weakstats(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "weakstats\n"
            "\nReturns various high level weak block statistics.\n");

    UniValue result(UniValue::VOBJ);

    LOCK(cs_weakblocks);
    result.push_back(Pair("weakblocksknown", (uint64_t)weakstore.size()));
    result.push_back(Pair("weakchaintips", (uint64_t)weakstore.chainTips().size()));

    CWeakblockRef tip = weakstore.Tip();
    if (tip == nullptr) {
        result.push_back(Pair("weakchainheight", -1));
    } else {
        result.push_back(Pair("weakchainheight", tip->GetWeakHeight()));
        result.push_back(Pair("weakchaintiphash",  tip->GetHash().GetHex()));
        result.push_back(Pair("weakchaintipnumtx", (uint64_t)tip->vtx.size()));
    }
    return result;
}

UniValue weakchaintips(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "weakchaintips\n"
            "\nGives back the current weak chain tips as pairs of (weak block hash, weak chain height), in chronological order\n");

    const std::vector<CWeakblockRef>& tips = weakstore.chainTips();
    UniValue result(UniValue::VARR);

    for (auto wb : tips) {
        UniValue entry(UniValue::VARR);
        entry.push_back(wb->GetHash().GetHex());
        entry.push_back(wb->GetWeakHeight());
        result.push_back(entry);
    }
    return result;
}

UniValue weakconfirmations(const UniValue &params, bool fHelp) {
    // FIXME: This is currently slow. Needs a proper index. */
    if (fHelp || params.size() < 1)
        throw runtime_error(
            "weakconfirmations \"hexstring\"\n"
            "\nReturns the depth the given transaction can be found in the current weak block chain tip.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the TXID\n"
            "\nResult:\n"
            "\"num\"             (int) The number of weak block confirmations\n");


    std::string txid_hex = params[0].get_str();
    uint256 hash = ParseHashV(params[0], "parameter 1");

    CWeakblockRef pblock = weakstore.Tip();
    int confs = 0;

    while (pblock != nullptr) {
        bool found =false;
        for (auto tx : pblock->vtx) {
            if (tx->GetHash() == hash) {
                found = true;
                break;
            }
        }
        if (found) confs++;
        else break;
        pblock = weakstore.parent(pblock->GetHash());
    }
    return confs;
}

UniValue weaknodeknowledge(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "weaknodeknowledge\n"
            "\nReturns the nodes knowing about each weak block\n");

    LOCK(cs_weakblocks);

    UniValue result(UniValue::VARR);
    const  std::unordered_map<uint256, std::unordered_set<NodeId>, BlockHasher>& node_knowledge = weakstore.nodeKnowledge();

    for (auto p : node_knowledge) {
        UniValue entry(UniValue::VARR);
        entry.push_back(p.first.GetHex());

        UniValue nodes(UniValue::VARR);
        for (auto node_id : p.second) {
            CNodeRef node = connmgr->FindNodeFromId(node_id);
            if (bool(node)) {
                std::string n = node->GetLogName();
                nodes.push_back(n);
            }
        }
        entry.push_back(nodes);
        result.push_back(entry);
    }
    return result;
}


// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "weakblocks",         "weakstats",              &weakstats,              true  },
    { "weakblocks",         "weakchaintips",          &weakchaintips,          true  },
    { "weakblocks",         "weakconfirmations",      &weakconfirmations,      true  },
    { "weakblocks",         "weaknodeknowledge",      &weaknodeknowledge,      true  }
};
// clang-format on

void RegisterWeakBlockRPCCommands(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}
