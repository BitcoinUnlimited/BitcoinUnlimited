// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"

#include "base58.h"
#include "client.h"
#include "fs.h"
#include "init.h"
#include "random.h"
#include "sync.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"

#include "unlimited.h"

#include <univalue.h>

#include <boost/algorithm/string/case_conv.hpp> // for to_upper()
#include <boost/bind/bind.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/signals2/signal.hpp>
#include <thread>

using namespace RPCServer;
using namespace std;

static bool fRPCRunning = false;
static bool fRPCInWarmup = true;
static std::string rpcWarmupStatus("RPC server started");
extern CCriticalSection cs_rpcWarmup;
/* Timer-creating functions */
static RPCTimerInterface *timerInterface = nullptr;
/* Map of name to timer.
 * @note Can be changed to std::unique_ptr when C++11 */
static std::map<std::string, boost::shared_ptr<RPCTimerBase> > deadlineTimers;

static struct CRPCSignals
{
    boost::signals2::signal<void()> Started;
    boost::signals2::signal<void()> Stopped;
    boost::signals2::signal<void(const CRPCCommand &)> PreCommand;
} g_rpcSignals;

void RPCServer::OnStarted(boost::function<void()> slot) { g_rpcSignals.Started.connect(slot); }
void RPCServer::OnStopped(boost::function<void()> slot) { g_rpcSignals.Stopped.connect(slot); }
void RPCServer::OnPreCommand(boost::function<void(const CRPCCommand &)> slot)
{
    g_rpcSignals.PreCommand.connect(boost::bind(slot, boost::placeholders::_1));
}

class CRPCConvertParam
{
public:
    std::string methodName; //! method whose params want conversion
    int paramIdx; //! 0-based idx of param to convert
};

/* clang-format off */
static const CRPCConvertParam vRPCConvertParams[] =
{
    {"stop", 0},
    {"setmocktime", 0},
    {"getaddednodeinfo", 0},
    {"setgenerate", 0},
    {"setgenerate", 1},
    {"generate", 0},
    {"generate", 1},
    {"generatetoaddress", 0},
    {"generatetoaddress", 2},
    {"getnetworkhashps", 0},
    {"getnetworkhashps", 1},
    {"sendtoaddress", 1},
    {"sendtoaddress", 4},
    {"settxfee", 0},
    {"getreceivedbyaddress", 1},
    {"getreceivedbyaccount", 1},
    {"listreceivedbyaddress", 0},
    {"listreceivedbyaddress", 1},
    {"listreceivedbyaddress", 2},
    {"listreceivedbyaccount", 0},
    {"listreceivedbyaccount", 1},
    {"listreceivedbyaccount", 2},
    {"getbalance", 1},
    {"getbalance", 2},
    {"getblockhash", 0},
    {"move", 2},
    {"move", 3},
    {"sendfrom", 2},
    {"sendfrom", 3},
    {"listtransactions", 1},
    {"listtransactions", 2},
    {"listtransactions", 3},
    {"listtransactionsfrom", 1},
    {"listtransactionsfrom", 2},
    {"listtransactionsfrom", 3},
    {"listaccounts", 0},
    {"listaccounts", 1},
    {"walletpassphrase", 1},
    {"getblocktemplate", 0},
    {"getminingcandidate", 0},
    {"submitminingsolution", 0},
    {"listsinceblock", 1},
    {"listsinceblock", 2},
    {"sendmany", 1},
    {"sendmany", 2},
    {"sendmany", 4},
    {"addmultisigaddress", 0},
    {"addmultisigaddress", 1},
    {"createmultisig", 0},
    {"createmultisig", 1},
    {"listunspent", 0},
    {"listunspent", 1},
    {"listunspent", 2},
    {"getblock", 1},
    {"getblock", 2},
    {"getblockheader", 1},
    { "getchaintxstats", 0},
    {"gettransaction", 1},
    {"getrawtransaction", 1},
    {"createrawtransaction", 0},
    {"createrawtransaction", 1},
    {"createrawtransaction", 2},
    {"signrawtransaction", 1},
    {"signrawtransaction", 2},
    {"sendrawtransaction", 1},
    {"validaterawtransaction", 1},
    {"fundrawtransaction", 1},
    {"gettxout", 1},
    {"gettxout", 2},
    {"gettxoutproof", 0},
    {"lockunspent", 0},
    {"lockunspent", 1},
    {"importprivkey", 2},
    {"importaddress", 2},
    {"importaddress", 3},
    {"importpubkey", 2},
    {"verifychain", 0},
    {"verifychain", 1},
    {"keypoolrefill", 0},
    {"getrawmempool", 0},
    {"getraworphanpool", 0},
    {"estimatefee", 0},
    {"estimatesmartfee", 0},
    {"prioritisetransaction", 1},
    {"prioritisetransaction", 2},
    {"setban", 2},
    {"setban", 3},
    {"rollbackchain", 0},
    {"rollbackchain", 1},
    {"reconsidermostworkchain", 0},
    {"reconsidermostworkchain", 1},
    {"getmempoolancestors", 1},
    {"getmempooldescendants", 1},
    {"getrawtransactionssince", 1},
    {"getblockstats", 1}
};
/* clang-format on */

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int> > members;

    struct CompareMethod
    {
    private:
        const std::string val_;

    public:
        CompareMethod(const std::string &val) : val_(val) {}
        bool operator()(const std::pair<std::string, int> &entry) const { return val_ == entry.first; }
    };

