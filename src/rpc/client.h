// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_CLIENT_H
#define BITCOIN_RPC_CLIENT_H

#include "allowed_args.h"
#include <univalue.h>

// Exception thrown on connection error.  This error is used to determine
// when to wait if -rpcwait is given.
class CConnectionFailed : public std::runtime_error
{
public:
    explicit inline CConnectionFailed(const std::string &msg) : std::runtime_error(msg) {}
};

// helper function to call an RPC and get a response
UniValue CallRPC(const std::string &strMethod, const UniValue &params);

UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams);

// Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
// as well as objects and arrays.
UniValue ParseNonRFCJSONValue(const std::string &strVal);

#define CONTINUE_EXECUTION -1

// Initialize apps that use the bitcoin.conf configuration file and flags.
// This function returns either EXIT_FAILURE or EXIT_SUCCESS codes (stdlib.h) when it's expected to stop the process
// or CONTINUE_EXECUTION when it's expected to continue further.
int AppInitRPC(const std::string &usage, const AllowedArgs::AllowedArgs &allowedArgs, int argc, char *argv[]);

#endif // BITCOIN_RPC_CLIENT_H
