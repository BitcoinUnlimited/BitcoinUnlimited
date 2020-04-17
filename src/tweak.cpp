// Copyright (c) 2016-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tweak.h"
#include "main.h"
#include "net.h"
#include "rpc/server.h"
#include "util.h"

#include <boost/lexical_cast.hpp>
#include <iomanip>
#include <thread>

using namespace std;

void LoadTweaks()
{
    for (CTweakMap::iterator it = tweaks.begin(); it != tweaks.end(); ++it)
    {
        std::string name("-");
        name.append(it->second->GetName());
        std::string result = GetArg(name.c_str(), "");
        if (result.size())
        {
            it->second->Set(UniValue(result));
        }
    }
}

// RPC Get a particular tweak
UniValue gettweak(const UniValue &params, bool fHelp)
{
    if (fHelp)
    {
        throw runtime_error("get"
                            "\nReturns the value of a configuration setting\n"
                            "\nArguments: configuration setting name\n"
                            "\nResult:\n"
                            "  {\n"
                            "    \"setting name\" : value of the setting\n"
                            "    ...\n"
                            "  }\n"
                            "\nExamples:\n" +
                            HelpExampleCli("get a b", "") + HelpExampleRpc("get a b", ""));
    }

    UniValue ret(UniValue::VOBJ);
    bool help = false;
    unsigned int psize = params.size();

    if (psize == 0) // No arguments should return all tweaks
    {
        for (CTweakMap::iterator item = tweaks.begin(); item != tweaks.end(); ++item)
        {
            ret.pushKV(item->second->GetName(), item->second->Get());
        }
    }

    for (unsigned int i = 0; i < psize; i++)
    {
        bool fMatch = false;
        string name = params[i].get_str();
        if (name == "help")
        {
            help = true;
            continue;
        }
        // always match any beginning part of string to be
        // compatible with old implementation of gettweak(..)
        std::string match_str = (name[name.size() - 1] == '*') ? name : name + "*";

        for (CTweakMap::iterator item = tweaks.begin(); item != tweaks.end(); ++item)
        {
            if (wildmatch(match_str, item->first))
            {
                if (help)
                    ret.pushKV(item->second->GetName(), item->second->GetHelp());
                else
                    ret.pushKV(item->second->GetName(), item->second->Get());

                fMatch = true;
            }
        }
        if (!fMatch)
        {
            std::string error = "No tweak available for " + name;
            throw std::invalid_argument(error.c_str());
        }
    }
    if (ret.empty())
        throw std::invalid_argument("No tweak available for that selection");

    return ret;
}
// RPC Set a particular tweak
UniValue settweak(const UniValue &params, bool fHelp)
{
    if (fHelp)
    {
        throw runtime_error(
            "set"
            "\nSets the value of a configuration option.  Parameters must be of the format name=value, with no spaces "
            "(use name=\"the value\" for strings)\n"
            "\nArguments: <configuration setting name>=<value> <configuration setting name2>=<value2>...\n"
            "\nResult:\n"
            "nothing or error string\n"
            "\nExamples:\n" +
            HelpExampleCli("set a 5", "") + HelpExampleRpc("get a b", ""));
    }

    std::string result;
    // First validate all the parameters that are being set
    for (unsigned int i = 0; i < params.size(); i++)
    {
        string s = params[i].get_str();
        size_t split = s.find("=");
        if (split == std::string::npos)
        {
            throw runtime_error("Invalid assignment format, missing =");
        }
        std::string name = s.substr(0, split);
        std::string value = s.substr(split + 1);

        CTweakMap::iterator item = tweaks.find(name);
        if (item != tweaks.end())
        {
            std::string ret = item->second->Validate(value);
            if (!ret.empty())
            {
                result.append(ret);
                result.append("\n");
            }
        }
    }
    if (!result.empty()) // If there were any validation failures, give up
    {
        throw runtime_error(result);
    }

    // Now assign
    UniValue ret(UniValue::VARR);
    UniValue names(UniValue::VARR);
    for (unsigned int i = 0; i < params.size(); i++)
    {
        string s = params[i].get_str();
        size_t split = s.find("=");
        if (split == std::string::npos)
        {
            throw runtime_error("Invalid assignment format, missing =");
        }
        std::string name = s.substr(0, split);
        std::string value = s.substr(split + 1);

        CTweakMap::iterator item = tweaks.find(name);
        if (item != tweaks.end())
        {
            UniValue tmp = item->second->Set(value);
            if (!tmp.isNull())
            {
                ret.push_back(tmp);
            }

            names.push_back(name);
        }
        else
        {
            std::string error = "No tweak available for " + name;
            throw std::invalid_argument(error.c_str());
        }
    }
    if (!ret.empty())
    {
        return ret;
    }
    return gettweak(names, false);
}
