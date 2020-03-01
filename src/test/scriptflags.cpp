// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2018 The Bitcoin SV developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/scriptflags.h"

#include "script/interpreter.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/test/unit_test.hpp>

#include <map>
#include <vector>

// clang-format off
static std::map<std::string, uint32_t> mapFlagNames =
{
    {"NONE", SCRIPT_VERIFY_NONE},
    {"P2SH", SCRIPT_VERIFY_P2SH},
    {"STRICTENC", SCRIPT_VERIFY_STRICTENC},
    {"DERSIG", SCRIPT_VERIFY_DERSIG},
    {"LOW_S", SCRIPT_VERIFY_LOW_S},
    {"SIGPUSHONLY", SCRIPT_VERIFY_SIGPUSHONLY},
    {"MINIMALDATA", SCRIPT_VERIFY_MINIMALDATA},
    {"DISCOURAGE_UPGRADABLE_NOPS", SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS},
    {"CLEANSTACK", SCRIPT_VERIFY_CLEANSTACK},
    {"CHECKLOCKTIMEVERIFY", SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY},
    {"CHECKSEQUENCEVERIFY", SCRIPT_VERIFY_CHECKSEQUENCEVERIFY},
    {"MINIMALIF", SCRIPT_VERIFY_MINIMALIF},
    {"NULLFAIL", SCRIPT_VERIFY_NULLFAIL},
    {"COMPRESSED_PUBKEYTYPE", SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE},
    {"SIGHASH_FORKID", SCRIPT_ENABLE_SIGHASH_FORKID},
    {"REPLAY_PROTECTION", SCRIPT_ENABLE_REPLAY_PROTECTION},
    {"CHECKDATASIG", SCRIPT_ENABLE_CHECKDATASIG},
    {"DISALLOW_SEGWIT_RECOVERY", SCRIPT_DISALLOW_SEGWIT_RECOVERY},
    {"SCHNORR_MULTISIG", SCRIPT_ENABLE_SCHNORR_MULTISIG},
    {"REVERSEBYTES", SCRIPT_ENABLE_OP_REVERSEBYTES},
};
// clang-format on

uint32_t ParseScriptFlags(std::string strFlags)
{
    if (strFlags.empty())
    {
        return 0;
    }

    uint32_t flags = 0;
    std::vector<std::string> words;
    boost::algorithm::split(words, strFlags, boost::algorithm::is_any_of(","));

    for (std::string &word : words)
    {
        if (!mapFlagNames.count(word))
            BOOST_ERROR("Bad test: unknown verification flag '" << word << "'");
        flags |= mapFlagNames[word];
    }

    return flags;
}

std::string FormatScriptFlags(uint32_t flags)
{
    if (flags == 0)
    {
        return "";
    }

    std::string ret;
    std::map<std::string, uint32_t>::const_iterator it = mapFlagNames.begin();
    uint32_t unused = flags;
    while (it != mapFlagNames.end())
    {
        if (flags & it->second)
        {
            ret += it->first + ",";
            unused &= ~it->second;
        }
        it++;
    }

    if (unused)
    {
        BOOST_ERROR("mapFlagNames needs updating: verification flag has no string mapping '0x" << std::hex << unused << "'");
    }

    return ret.substr(0, ret.size() - 1);
}
