// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_CASHADDRENC_H
#define BITCOIN_CASHADDRENC_H

#include "script/standard.h"

#include <string>
#include <vector>

class CChainParams;

enum CashAddrType : uint8_t
{
    PUBKEY_TYPE = 0,
    SCRIPT_TYPE = 1,
    TOKEN_PUBKEY_TYPE = 2, //< Token-Aware P2PKH
    TOKEN_SCRIPT_TYPE = 3, //< Token-Aware P2SH
};

struct CashAddrContent
{
    CashAddrType type{};
    std::vector<uint8_t> hash;

    bool IsNull() const { return hash.empty(); }
    bool IsTokenAwareType() const { return type == TOKEN_PUBKEY_TYPE || type == TOKEN_SCRIPT_TYPE; }
};

std::string EncodeCashAddr(const CTxDestination &, const CChainParams &, bool tokenAwareType = false);
std::string EncodeCashAddr(const std::string &prefix, const CashAddrContent &content);


CTxDestination DecodeCashAddr(const std::string &addr, const CChainParams &params, bool *tokenAwareTypeOut = nullptr);
CashAddrContent DecodeCashAddrContent(const std::string &addr, const CChainParams &params);
CashAddrContent DecodeCashAddrContent(const std::string &addr, const std::string &prefix);
CTxDestination DecodeCashAddrDestination(const CashAddrContent &content);

std::vector<uint8_t> PackCashAddrContent(const CashAddrContent &content);
#endif
