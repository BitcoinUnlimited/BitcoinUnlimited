// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
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


#include <stdio.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <univalue.h>

#ifdef DEBUG_LOCKORDER
std::atomic<bool> lockdataDestructed{false};
LockData lockdata;
#endif

using namespace std;

int CommandLineRPC(int argc, char *argv[])
{
    string strPrint;
    int nRet = 0;
    try
    {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0]))
        {
            argc--;
            argv++;
        }
        std::vector<std::string> args = std::vector<std::string>(&argv[1], &argv[argc]);
        if (GetBoolArg("-stdin", false))
        {
            // Read one arg per line from stdin and append
            std::string line;
            while (std::getline(std::cin, line))
                args.push_back(line);
        }
        if (args.size() < 1)
            throw runtime_error("too few parameters (need at least command)");
        std::string strMethod = args[0];
        UniValue params(UniValue::VARR);
        for (unsigned int idx = 1; idx < args.size(); idx++)
        {
            const std::string &strVal = args[idx];
            params.push_back(strVal);
        }

        // Execute and handle connection failures with -rpcwait
        const bool fWait = GetBoolArg("-rpcwait", false);
        do
        {
            try
            {
                const UniValue reply = CallRPC(strMethod, params);

                // Parse reply
                const UniValue &result = find_value(reply, "result");
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
            catch (const CConnectionFailed &)
            {
                if (fWait)
                    MilliSleep(1000);
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
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
        throw;
    }

    if (strPrint != "")
    {
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
    }
    return nRet;
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
        std::string appname("bitcoin-cli");
        std::string usage = "\n" + _("Usage:") + "\n" + "  " + appname + " [options] " +
                            strprintf(_("Send command to %s"), _(PACKAGE_NAME)) + "\n" + "  " + appname +
                            " [options] help                " + _("List commands") + "\n" + "  " + appname +
                            " [options] help <command>      " + _("Get help for a command") + "\n";

        int ret = AppInitRPC(usage, AllowedArgs::BitcoinCli(), argc, argv);
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
        PrintExceptionContinue(nullptr, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try
    {
        ret = CommandLineRPC(argc, argv);
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CommandLineRPC()");
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
    }
    return ret;
}