public:
    CRPCConvertTable();

    bool convert(const std::string &method, int idx) { return (members.count(std::make_pair(method, idx)) > 0); }
    bool hasMethod(const std::string &method)
    {
        return std::find_if(members.begin(), members.end(), CompareMethod(method)) != members.end();
    }
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

void RPCTypeCheck(const UniValue &params, const list<UniValue::VType> &typesExpected, bool fAllowNull)
{
    unsigned int i = 0;
    for (const UniValue::VType &t : typesExpected)
    {
        if (params.size() <= i)
            break;

        const UniValue &v = params[i];
        if (!((v.type() == t) || (fAllowNull && (v.isNull()))))
        {
            string err = strprintf("Expected type %s, got %s", uvTypeName(t), uvTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
        i++;
    }
}

void RPCTypeCheckObj(const UniValue &o,
    const std::map<std::string, UniValueType> &typesExpected,
    bool fAllowNull,
    bool fStrict)
{
    for (const auto &t : typesExpected)
    {
        const UniValue &v = find_value(o, t.first);
        if (!fAllowNull && v.isNull())
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", t.first));

        if (!(t.second.typeAny || (v.type() == t.second.type) || (fAllowNull && (v.isNull()))))
        {
            string err =
                strprintf("Expected type %s for %s, got %s", uvTypeName(t.second.type), t.first, uvTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }

        if (fStrict)
        {
            for (const std::string &k : o.getKeys())
            {
                if (typesExpected.count(k) == 0)
                {
                    string err = strprintf("Unexpected keys %s", k);
                    throw JSONRPCError(RPC_TYPE_ERROR, err);
                }
            }
        }
    }
}

CAmount AmountFromValue(const UniValue &value)
{
    if (!value.isNum() && !value.isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount is not a number or string");
    CAmount amount;
    if (!ParseFixedPoint(value.getValStr(), 8, &amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    if (!MoneyRange(amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount out of range");
    return amount;
}

UniValue ValueFromAmount(const CAmount &amount)
{
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    return UniValue(UniValue::VNUM, strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder));
}

uint256 ParseHashV(const UniValue &v, string strName)
{
    string strHex;
    if (v.isStr())
        strHex = v.get_str();
    if (!IsHex(strHex)) // Note: IsHex("") is false
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName + " must be hexadecimal string (not '" + strHex + "')");
    if (64 != strHex.length())
        throw JSONRPCError(
            RPC_INVALID_PARAMETER, strprintf("%s must be of length %d (not %d)", strName, 64, strHex.length()));
    uint256 result;
    result.SetHex(strHex);
    return result;
}
uint256 ParseHashO(const UniValue &o, string strKey) { return ParseHashV(find_value(o, strKey), strKey); }
vector<unsigned char> ParseHexV(const UniValue &v, string strName)
{
    string strHex;
    if (v.isStr())
        strHex = v.get_str();
    if (!IsHex(strHex))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName + " must be hexadecimal string (not '" + strHex + "')");
    return ParseHex(strHex);
}
vector<unsigned char> ParseHexO(const UniValue &o, string strKey) { return ParseHexV(find_value(o, strKey), strKey); }
/**
 * Note: This interface may still be subject to change.
 */

std::string CRPCTable::help(const std::string &strCommand) const
{
    string strRet;
    string category;
    set<rpcfn_type> setDone;
    typedef pair<string, CRPCCommand> rpc_pair;
    vector<rpc_pair> vCommands;

    for (rpc_pair p : mapCommands)
        // sort by command category first, then by command name
        vCommands.push_back(rpc_pair(p.second.category + p.first, p.second));
    sort(vCommands.begin(), vCommands.end(), [](rpc_pair a, rpc_pair b) { return a.first < b.first; });
    for (rpc_pair command : vCommands)
    {
        const CRPCCommand *pcmd = &(command.second);
        string strMethod = pcmd->name;
        // We already filter duplicates, but these deprecated screw up the sort order
        if (strMethod.find("label") != string::npos)
            continue;
        if ((strCommand != "" || pcmd->category == "hidden") && strMethod != strCommand)
            continue;
        try
        {
            UniValue params;
            rpcfn_type pfn = pcmd->actor;
            if (setDone.insert(pfn).second)
                (*pfn)(params, true);
        }
        catch (const std::exception &e)
        {
            // Help text is returned in an exception
            string strHelp = string(e.what());
            if (strCommand == "")
            {
                if (strHelp.find('\n') != string::npos)
                    strHelp = strHelp.substr(0, strHelp.find('\n'));

                if (category != pcmd->category)
                {
                    if (!category.empty())
                        strRet += "\n";
                    category = pcmd->category;
                    string firstLetter = category.substr(0, 1);
                    boost::to_upper(firstLetter);
                    strRet += "== " + firstLetter + category.substr(1) + " ==\n";
                }
            }
            strRet += strHelp + "\n";
        }
    }
    if (strRet == "")
        strRet = strprintf("help: unknown command: %s\n", strCommand);
    strRet = strRet.substr(0, strRet.size() - 1);
    return strRet;
}

UniValue help(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error("help ( \"command\" )\n"
                            "\nList all commands, or get help for a specified command.\n"
                            "\nArguments:\n"
                            "1. \"command\"     (string, optional) The command to get help on\n"
                            "\nResult:\n"
                            "\"text\"     (string) The help text\n");

    string strCommand;
    if (params.size() > 0)
        strCommand = params[0].get_str();

    return tableRPC.help(strCommand);
}


UniValue stop(const UniValue &params, bool fHelp)
{
    // Accept the deprecated and ignored 'detach' boolean argument
    if (fHelp || params.size() > 1)
        throw runtime_error("stop\n"
                            "\nStop Bitcoin server.");
    // Event loop will exit after current HTTP requests have been handled, so
    // this reply will get back to the client.
    StartShutdown();
    return "Bitcoin server stopping";
}

UniValue uptime(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw std::runtime_error("uptime\n"
                                 "\nReturns the total uptime of the server.\n"
                                 "\nResult:\n"
                                 "ttt        (numeric) The number of seconds that the server has been running\n"
                                 "\nExamples:\n" +
                                 HelpExampleCli("uptime", "") + HelpExampleRpc("uptime", ""));
    }
    return GetTime() - GetStartupTime();
}


/**
 * Call Table
 */
static const CRPCCommand vRPCCommands[] = {
    //  category              name                      actor (function)         okSafeMode
    //  --------------------- ------------------------  -----------------------  ----------
    /* Overall control/query calls */
    {"control", "help", &help, true}, {"control", "stop", &stop, true}, {"control", "uptime", &uptime, true}};

CRPCTable::CRPCTable()
{
    for (auto cmd : vRPCCommands)
        appendCommand(cmd);
}

const CRPCCommand *CRPCTable::operator[](const std::string &name) const
{
    map<string, CRPCCommand>::const_iterator it = mapCommands.find(name);
    if (it == mapCommands.end())
        return nullptr;
    return &(it->second);
}

bool CRPCTable::appendCommand(const CRPCCommand &cmd)
{
    if (IsRPCRunning())
        return false;

    // don't allow overwriting for now
    map<string, CRPCCommand>::const_iterator it = mapCommands.find(cmd.name);
    if (it != mapCommands.end())
        return false;

    mapCommands[cmd.name] = cmd;
    return true;
}

bool StartRPC()
{
    LOG(RPC, "Starting RPC\n");
    fRPCRunning = true;
    g_rpcSignals.Started();
    return true;
}

void InterruptRPC()
{
    LOG(RPC, "Interrupting RPC\n");
    // Interrupt e.g. running longpolls
    fRPCRunning = false;
}

void StopRPC()
{
    LOG(RPC, "Stopping RPC\n");
    deadlineTimers.clear();
    g_rpcSignals.Stopped();
}

bool IsRPCRunning() { return fRPCRunning; }
void SetRPCWarmupStatus(const std::string &newStatus)
{
    LOCK(cs_rpcWarmup);
    rpcWarmupStatus = newStatus;
}

void SetRPCWarmupFinished()
{
    LOCK(cs_rpcWarmup);
    assert(fRPCInWarmup);
    fRPCInWarmup = false;
}

bool RPCIsInWarmup(std::string *outStatus)
{
    LOCK(cs_rpcWarmup);
    if (outStatus)
        *outStatus = rpcWarmupStatus;
    return fRPCInWarmup;
}

void JSONRequest::parse(const UniValue &valRequest)
{
    // Parse request
    if (!valRequest.isObject())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
    const UniValue &request = valRequest.get_obj();

    // Parse id now so errors from here on will have the id
    id = find_value(request, "id");

    // Parse method
    UniValue valMethod = find_value(request, "method");
    if (valMethod.isNull())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
    if (!valMethod.isStr())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
    strMethod = valMethod.get_str();
    if (strMethod != "getblocktemplate")
        LOG(RPC, "ThreadRPCServer method=%s\n", SanitizeString(strMethod));

    // Parse params
    UniValue valParams = find_value(request, "params");
    if (valParams.isArray())
        params = valParams.get_array();
    else if (valParams.isNull())
        params = UniValue(UniValue::VARR);
    else
        throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
}

static UniValue JSONRPCExecOne(const UniValue &req)
{
    UniValue rpc_result(UniValue::VOBJ);

    JSONRequest jreq;
    try
    {
        jreq.parse(req);

        UniValue result = tableRPC.execute(jreq.strMethod, jreq.params);
        rpc_result = JSONRPCReplyObj(result, NullUniValue, jreq.id);
    }
    catch (const UniValue &objError)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue, objError, jreq.id);
    }
    catch (const std::exception &e)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue, JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
    }

    return rpc_result;
}

std::string JSONRPCExecBatch(const UniValue &vReq)
{
    UniValue ret(UniValue::VARR);
    for (unsigned int reqIdx = 0; reqIdx < vReq.size(); reqIdx++)
        ret.push_back(JSONRPCExecOne(vReq[reqIdx]));

    return ret.write() + "\n";
}

UniValue CRPCTable::execute(const std::string &strMethod, const UniValue &preparams) const
{
    // Return immediately if in warmup
    {
        LOCK(cs_rpcWarmup);
        if (fRPCInWarmup)
            throw JSONRPCError(RPC_IN_WARMUP, rpcWarmupStatus);
    }
    UniValue params(UniValue::VARR);
    if (rpcCvtTable.hasMethod(strMethod))
    {
        bool needsConvert = true;
        // check if any of the params are not strings
        for (size_t i = 0; i < preparams.size(); i++)
        {
            if (preparams[i].isStr() != true)
            {
                needsConvert = false;
                params = preparams;
                break;
            }
        }
        if (needsConvert)
        {
            std::vector<std::string> vParams;
            for (size_t i = 0; i < preparams.size(); i++)
            {
                vParams.push_back(preparams[i].get_str());
            }
            params = RPCConvertValues(strMethod, std::vector<std::string>(vParams.begin(), vParams.end()));
        }
    }
    else
    {
        params = preparams;
    }

    // Find method
    const CRPCCommand *pcmd = tableRPC[strMethod];
    if (!pcmd)
    {
        std::stringstream ss;
        ss << "Method '" << strMethod << "' not found";
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, ss.str());
    }

    g_rpcSignals.PreCommand(*pcmd);

    UniValue result;
    try
    {
        // Execute
        result = pcmd->actor(params, false);
    }
    catch (const std::exception &e)
    {
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    }
    return result;
}

