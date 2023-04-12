// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dstencode.h"
#include "base58.h"
#include "cashaddrenc.h"
#include "chainparams.h"
#include "config.h"
#include "script/standard.h"

std::string EncodeDestination(const CTxDestination &dst,
    const CChainParams &params,
    const Config &cfg,
    bool tokenAwareAddress)
{
    if (cfg.UseCashAddrEncoding())
    {
        return EncodeCashAddr(dst, params, tokenAwareAddress);
    }
    if (tokenAwareAddress)
    {
        throw std::runtime_error("Legacy addresses don't support token-awareness");
    }
    return EncodeLegacyAddr(dst, params);
}

CTxDestination DecodeDestination(const std::string &addr, const CChainParams &params, bool *tokenAwareAddressOut)
{
    CTxDestination dst = DecodeCashAddr(addr, params, tokenAwareAddressOut);
    if (IsValidDestination(dst))
    {
        return dst;
    }
    if (tokenAwareAddressOut)
        *tokenAwareAddressOut = false; // legacy is never a token-aware address
    return DecodeLegacyAddr(addr, params);
}

bool IsValidDestinationString(const std::string &addr, const CChainParams &params, bool *tokenAwareAddressOut)
{
    return IsValidDestination(DecodeDestination(addr, params, tokenAwareAddressOut));
}

std::string EncodeDestination(const CTxDestination &dst, bool tokenAwareAddress)
{
    return EncodeDestination(dst, Params(), GetConfig(), tokenAwareAddress);
}
CTxDestination DecodeDestination(const std::string &addr, bool *tokenAwareAddressOut)
{
    return DecodeDestination(addr, Params(), tokenAwareAddressOut);
}
bool IsValidDestinationString(const std::string &addr, bool *tokenAwareAddressOut)
{
    return IsValidDestinationString(addr, Params(), tokenAwareAddressOut);
}
