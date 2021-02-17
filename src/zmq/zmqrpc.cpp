// Copyright (c) 2018 The Bitcoin Core developers
// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <zmq/zmqrpc.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <zmq/zmqabstractnotifier.h>
#include <zmq/zmqnotificationinterface.h>

#include <univalue.h>

namespace {

UniValue getzmqnotifications(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() != 0) {
        throw std::runtime_error(
            "getzmqnotifications\n"
            "\nReturns information about the active ZeroMQ notifications.\n"
            "\nResult:\n"
            "[\n"
            "  {                        (json object)\n"
            "    \"type\": \"pubhashtx\",   (string) Type of notification\n"
            "    \"address\": \"...\"       (string) Address of the publisher\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getzmqnotifications", "") +
            HelpExampleRpc("getzmqnotifications", ""));
    }

    UniValue result(UniValue::VARR);
    if (pzmqNotificationInterface != nullptr) {
        auto notifiers = pzmqNotificationInterface->GetActiveNotifiers();
        for (const auto *n : notifiers) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("type", n->GetType());
            obj.pushKV("address", n->GetAddress());
            result.push_back(obj);
        }
    }

    return result;
}

// clang-format off
static const CRPCCommand commands[] = {
    //  category          name                     actor (function)        argNames
    //  ----------------- ------------------------ ----------------------- ----------
    { "zmq",            "getzmqnotifications",   getzmqnotifications,    {} },
};
// clang-format on

} // anonymous namespace

void RegisterZMQRPCCommands(CRPCTable &t) {
    for (const auto &c : commands) {
        t.appendCommand(c);
    }
}