std::vector<std::string> CRPCTable::listCommands() const
{
    std::vector<std::string> commandList;
    typedef std::map<std::string, CRPCCommand> commandMap;

    std::transform(mapCommands.begin(), mapCommands.end(), std::back_inserter(commandList),
        boost::bind(&commandMap::value_type::first, boost::placeholders::_1));
    return commandList;
}

std::string HelpExampleCli(const std::string &methodname, const std::string &args)
{
    return "> bitcoin-cli " + methodname + " " + args + "\n";
}

std::string HelpExampleRpc(const std::string &methodname, const std::string &args)
{
    return "> curl --user myusername --data-binary '{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", "
           "\"method\": \"" +
           methodname + "\", \"params\": [" + args + "] }' -H 'content-type: text/plain;' http://127.0.0.1:8332/\n";
}

void RPCSetTimerInterfaceIfUnset(RPCTimerInterface *iface)
{
    if (!timerInterface)
        timerInterface = iface;
}

void RPCSetTimerInterface(RPCTimerInterface *iface) { timerInterface = iface; }
void RPCUnsetTimerInterface(RPCTimerInterface *iface)
{
    if (timerInterface == iface)
        timerInterface = nullptr;
}

void RPCRunLater(const std::string &name, boost::function<void(void)> func, int64_t nSeconds)
{
    if (!timerInterface)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No timer handler registered for RPC");
    deadlineTimers.erase(name);
    LOG(RPC, "queue run of timer %s in %i seconds (using %s)\n", name, nSeconds, timerInterface->Name());
    deadlineTimers.insert(
        std::make_pair(name, boost::shared_ptr<RPCTimerBase>(timerInterface->NewTimer(func, nSeconds * 1000))));
}

CRPCTable tableRPC;
