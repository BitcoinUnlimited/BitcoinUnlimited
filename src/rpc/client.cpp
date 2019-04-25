// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/client.h"
#include "chainparamsbase.h"
#include "clientversion.h"
#include "rpc/protocol.h"
#include "util.h"
#include "utilstrencodings.h"

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <set>
#include <stdint.h>

#include <univalue.h>

using namespace std;

class CRPCConvertParam
{
public:
    std::string methodName; //! method whose params want conversion
    int paramIdx; //! 0-based idx of param to convert
};

static const CRPCConvertParam vRPCConvertParams[] = {{"stop", 0}, {"setmocktime", 0}, {"getaddednodeinfo", 0},
    {"setgenerate", 0}, {"setgenerate", 1}, {"generate", 0}, {"generate", 1}, {"generatetoaddress", 0},
    {"generatetoaddress", 2}, {"getnetworkhashps", 0}, {"getnetworkhashps", 1}, {"sendtoaddress", 1},
    {"sendtoaddress", 4}, {"settxfee", 0}, {"getreceivedbyaddress", 1}, {"getreceivedbyaccount", 1},
    {"listreceivedbyaddress", 0}, {"listreceivedbyaddress", 1}, {"listreceivedbyaddress", 2},
    {"listreceivedbyaccount", 0}, {"listreceivedbyaccount", 1}, {"listreceivedbyaccount", 2}, {"getbalance", 1},
    {"getbalance", 2}, {"getblockhash", 0}, {"move", 2}, {"move", 3}, {"sendfrom", 2}, {"sendfrom", 3},
    {"listtransactions", 1}, {"listtransactions", 2}, {"listtransactions", 3}, {"listtransactionsfrom", 1},
    {"listtransactionsfrom", 2}, {"listtransactionsfrom", 3}, {"listaccounts", 0}, {"listaccounts", 1},
    {"walletpassphrase", 1}, {"getblocktemplate", 0}, {"getminingcandidate", 0}, {"submitminingsolution", 0},
    {"listsinceblock", 1}, {"listsinceblock", 2}, {"sendmany", 1}, {"sendmany", 2}, {"sendmany", 4},
    {"addmultisigaddress", 0}, {"addmultisigaddress", 1}, {"createmultisig", 0}, {"createmultisig", 1},
    {"listunspent", 0}, {"listunspent", 1}, {"listunspent", 2}, {"getblock", 1}, {"getblock", 2}, {"getblockheader", 1},
    {"gettransaction", 1}, {"getrawtransaction", 1}, {"createrawtransaction", 0}, {"createrawtransaction", 1},
    {"createrawtransaction", 2}, {"signrawtransaction", 1}, {"signrawtransaction", 2}, {"sendrawtransaction", 1},
    {"fundrawtransaction", 1}, {"gettxout", 1}, {"gettxout", 2}, {"gettxoutproof", 0}, {"lockunspent", 0},
    {"lockunspent", 1}, {"importprivkey", 2}, {"importaddress", 2}, {"importaddress", 3}, {"importpubkey", 2},
    {"verifychain", 0}, {"verifychain", 1}, {"keypoolrefill", 0}, {"getrawmempool", 0}, {"getraworphanpool", 0},
    {"estimatefee", 0}, {"estimatepriority", 0}, {"estimatesmartfee", 0}, {"estimatesmartpriority", 0},
    {"prioritisetransaction", 1}, {"prioritisetransaction", 2}, {"setban", 2}, {"setban", 3}, {"rollbackchain", 0},
    {"rollbackchain", 1}, {"reconsidermostworkchain", 0}, {"reconsidermostworkchain", 1}, {"getmempoolancestors", 1},
    {"getmempooldescendants", 1}, {"getrawtransactionssince", 1}};

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int> > members;

public:
    CRPCConvertTable();

    bool convert(const std::string &method, int idx) { return (members.count(std::make_pair(method, idx)) > 0); }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem = (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++)
    {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName, vRPCConvertParams[i].paramIdx));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string &strVal)
{
    UniValue jVal;
    if (!jVal.read(std::string("[") + strVal + std::string("]")) || !jVal.isArray() || jVal.size() != 1)
        throw runtime_error(string("Error parsing JSON:") + strVal);
    return jVal[0];
}

