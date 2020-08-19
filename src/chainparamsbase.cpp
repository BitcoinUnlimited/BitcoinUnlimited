// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparamsbase.h"

#include "tinyformat.h"
#include "util.h"

#include <assert.h>

const std::string CBaseChainParams::MAIN = "main";
const std::string CBaseChainParams::UNL = "nol";
const std::string CBaseChainParams::TESTNET = "test";
const std::string CBaseChainParams::TESTNET4 = "test4";
const std::string CBaseChainParams::REGTEST = "regtest";

/**
 * Main network
 */
class CBaseMainParams : public CBaseChainParams
{
public:
    CBaseMainParams() { nRPCPort = 8332; }
};
static CBaseMainParams mainParams;

/**
 * Unl network
 */
class CBaseUnlParams : public CBaseChainParams
{
public:
    CBaseUnlParams()
    {
        nRPCPort = 9332;
        strDataDir = "nol";
    }
};
static CBaseUnlParams unlParams;

/**
 * Testnet (v3)
 */
class CBaseTestNetParams : public CBaseChainParams
{
public:
    CBaseTestNetParams()
    {
        nRPCPort = 18332;
        strDataDir = "testnet3";
    }
};
static CBaseTestNetParams testNetParams;

class CBaseTestNet4Params : public CBaseChainParams
{
public:
    CBaseTestNet4Params()
    {
        nRPCPort = 28333;
        strDataDir = "testnet4";
    }
};
static CBaseTestNet4Params testNet4Params;

/*
 * Regression test
 */
class CBaseRegTestParams : public CBaseChainParams
{
public:
    CBaseRegTestParams()
    {
        nRPCPort = 18332;
        strDataDir = "regtest";
    }
};
static CBaseRegTestParams regTestParams;

static CBaseChainParams *pCurrentBaseParams = 0;

const CBaseChainParams &BaseParams()
{
    assert(pCurrentBaseParams);
    return *pCurrentBaseParams;
}

CBaseChainParams &BaseParams(const std::string &chain)
{
    if (chain == CBaseChainParams::MAIN)
        return mainParams;
    else if (chain == CBaseChainParams::UNL)
        return unlParams;
    else if (chain == CBaseChainParams::TESTNET)
        return testNetParams;
    else if (chain == CBaseChainParams::TESTNET4)
        return testNet4Params;
    else if (chain == CBaseChainParams::REGTEST)
        return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectBaseParams(const std::string &chain) { pCurrentBaseParams = &BaseParams(chain); }
std::string ChainNameFromCommandLine()
{
    uint64_t num_selected = 0;
    bool fRegTest = GetBoolArg("-regtest", false);
    num_selected += fRegTest;
    bool fTestNet = GetBoolArg("-testnet", false);
    num_selected += fTestNet;
    bool fTestNet4 = GetBoolArg("-testnet4", false);
    num_selected += fTestNet4;
    bool fUnl = GetBoolArg("-chain_nol", false);
    num_selected += fUnl;

    if (num_selected > 1)
        throw std::runtime_error("Invalid combination of -regtest, -testnet, and -testnet4.");
    if (fRegTest)
        return CBaseChainParams::REGTEST;
    if (fTestNet)
        return CBaseChainParams::TESTNET;
    if (fTestNet4)
        return CBaseChainParams::TESTNET4;
    if (fUnl)
        return CBaseChainParams::UNL;
    return CBaseChainParams::MAIN;
}

bool AreBaseParamsConfigured() { return pCurrentBaseParams != nullptr; }
