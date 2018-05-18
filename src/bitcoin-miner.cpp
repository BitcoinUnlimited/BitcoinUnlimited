// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "chainparamsbase.h"
#include "clientversion.h"
#include "fs.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"
#include "primitives/block.h"
#include "hash.h"
#include "streams.h"
#include "arith_uint256.h"

#include <boost/thread.hpp>

#include <stdio.h>
#include <cstdlib>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <univalue.h>

using namespace std;

// BU add lockstack stuff here for bitcoin-cli, because I need to carefully
// order it in globals.cpp for bitcoind and bitcoin-qt
boost::mutex dd_mutex;
std::map<std::pair<void *, void *>, LockStack> lockorders;
boost::thread_specific_ptr<LockStack> lockstack;

static const int CONTINUE_EXECUTION = -1;

static const int NEW_CANDIDATE_INTERVAL = 30; // seconds

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
bool static ScanHash(const CBlockHeader *pblock, uint32_t &nNonce, uint256 *phash)
{
    // Write the first 76 bytes of the block header to a double-SHA256 state.
    CHash256 hasher;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *pblock;
    assert(ss.size() == 80);
    hasher.Write((unsigned char *)&ss[0], 76);

    while (true)
    {
        nNonce++;

        // Write the last 4 bytes of the block header (the nonce) to a copy of
        // the double-SHA256 state, and compute the result.
        CHash256(hasher).Write((unsigned char *)&nNonce, 4).Finalize((unsigned char *)phash);

        // Return the nonce if the hash has at least some zero bits,
        // caller will check if it has enough to reach the target
        if (((uint16_t *)phash)[15] == 0)
            return true;

        // If nothing found after trying for a while, return -1
        if ((nNonce & 0xfff) == 0)
            return false;
    }
}


//////////////////////////////////////////////////////////////////////////////
//
// Start
//

//
// Exception thrown on connection error.  This error is used to determine
// when to wait if -rpcwait is given.
//
class CConnectionFailed : public std::runtime_error
{
public:
    explicit inline CConnectionFailed(const std::string &msg) : std::runtime_error(msg) {}
};

//
// This function returns either one of EXIT_ codes when it's expected to stop the process or
// CONTINUE_EXECUTION when it's expected to continue further.
//
static int AppInitRPC(int argc, char *argv[])
{
    //
    // Parameters
    //
    AllowedArgs::BitcoinCli allowedArgs;
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
        std::string strUsage =
            strprintf(_("%s RPC client version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n";
        if (!mapArgs.count("-version"))
        {
            strUsage += "\n" + _("Usage:") + "\n" + "  bitcoin-miner [options] " +
                        strprintf(_("Send command to %s"), _(PACKAGE_NAME)) + "\n" +
                        "  bitcoin-cli [options] help                " + _("List commands") + "\n" +
                        "  bitcoin-cli [options] help <command>      " + _("Get help for a command") + "\n";

            strUsage += "\n" + allowedArgs.helpMessage();
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


/** Reply structure for request_done to fill in */
struct HTTPReply
{
    int status;
    std::string body;
};

static void http_request_done(struct evhttp_request *req, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply *>(ctx);

    if (req == NULL)
    {
        /* If req is NULL, it means an error occurred while connecting, but
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
    struct evhttp_connection *evcon = evhttp_connection_base_new(base, NULL, host.c_str(), port); // TODO RAII
    if (evcon == NULL)
        throw runtime_error("create connection failed");
    evhttp_connection_set_timeout(evcon, GetArg("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT));

    HTTPReply response;
    struct evhttp_request *req = evhttp_request_new(http_request_done, (void *)&response); // TODO RAII
    if (req == NULL)
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

int CpuMiner(void)
{
    while (1)
    {
        UniValue reply;
        UniValue result;
        string strPrint;
        int nRet = 0;
        try
        {
            // Execute and handle connection failures with -rpcwait
            const bool fWait = true;
            do
            {
                try
                {
                    UniValue params;
                    reply = CallRPC("getstratum", params);

                    // Parse reply
                    result = find_value(reply, "result");
                    const UniValue &error = find_value(reply, "error");

                    if (!error.isNull())
                    {
                        // Error
                        int code = error["code"].get_int();
                        if (fWait && code == RPC_IN_WARMUP)
                            throw CConnectionFailed("server in warmup");
                        strPrint = "error: " + error.write();
                        nRet = abs(code);
                        if (error.isObject())
                        {
                            UniValue errCode = find_value(error, "code");
                            UniValue errMsg = find_value(error, "message");
                            strPrint = errCode.isNull() ? "" : "error code: " + errCode.getValStr() + "\n";

                            if (errMsg.isStr())
                                strPrint += "error message:\n" + errMsg.get_str();
                        }
                    }
                    else
                    {
                        // Result
                        if (result.isNull())
                            strPrint = "";
                        else if (result.isStr())
                            strPrint = result.get_str();
                        else
                            strPrint = result.write(2);
                    }
                    // Connection succeeded, no need to retry.
                    break;
                }
                catch (const CConnectionFailed &c)
                {
                    if (fWait)
                    {
                        printf("Warning: %s\n", c.what());
                        MilliSleep(1000);
                    }
                    else
                        throw;
                }
            } while (fWait);
        }
        catch (const boost::thread_interrupted &)
        {
            throw;
        }
        catch (const std::exception &e)
        {
            strPrint = string("error: ") + e.what();
            nRet = EXIT_FAILURE;
        }
        catch (...)
        {
            PrintExceptionContinue(NULL, "CommandLineRPC()");
            throw;
        }

        if (strPrint != "")
        {
            fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
            // Actually do some mining
            if (1)
            {
                CBlockHeader blkhdr;
                // TODO: Fill in blkhdr members from reply
                // nVersion
                // uint256 hashPrevBlock;
                // uint256 hashMerkleRoot;
                // uint32_t nTime;

                blkhdr.nBits = std::strtoul(result["nBits"].get_str().c_str(), nullptr, 16);
                // start somewhere random
                uint32_t nNonce = blkhdr.nNonce = std::rand();

                arith_uint256 hashTarget = arith_uint256().SetCompact(blkhdr.nBits);
                // Check if something found
                uint256 hash;
                int64_t start = GetTime();
                printf("searching...target: %s\n",hashTarget.GetHex().c_str());
                while (GetTime() < start + NEW_CANDIDATE_INTERVAL)
                {
                    if (ScanHash(&blkhdr, nNonce, &hash))
                    {
                        if (UintToArith256(hash) <= hashTarget)
                        {
                            // Found a solution
                            blkhdr.nNonce = nNonce;
                            printf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex().c_str(),
                                hashTarget.GetHex().c_str());

                            // TODO submit the block header via RPC
                            break;
                        }
                    }
                }
            }
        }
    }
}


int main(int argc, char *argv[])
{
    SetupEnvironment();
    if (!SetupNetworking())
    {
        fprintf(stderr, "Error: Initializing networking failed\n");
        exit(1);
    }

    try
    {
        int ret = AppInitRPC(argc, argv);
        if (ret != CONTINUE_EXECUTION)
            return ret;
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "AppInitRPC()");
        return EXIT_FAILURE;
    }
    catch (...)
    {
        PrintExceptionContinue(NULL, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try
    {
        ret = CpuMiner();
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CommandLineRPC()");
    }
    catch (...)
    {
        PrintExceptionContinue(NULL, "CommandLineRPC()");
    }
    return ret;
}