/** Convert strings to command-specific RPC representation */
UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++)
    {
        const std::string &strVal = strParams[idx];

        if (!rpcCvtTable.convert(strMethod, idx))
        {
            // insert string value directly
            params.push_back(strVal);
        }
        else
        {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}

/** Reply structure for request_done to fill in */
struct HTTPReply
{
    int status;
    std::string body;
};

void static http_request_done(struct evhttp_request *req, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply *>(ctx);

    if (req == nullptr)
    {
        /* If req is nullptr, it means an error occurred while connecting, but
         * I'm not sure how to find out which one. We also don't really care.
         */
        reply->status = 0;
        return;
    }

    reply->status = evhttp_request_get_response_code(req);

    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    if (buf)
    {
        size_t size = evbuffer_get_length(buf);
        const char *data = (const char *)evbuffer_pullup(buf, size);
        if (data)
            reply->body = std::string(data, size);
        evbuffer_drain(buf, size);
    }
}


UniValue CallRPC(const string &strMethod, const UniValue &params)
{
    std::string host = GetArg("-rpcconnect", DEFAULT_RPCCONNECT);
    int port = GetArg("-rpcport", BaseParams().RPCPort());

    // Create event base
    struct event_base *base = event_base_new(); // TODO RAII
    if (!base)
        throw runtime_error("cannot create event_base");

    // Synchronously look up hostname
    struct evhttp_connection *evcon = evhttp_connection_base_new(base, nullptr, host.c_str(), port); // TODO RAII
    if (evcon == nullptr)
        throw runtime_error("create connection failed");
    evhttp_connection_set_timeout(evcon, GetArg("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT));

    HTTPReply response;
    struct evhttp_request *req = evhttp_request_new(http_request_done, (void *)&response); // TODO RAII
    if (req == nullptr)
        throw runtime_error("create http request failed");

    // Get credentials
    std::string strRPCUserColonPass;
    if (mapArgs["-rpcpassword"] == "")
    {
        // Try fall back to cookie-based authentication if no password is provided
        if (!GetAuthCookie(&strRPCUserColonPass))
        {
            throw runtime_error(strprintf(_("Could not locate RPC credentials. No authentication cookie could be "
                                            "found, and no rpcpassword is set in the configuration file (%s)"),
                GetConfigFile(GetArg("-conf", BITCOIN_CONF_FILENAME)).string().c_str()));
        }
    }
    else
    {
        strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    }

    struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req);
    assert(output_headers);
    evhttp_add_header(output_headers, "Host", host.c_str());
    evhttp_add_header(output_headers, "Connection", "close");
    evhttp_add_header(
        output_headers, "Authorization", (std::string("Basic ") + EncodeBase64(strRPCUserColonPass)).c_str());

    // Attach request data
    std::string strRequest = JSONRPCRequest(strMethod, params, 1);
    struct evbuffer *output_buffer = evhttp_request_get_output_buffer(req);
    assert(output_buffer);
    evbuffer_add(output_buffer, strRequest.data(), strRequest.size());

    int r = evhttp_make_request(evcon, req, EVHTTP_REQ_POST, "/");
    if (r != 0)
    {
        evhttp_connection_free(evcon);
        event_base_free(base);
        throw CConnectionFailed("send http request failed");
    }

    event_base_dispatch(base);
    evhttp_connection_free(evcon);
    event_base_free(base);

    if (response.status == 0)
        throw CConnectionFailed("couldn't connect to server");
    else if (response.status == HTTP_UNAUTHORIZED)
        throw runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (response.status >= 400 && response.status != HTTP_BAD_REQUEST && response.status != HTTP_NOT_FOUND &&
             response.status != HTTP_INTERNAL_SERVER_ERROR)
        throw runtime_error(strprintf("server returned HTTP error %d", response.status));
    else if (response.body.empty())
        throw runtime_error("no response from server");

    // Parse reply
    UniValue valReply(UniValue::VSTR);
    if (!valReply.read(response.body))
        throw runtime_error("couldn't parse reply from server");
    const UniValue &reply = valReply.get_obj();
    if (reply.empty())
        throw runtime_error("expected reply to have result, error and id properties");

    return reply;
}


// This function returns either one of EXIT_ codes when it's expected to stop the process or
// CONTINUE_EXECUTION when it's expected to continue further.
int AppInitRPC(const std::string &usage, const AllowedArgs::AllowedArgs &allowedArgs, int argc, char *argv[])
{
    try
    {
        ParseParameters(argc, argv, allowedArgs);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error parsing program options: %s\n", e.what());
        return EXIT_FAILURE;
    }
    if (mapArgs.count("-?") || mapArgs.count("-h") || mapArgs.count("-help") || mapArgs.count("-version"))
    {
        std::string strUsage = strprintf(_("%s "), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n";
        if (!mapArgs.count("-version"))
        {
            strUsage += usage + "\n" + allowedArgs.helpMessage();
        }

        fprintf(stdout, "%s", strUsage.c_str());
        if (argc < 2)
        {
            fprintf(stderr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    if (!fs::is_directory(GetDataDir(false)))
    {
        fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", GetArg("-datadir", "").c_str());
        return EXIT_FAILURE;
    }
    try
    {
        ReadConfigFile(mapArgs, mapMultiArgs, allowedArgs);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error reading configuration file: %s\n", e.what());
        return EXIT_FAILURE;
    }
    // Check for -testnet or -regtest parameter (BaseParams() calls are only valid after this clause)
    try
    {
        SelectBaseParams(ChainNameFromCommandLine());
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }

    return CONTINUE_EXECUTION;
}
