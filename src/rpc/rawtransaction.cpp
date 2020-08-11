// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "blockstorage/blockstorage.h"
#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "dstencode.h"
#include "index/txindex.h"
#include "init.h"
#include "keystore.h"
#include "main.h"
#include "merkleblock.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "txadmission.h"
#include "txmempool.h"
#include "uint256.h"
#include "utilstrencodings.h"
#include "validation/validation.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>

#include <univalue.h>

using namespace std;

void ScriptPubKeyToJSON(const CScript &scriptPubKey, UniValue &out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired))
    {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    UniValue a(UniValue::VARR);
    for (const CTxDestination &addr : addresses)
    {
        a.push_back(EncodeDestination(addr));
    }

    out.pushKV("addresses", a);
}


void TxToJSON(const CTransaction &tx, const int64_t txTime, const uint256 hashBlock, UniValue &entry)
{
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("size", (int)tx.GetTxSize());
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("locktime", (int64_t)tx.nLockTime);
    UniValue vin(UniValue::VARR);
    for (const CTxIn &txin : tx.vin)
    {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else
        {
            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("vout", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", o);
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut &txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.pushKV("value", ValueFromAmount(txout.nValue));
        out.pushKV("n", (int64_t)i);
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);
    }
    entry.pushKV("vout", vout);

    bool confs = false;
    if (!hashBlock.IsNull())
    {
        entry.pushKV("blockhash", hashBlock.GetHex());
        CBlockIndex *pindex = LookupBlockIndex(hashBlock);
        if (pindex)
        {
            if (chainActive.Contains(pindex))
            {
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
                confs = true;
            }
        }
    }
    // If the confirmations wasn't written with a valid block, then we have 0 confirmations.
    if (!confs)
    {
        entry.pushKV("confirmations", 0);
        if (txTime != -1)
            entry.pushKV("time", txTime);
    }

    entry.pushKV("hex", EncodeHexTx(tx));
}

UniValue getrawtransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose \"blockhash\" )\n"

            "\nNOTE: By default this function only works for mempool transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions. If the block which contains the transaction\n"
            "is known, its hash can be provided even for nodes without -txindex. Note that if a blockhash is\n"
            "provided, only that block will be searched and if the transaction is in the mempool or other\n"
            "blocks, or if this node does not have the given block available, the transaction will not be found.\n"
            "DEPRECATED: for now, it also works for transactions with unspent outputs.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.\n"
            "If verbose is non-zero, returns an Object with information about 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose     (bool, optional, default=false) If false, return a string, otherwise return a json object\n"
            "3. \"blockhash\" (string, optional) The block in which to look for the transaction\n"

            "\nResult (if verbose is not set or set to 0):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose > 0):\n"
            "{\n"
            "  \"in_active_chain\": b, (bool) Whether specified block is in the active chain or not (only present with "
            "explicit \"blockhash\" argument)\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " +
            CURRENCY_UNIT +
            "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"bitcoinaddress\"        (string) bitcoin address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getrawtransaction", "\"mytxid\"") + HelpExampleCli("getrawtransaction", "\"mytxid\" true") +
            HelpExampleRpc("getrawtransaction", "\"mytxid\", true") +
            HelpExampleCli("getrawtransaction", "\"mytxid\" false \"myblockhash\"") +
            HelpExampleCli("getrawtransaction", "\"mytxid\" true \"myblockhash\""));

    bool in_active_chain = true;
    uint256 hash = ParseHashV(params[0], "parameter 1");
    CBlockIndex *blockindex = nullptr;

    bool fVerbose = false;
    if (!params[1].isNull())
    {
        fVerbose = params[1].isNum() ? (params[1].get_int() != 0) : params[1].get_bool();
    }

    if (!params[2].isNull())
    {
        uint256 blockhash = ParseHashV(params[2], "parameter 3");
        if (!blockhash.IsNull())
        {
            READLOCK(cs_mapBlockIndex);
            BlockMap::iterator it = mapBlockIndex.find(blockhash);
            if (it == mapBlockIndex.end())
            {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
            }
            blockindex = it->second;
            in_active_chain = chainActive.Contains(blockindex);
        }
    }

    CTransactionRef tx;
    int64_t txTime = GetTime(); // Will be overwritten by GetTransaction if we have a better value
    uint256 hash_block;
    if (!GetTransaction(hash, tx, txTime, Params().GetConsensus(), hash_block, true, blockindex))
    {
        std::string errmsg;
        if (blockindex)
        {
            READLOCK(cs_mapBlockIndex);
            if (!(blockindex->nStatus & BLOCK_HAVE_DATA))
            {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
            }
            errmsg = "No such transaction found in the provided block";
        }
        else if (!fTxIndex)
        {
            errmsg = "No such mempool transaction. Use -txindex to enable blockchain transaction queries";
        }
        else if (fTxIndex && !IsTxIndexReady())
        {
            errmsg = "transaction index is still syncing...try again later";
        }
        else
        {
            errmsg = "No such mempool or blockchain transaction";
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
    }

    if (!fVerbose)
    {
        return EncodeHexTx(*tx);
    }

    UniValue result(UniValue::VOBJ);
    if (blockindex)
        result.pushKV("in_active_chain", in_active_chain);
    TxToJSON(*tx, txTime, hash_block, result);
    return result;
}

bool is_digits(const std::string &str)
{
    return std::all_of(str.begin(), str.end(), ::isdigit); // C++11
}

UniValue getrawblocktransactions(const UniValue &params, bool fHelp)
{
    bool fVerbose = false;

    // check for param  --verbose or -v
    string::size_type params_offset = 0;
    if (params[0].isStr() && (params[0].get_str() == "--verbose" || params[0].get_str() == "-v"))
    {
        fVerbose = true;
        ++params_offset;
    }

    if (fHelp || params.size() < (1 + params_offset) || params.size() > (2 + params_offset))
        throw runtime_error(
            "getrawblocktransactions\n"
            "\nReturn the raw transaction data for a given block.\n"
            "\nIf verbose=0, each tx is a string that is serialized, hex-encoded data.\n"
            "If verbose is non-zero, returns an array of Objects with information about each tx in the block.\n"

            "\nArguments:\n"
            "1. \"-v\" or \"--verbose\" (string, optional, default=false) return an array of txid:hexstring, other "
            "return an "
            "array of tx json object\n"
            "2. \"hashblock\"  (string, required) The block hash\n"
            "3. \"protocol_id\" (string, optional) The protocol id to search OP_RETURN for. Use * as a wildcard for "
            "any id. If this param is entered we will not return any transactions that do not meet the protocol id "
            "criteria\n"

            "\nResult (if verbose is not set):\n"
            "{\n"
            "  \"txid\" : \"data\",      (string) The serialized, hex-encoded data for 'txid'\n"
            "  ...\n"
            "}\n"

            "\nResult (if verbose is set):\n"
            "{\n"
            "  \"txid\" : {                (string) The transaction id (same as provided)\n"
            "    \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "    \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "    \"size\" : n,             (numeric) The transaction size\n"
            "    \"version\" : n,          (numeric) The version\n"
            "    \"locktime\" : ttt,       (numeric) The lock time\n"
            "    \"vin\" : [               (array of json objects)\n"
            "       {\n"
            "         \"txid\": \"id\",    (string) The transaction id\n"
            "         \"vout\": n,         (numeric) \n"
            "         \"scriptSig\": {     (json object) The script\n"
            "           \"asm\": \"asm\",  (string) asm\n"
            "           \"hex\": \"hex\"   (string) hex\n"
            "         },\n"
            "         \"sequence\": n      (numeric) The script sequence number\n"
            "       }\n"
            "       ,...\n"
            "      ],\n"
            "    \"vout\" : [              (array of json objects)\n"
            "       {\n"
            "         \"value\" : x.xxx,            (numeric) The value in " +
            CURRENCY_UNIT +
            "\n"
            "         \"n\" : n,                    (numeric) index\n"
            "         \"scriptPubKey\" : {          (json object)\n"
            "           \"asm\" : \"asm\",          (string) the asm\n"
            "           \"hex\" : \"hex\",          (string) the hex\n"
            "           \"reqSigs\" : n,            (numeric) The required sigs\n"
            "           \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "           \"addresses\" : [           (json array of string)\n"
            "             \"bitcoinaddress\"        (string) bitcoin address\n"
            "             ,...\n"
            "           ]\n"
            "         }\n"
            "       }\n"
            "      ,...\n"
            "      ],\n"
            "    \"blockhash\" : \"hash\",   (string) the block hash\n"
            "    \"confirmations\" : n,      (numeric) The confirmations\n"
            "    \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "    \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  },\n"
            "  ...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getrawblocktransactions", "\"hashblock\"") +
            HelpExampleCli("getrawblocktransactions", "\"hashblock\" 1") +
            HelpExampleRpc("getrawblocktransactions", "\"hashblock\", 1"));

    uint256 hashBlock = ParseHashV(params[0 + params_offset], "parameter 1");

    std::string str_protocol_id = "";
    bool fAll = false;
    uint32_t protocol_id = 0;
    bool has_protocol = params.size() > (1 + params_offset);
    if (has_protocol)
    {
        str_protocol_id = params[1 + params_offset].get_str();
        fAll = (str_protocol_id == "*");
        if (!fAll)
        {
            if (!is_digits(str_protocol_id))
            {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid protocol id");
            }
            protocol_id = std::stoi(str_protocol_id);
        }
    }

    CBlockIndex *pblockindex = nullptr;
    pblockindex = LookupBlockIndex(hashBlock);
    if (!pblockindex)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    UniValue resultSet(UniValue::VOBJ);
    for (auto tx : block.vtx)
    {
        if (has_protocol)
        {
            if (fAll && !tx->HasData())
            {
                continue;
            }
            else if (!fAll && !tx->HasData(protocol_id))
            {
                continue;
            }
        }
        string strHex = EncodeHexTx(*tx);

        if (!fVerbose)
        {
            resultSet.pushKV(tx->GetHash().GetHex(), strHex);
            continue;
        }

        UniValue result(UniValue::VOBJ);
        result.pushKV("hex", strHex);
        TxToJSON(*tx, 0, block.GetHash(), result); // txTime is 0 because block time will used
        resultSet.pushKV(tx->GetHash().ToString(), result);
    }
    return resultSet;
}

UniValue getrawtransactionssince(const UniValue &params, bool fHelp)
{
    bool fVerbose = false;

    // check for param  --verbose or -v
    string::size_type params_offset = 0;
    if (params[0].isStr() && (params[0].get_str() == "--verbose" || params[0].get_str() == "-v"))
    {
        fVerbose = true;
        ++params_offset;
    }

    if (fHelp || params.size() < (1 + params_offset) || params.size() > (3 + params_offset))
        throw runtime_error(
            "getrawtransactionssince\n"
            "\nReturn the raw transaction data for <count> blocks starting with blockhash and moving towards the "
            "tip.\n"
            "\nIf verbose=0, each tx is a string that is serialized, hex-encoded data.\n"
            "If verbose is non-zero, returns an array of Objects with information about each tx in the block.\n"

            "\nArguments:\n"
            "1. \"-v\" or \"--verbose\" (string, optional, default=false) return an array of txid:hexstring, other "
            "return an "
            "array of tx json object\n"
            "2. \"hashblock\" (string, required) The block hash\n"
            "3. count    (numeric, optional, default=1) Fetch information for <count> blocks "
            "starting with <hashblock> and moving towards the chain tip\n"
            "4. \"protocol_id\" (string, optional) The protocol id to search OP_RETURN for. Use * as a wildcard for "
            "any id. If this param is entered we will not return any transactions that do not meet the protocol id "
            "criteria\n"


            "\nResult (if verbose is not set or set to 0):\n"
            "{\n"
            "  \"hash\" : {    (string) the block hash\n"
            "        \"txid\" : \"data\",      (string) The serialized, hex-encoded data for 'txid'\n"
            "        ...\n"
            "  },\n"
            "  ...\n"
            "}\n"

            "\nResult (if verbose > 0):\n"
            "{\n"
            "  \"hash\" : {   (string) the block hash\n"
            "    \"txid\" : {                (string) The transaction id (same as provided)\n"
            "      \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "      \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "      \"size\" : n,             (numeric) The transaction size\n"
            "      \"version\" : n,          (numeric) The version\n"
            "      \"locktime\" : ttt,       (numeric) The lock time\n"
            "      \"vin\" : [               (array of json objects)\n"
            "         {\n"
            "           \"txid\": \"id\",    (string) The transaction id\n"
            "           \"vout\": n,         (numeric) \n"
            "           \"scriptSig\": {     (json object) The script\n"
            "             \"asm\": \"asm\",  (string) asm\n"
            "             \"hex\": \"hex\"   (string) hex\n"
            "           },\n"
            "           \"sequence\": n      (numeric) The script sequence number\n"
            "         }\n"
            "         ,...\n"
            "        ],\n"
            "      \"vout\" : [              (array of json objects)\n"
            "         {\n"
            "           \"value\" : x.xxx,            (numeric) The value in " +
            CURRENCY_UNIT +
            "\n"
            "           \"n\" : n,                    (numeric) index\n"
            "           \"scriptPubKey\" : {          (json object)\n"
            "             \"asm\" : \"asm\",          (string) the asm\n"
            "             \"hex\" : \"hex\",          (string) the hex\n"
            "             \"reqSigs\" : n,            (numeric) The required sigs\n"
            "             \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "             \"addresses\" : [           (json array of string)\n"
            "               \"bitcoinaddress\"        (string) bitcoin address\n"
            "               ,...\n"
            "             ]\n"
            "           }\n"
            "         }\n"
            "         ,...\n"
            "        ],\n"
            "      \"blockhash\" : \"hash\",   (string) the block hash\n"
            "      \"confirmations\" : n,      (numeric) The confirmations\n"
            "      \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "      \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "    },\n"
            "    ...\n"
            "  },\n"
            "  ...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getrawtransactionssince", "\"hashblock\"") +
            HelpExampleCli("getrawtransactionssince", "-v \"hashblock\"") +
            HelpExampleCli("getrawtransactionssince", "-v \"hashblock\" 10") +
            HelpExampleRpc("getrawtransactionssince", "-v \"hashblock\", 10"));

    LOCK(cs_main);

    uint256 hashBlock = ParseHashV(params[0 + params_offset], "parameter 1");

    int64_t limit = 1;
    if (params.size() > 1 + params_offset)
    {
        int64_t arg = params[1 + params_offset].get_int64();
        if (arg > 1)
        {
            limit = arg;
        }
    }

    std::string str_protocol_id = "";
    bool fAll = false;
    uint32_t protocol_id = 0;
    bool has_protocol = params.size() > (2 + params_offset);
    if (has_protocol)
    {
        str_protocol_id = params[2 + params_offset].get_str();
        fAll = (str_protocol_id == "*");
        if (!fAll)
        {
            if (!is_digits(str_protocol_id))
            {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid protocol id");
            }
            protocol_id = std::stoi(str_protocol_id);
        }
    }

    CBlockIndex *pblockindex = LookupBlockIndex(hashBlock);
    if (!pblockindex)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }
    int hashBlockHeight = pblockindex->nHeight;
    UniValue resultSet(UniValue::VOBJ);
    int64_t fetched = 0;
    while (fetched < limit)
    {
        pblockindex = chainActive[hashBlockHeight + fetched];
        if (!pblockindex)
        {
            // we are now past the tip
            break;
        }
        CBlock block;
        if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
        }
        UniValue blockResults(UniValue::VOBJ);
        for (auto tx : block.vtx)
        {
            if (has_protocol)
            {
                if (fAll && !tx->HasData())
                {
                    continue;
                }
                else if (!fAll && !tx->HasData(protocol_id))
                {
                    continue;
                }
            }
            string strHex = EncodeHexTx(*tx);
            if (!fVerbose)
            {
                blockResults.pushKV(tx->GetHash().ToString(), strHex);
                continue;
            }
            UniValue txDetails(UniValue::VOBJ);
            txDetails.pushKV("hex", strHex);
            TxToJSON(*tx, 0, block.GetHash(), txDetails); // txTime can be 0 because block time overrides
            blockResults.pushKV(tx->GetHash().ToString(), txDetails);
        }
        resultSet.pushKV(block.GetHash().GetHex(), blockResults);
        fetched++;
    }

    return resultSet;
}

UniValue gettxoutproof(const UniValue &params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2))
        throw runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included in manually (by blockhash).\n"
            "\nReturn the raw transaction data.\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"block hash\"  (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n");

    set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++)
    {
        const UniValue &txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid txid ") + txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated txid: ") + txid.get_str());
        setTxids.insert(hash);
        oneTxid = hash;
    }

    CBlockIndex *pblockindex = nullptr;

    uint256 hashBlock;
    if (params.size() > 1)
    {
        hashBlock = uint256S(params[1].get_str());
        pblockindex = LookupBlockIndex(hashBlock);
        if (!pblockindex)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }
    else
    {
        LOCK(cs_main);
        CoinAccessor coin(*pcoinsTip, oneTxid);
        if (coin && !coin->IsSpent() && coin->nHeight > 0 && coin->nHeight <= chainActive.Height())
        {
            pblockindex = chainActive[coin->nHeight];
        }
    }

    if (pblockindex == nullptr)
    {
        CTransactionRef tx;
        int64_t txTime = 0; // This data is not needed for this function
        if (!GetTransaction(oneTxid, tx, txTime, Params().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
        {
            std::string errmsg;
            if (!fTxIndex)
            {
                errmsg = "No such mempool transaction. Use -txindex to enable blockchain transaction queries";
            }
            else if (fTxIndex && !IsTxIndexReady())
                errmsg = "Transaction index is still syncing...try again later";
            else
                errmsg = "Transaction not found in transaction index";

            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg);
        }
        pblockindex = LookupBlockIndex(hashBlock);
        if (!pblockindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
    }

    CBlock block;
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    for (const auto &tx : block.vtx)
        if (setTxids.count(tx->GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "(Not all) transactions not found in specified block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue gettxoutproofs(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "gettxoutproofs [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included in manually (by blockhash).\n"
            "\nReturn the raw transaction data.\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"block hash\"  (string) Looks for txid in the block with this hash\n"
            "\nResult:\n"
            "{\n"
            "   \"txid\":\"data\",           (string) A string that is a serialized, hex-encoded data for the proof.\n"
            "   ..."
            "}\n");

    set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++)
    {
        const UniValue &txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid txid ") + txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated txid: ") + txid.get_str());
        setTxids.insert(hash);
        oneTxid = hash;
    }

    CBlockIndex *pblockindex = nullptr;

    uint256 hashBlock;
    hashBlock = uint256S(params[1].get_str());
    pblockindex = LookupBlockIndex(hashBlock);
    if (!pblockindex)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    UniValue resultSet(UniValue::VOBJ);

    bool ntxFound = false;
    for (const auto &txid : setTxids)
    {
        for (const auto &tx : block.vtx)
        {
            if (setTxids.count(tx->GetHash()))
            {
                ntxFound = true;
                break;
            }
        }
        if (ntxFound == false)
        {
            continue;
        }
        std::set<uint256> setTxid;
        setTxid.insert(txid);
        CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
        CMerkleBlock mb(block, setTxid);
        ssMB << mb;
        std::string strHex = HexStr(ssMB.begin(), ssMB.end());
        resultSet.pushKV(txid.ToString(), strHex);
        ntxFound = false;
    }
    return resultSet;
}

UniValue verifytxoutproof(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is "
            "invalid\n");

    CDataStream ssMB(ParseHexV(params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    vector<uint256> vMatch;
    vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot)
        return res;

    auto *pindex = LookupBlockIndex(merkleBlock.header.GetHash());

    {
        LOCK(cs_main);
        if (!pindex || !chainActive.Contains(pindex))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");
    }

    for (const uint256 &hash : vMatch)
        res.push_back(hash.GetHex());
    return res;
}

UniValue createrawtransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,\"data\":\"hex\",...} ( "
            "locktime )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs can be addresses or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"transactions\"        (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",    (string, required) The transaction id\n"
            "         \"vout\":n        (numeric, required) The output number\n"
            "         \"vout\":n,         (numeric, required) The output number\n"
            "         \"sequence\":n    (numeric, optional) The sequence number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"             (string, required) a json object with outputs\n"
            "    {\n"
            "      \"address\": x.xxx   (numeric or string, required) The key is the bitcoin address, the numeric "
            "value (can be string) is the " +
            CURRENCY_UNIT +
            " amount\n"
            "      \"data\": \"hex\",     (string, required) The key is \"data\", the value is hex encoded data\n"
            "      ...\n"
            "    }\n"
            "3. locktime                (numeric, optional, default=0) Raw locktime. Non-0 value also "
            "locktime-activates inputs\n"
            "\nResult:\n"
            "\"transaction\"            (string) hex string of the transaction\n"

            "\nExamples\n" +
            HelpExampleCli(
                "createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"") +
            HelpExampleCli("createrawtransaction",
                "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"") +
            HelpExampleRpc("createrawtransaction",
                "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"") +
            HelpExampleRpc("createrawtransaction",
                "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\""));

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ)(UniValue::VNUM), true);
    if (params[0].isNull() || params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = params[0].get_array();
    UniValue sendTo = params[1].get_obj();

    CMutableTransaction rawTx;

    if (params.size() > 2 && !params[2].isNull())
    {
        int64_t nLockTime = params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

    for (unsigned int idx = 0; idx < inputs.size(); idx++)
    {
        const UniValue &input = inputs[idx];
        const UniValue &o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue &vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence =
            (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());

        // set the sequence number if passed in the parameters object
        const UniValue &sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum())
        {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            else
                nSequence = (uint32_t)seqNr64;
        }
        else if (!sequenceObj.isNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence parameter is not a number");
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::set<CTxDestination> destinations;
    std::vector<std::string> addrList = sendTo.getKeys();
    for (const std::string &name_ : addrList)
    {
        if (name_ == "data")
        {
            std::vector<unsigned char> data = ParseHexV(sendTo[name_].getValStr(), "Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        }
        else
        {
            CTxDestination destination = DecodeDestination(name_);
            if (!IsValidDestination(destination))
            {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Bitcoin address: ") + name_);
            }

            if (!destinations.insert(destination).second)
            {
                throw JSONRPCError(
                    RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            }

            CScript scriptPubKey = GetScriptForDestination(destination);
            CAmount nAmount = AmountFromValue(sendTo[name_]);

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("decoderawtransaction \"hexstring\"\n"
                            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

                            "\nArguments:\n"
                            "1. \"hex\"      (string, required) The transaction hex string\n"

                            "\nResult:\n"
                            "{\n"
                            "  \"txid\" : \"id\",        (string) The transaction id\n"
                            "  \"size\" : n,             (numeric) The transaction size\n"
                            "  \"version\" : n,          (numeric) The version\n"
                            "  \"locktime\" : ttt,       (numeric) The lock time\n"
                            "  \"vin\" : [               (array of json objects)\n"
                            "     {\n"
                            "       \"txid\": \"id\",    (string) The transaction id\n"
                            "       \"vout\": n,         (numeric) The output number\n"
                            "       \"scriptSig\": {     (json object) The script\n"
                            "         \"asm\": \"asm\",  (string) asm\n"
                            "         \"hex\": \"hex\"   (string) hex\n"
                            "       },\n"
                            "       \"sequence\": n     (numeric) The script sequence number\n"
                            "     }\n"
                            "     ,...\n"
                            "  ],\n"
                            "  \"vout\" : [             (array of json objects)\n"
                            "     {\n"
                            "       \"value\" : x.xxx,            (numeric) The value in " +
                            CURRENCY_UNIT +
                            "\n"
                            "       \"n\" : n,                    (numeric) index\n"
                            "       \"scriptPubKey\" : {          (json object)\n"
                            "         \"asm\" : \"asm\",          (string) the asm\n"
                            "         \"hex\" : \"hex\",          (string) the hex\n"
                            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
                            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
                            "         \"addresses\" : [           (json array of string)\n"
                            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) bitcoin address\n"
                            "           ,...\n"
                            "         ]\n"
                            "       }\n"
                            "     }\n"
                            "     ,...\n"
                            "  ],\n"
                            "}\n"

                            "\nExamples:\n" +
                            HelpExampleCli("decoderawtransaction", "\"hexstring\"") +
                            HelpExampleRpc("decoderawtransaction", "\"hexstring\""));

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    CTransaction tx;

    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, -1, uint256(), result); // don't show the time since its not part of the tx serialized data

    return result;
}

UniValue decodescript(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("decodescript \"hex\"\n"
                            "\nDecode a hex-encoded script.\n"
                            "\nArguments:\n"
                            "1. \"hex\"     (string) the hex encoded script\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"asm\":\"asm\",   (string) Script public key\n"
                            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
                            "  \"type\":\"type\", (string) The output type\n"
                            "  \"reqSigs\": n,    (numeric) The required signatures\n"
                            "  \"addresses\": [   (json array of string)\n"
                            "     \"address\"     (string) bitcoin address\n"
                            "     ,...\n"
                            "  ],\n"
                            "  \"p2sh\",\"address\" (string) script address\n"
                            "}\n"
                            "\nExamples:\n" +
                            HelpExampleCli("decodescript", "\"hexstring\"") +
                            HelpExampleRpc("decodescript", "\"hexstring\""));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (params[0].get_str().size() > 0)
    {
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    }
    else
    {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    UniValue type;
    type = find_value(r, "type");

    if (type.isStr() && type.get_str() != "scripthash")
    {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH.
        r.pushKV("p2sh", EncodeDestination(CScriptID(script)));
    }

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn &txin, UniValue &vErrorsRet, const std::string &strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("txid", txin.prevout.hash.ToString());
    entry.pushKV("vout", (uint64_t)txin.prevout.n);
    entry.pushKV("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
    entry.pushKV("sequence", (uint64_t)txin.nSequence);
    entry.pushKV("error", strMessage);
    vErrorsRet.push_back(entry);
}

UniValue signrawtransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "signrawtransaction \"hexstring\" ( "
            "[{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] "
            "[\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase() +
            "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\"    (string, required for P2SH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privatekeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       followed by ANYONECANPAY and/or FORKID/NOFORKID flags separated with |, for example\n"
            "       \"ALL|ANYONECANPAY|FORKID\"\n"
            "       \"NONE|FORKID\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("signrawtransaction", "\"myhex\"") + HelpExampleRpc("signrawtransaction", "\"myhex\""));

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VARR)(UniValue::VARR)(UniValue::VSTR), true);

    vector<unsigned char> txData(ParseHexV(params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CMutableTransaction> txVariants;
    while (!ssData.empty())
    {
        try
        {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (const std::exception &)
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        READLOCK(mempool.cs_txmempool);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        {
            WRITELOCK(view.cs_utxo);
            for (const CTxIn &txin : mergedTx.vin)
            {
                // Load entries from viewChain into view; can fail.
                view._AccessCoin(txin.prevout);
            }
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && !params[2].isNull())
    {
        fGivenKeys = true;
        UniValue keys = params[2].get_array();
        for (unsigned int idx = 0; idx < keys.size(); idx++)
        {
            UniValue k = keys[idx];
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwalletMain)
        EnsureWalletIsUnlocked();
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && !params[1].isNull())
    {
        UniValue prevTxs = params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++)
        {
            const UniValue &p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(
                    RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)(
                                         "scriptPubKey", UniValue::VSTR));

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            Coin newcoin;
            {
                CoinAccessor coin(view, out);

                if (!coin->IsSpent() && coin->out.scriptPubKey != scriptPubKey)
                {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin->out.scriptPubKey) + "\nvs:\n" + ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = 0;
                if (prevOut.exists("amount"))
                {
                    newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
                newcoin.nHeight = 1;
            }
            view.AddCoin(out, std::move(newcoin), true);

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash())
            {
                RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)(
                                             "scriptPubKey", UniValue::VSTR)("redeemScript", UniValue::VSTR));
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull())
                {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore &keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else
    const CKeyStore &keystore = tempKeystore;
#endif

    bool fForkId = true;
    int nHashType = SIGHASH_ALL | SIGHASH_FORKID;
    if (params.size() > 3 && !params[3].isNull())
    {
        std::string strHashType = params[3].get_str();

        std::vector<string> strings;
        std::istringstream ss(strHashType);
        std::string s;
        while (getline(ss, s, '|'))
        {
            boost::trim(s);
            if (boost::iequals(s, "ALL"))
                nHashType = SIGHASH_ALL;
            else if (boost::iequals(s, "NONE"))
                nHashType = SIGHASH_NONE;
            else if (boost::iequals(s, "SINGLE"))
                nHashType = SIGHASH_SINGLE;
            else if (boost::iequals(s, "ANYONECANPAY"))
                nHashType |= SIGHASH_ANYONECANPAY;
            else if (boost::iequals(s, "FORKID"))
                nHashType |= SIGHASH_FORKID;
            else if (boost::iequals(s, "NOFORKID"))
            {
                // Still support signing legacy chain transactions
                fForkId = false;
                nHashType &= ~SIGHASH_FORKID;
            }
            else
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
            }
        }
    }

    bool fHashSingle = ((nHashType & ~(SIGHASH_ANYONECANPAY | SIGHASH_FORKID)) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTxIn &txin = mergedTx.vin[i];
        CoinAccessor coin(view, txin.prevout);
        if (coin->IsSpent())
        {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript &prevPubKey = coin->out.scriptPubKey;
        const CAmount &amount = coin->out.nValue;

        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            SignSignature(keystore, prevPubKey, mergedTx, i, amount, nHashType);

        // ... and merge in other signatures:
        if (fForkId)
        {
            for (const CMutableTransaction &txv : txVariants)
            {
                txin.scriptSig = CombineSignatures(prevPubKey,
                    TransactionSignatureChecker(&txConst, i, amount, SCRIPT_ENABLE_SIGHASH_FORKID), txin.scriptSig,
                    txv.vin[i].scriptSig);
            }
            ScriptError serror = SCRIPT_ERR_OK;
            if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENABLE_SIGHASH_FORKID,
                    maxScriptOps.Value(),
                    MutableTransactionSignatureChecker(&mergedTx, i, amount, SCRIPT_ENABLE_SIGHASH_FORKID), &serror))
            {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
        else
        {
            // Still support signing legacy chain transactions
            for (const CMutableTransaction &txv : txVariants)
            {
                txin.scriptSig = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount, 0),
                    txin.scriptSig, txv.vin[i].scriptSig);
            }
            ScriptError serror = SCRIPT_ERR_OK;
            if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, maxScriptOps.Value(),
                    MutableTransactionSignatureChecker(&mergedTx, i, amount, 0), &serror))
            {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(mergedTx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty())
    {
        result.pushKV("errors", vErrors);
    }

    return result;
}

UniValue sendrawtransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees, allownonstandard )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "This API does not return until the transaction has been fully validated, and raises\n"
            "an exception if submission was unsuccessful.\n"
            "\nAlso see enqueuerawtransaction, createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "3. allownonstandard (string 'standard', 'nonstandard', 'default', optional, default='default')\n"
            "                    Force standard or nonstandard transaction check\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction",
                "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n" + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n" + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n" + HelpExampleRpc("sendrawtransaction", "\"signedhex\""));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL)(UniValue::VSTR));

    // parse hex string from parameter
    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef ptx(MakeTransactionRef(std::move(tx)));
    const uint256 &hashTx = ptx->GetHash();

    bool fOverrideFees = false;
    TransactionClass txClass = TransactionClass::DEFAULT;

    // 2nd parameter allows high fees
    if (params.size() > 1)
    {
        fOverrideFees = params[1].get_bool();
    }
    // 3rd parameter must be the transaction class
    if (params.size() > 2)
    {
        txClass = ParseTransactionClass(params[2].get_str());
        if (txClass == TransactionClass::INVALID)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid transaction class");
    }

    CCoinsViewCache &view = *pcoinsTip;
    bool fHaveChain = false;
    {
        for (size_t o = 0; !fHaveChain && o < tx.vout.size(); o++)
        {
            CoinAccessor existingCoin(view, COutPoint(hashTx, o));
            fHaveChain = !existingCoin->IsSpent();
        }
    }
    bool fHaveMempool = mempool.exists(hashTx);
    if (!fHaveMempool && !fHaveChain)
    {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(mempool, state, ptx, false, &fMissingInputs, false, !fOverrideFees, txClass))
        {
            if (state.IsInvalid())
            {
                throw JSONRPCError(
                    RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            }
            else
            {
                if (fMissingInputs)
                {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        }
#ifdef ENABLE_WALLET
        else
            SyncWithWallets(ptx, nullptr, -1);
#endif
    }
    else if (fHaveChain)
    {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    return hashTx.GetHex();
}

void InputDebuggerToJSON(const CInputDebugger &input, UniValue &result)
{
    std::map<std::string, std::string>::const_iterator it;

    result.pushKV("isValid", input.isValid);
    UniValue uv_vdata(UniValue::VARR);
    for (auto &data : input.vData)
    {
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("isValid", data.isValid);
        UniValue entry_metadata(UniValue::VOBJ);
        for (it = data.metadata.begin(); it != data.metadata.end(); ++it)
        {
            entry_metadata.pushKV(it->first, it->second);
        }
        entry.pushKV("metadata", entry_metadata);
        UniValue entry_errors(UniValue::VARR);
        for (auto &error : data.errors)
        {
            entry_errors.push_back(error);
        }
        entry.pushKV("errors", entry_errors);
        uv_vdata.push_back(entry);
    }
    result.pushKV("inputs", uv_vdata);
}

UniValue validaterawtransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
    {
        throw std::runtime_error(
            "validaterawtransaction \"hexstring\" ( allowhighfees, allownonstandard )\n"
            "\nValidates raw transaction (serialized, hex-encoded) to local node without broadcasting it.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "3. allownonstandard (string 'standard', 'nonstandard', 'default', optional, default='default')\n"
            "                    Force standard or nonstandard transaction check\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"value\",           (string) The transaction hash\n"
            "  \"isValid\" : true|false,   (boolean) Will the transaction be accepted into the memory pool\n"
            "  \"isMineable\" : true|false,   (boolean) If the transaction is mineable now\n"
            "  \"isFutureMineable\" : true|false,   (boolean) If the transaction is mineable in the future\n"
            "  \"isStandard\" : true|false,   (boolean) If the transaction is standard\n"
            "  \"metadata\" : {\n"
            "       \"size\" : value,        (numeric) The size of the transaction in bytes\n"
            "       \"fee\" : value,         (numeric) The amount of fee included in the transaction in satoshi\n"
            "       \"feeneeded\" : value,   (numeric) The amount of fee needed for the transactio in satoshi\n"
            "    },"
            "  \"errors\" : [                 (json array) Script verification errors (if there are any)\n"
            "      \"reason\",           (string) A reason the tx would be rejected by the mempool\n"
            "        ...\n"
            "    ],\n"
            "  \"input_flags\" : {\n"
            "       \"isValid\" : true|false,        (boolean) Are all of the tx inputs valid with standard flags\n"
            "       \"inputs\" : [\n"
            "           \"isValid\" : true|false,        (boolean) is this input valid with standard flags\n"
            "           \"metadata\" : {\n"
            "               \"prevtx\" : value,        (string) The hash of the referenced, previous transaction\n"
            "               \"n\" : value,         (numeric) The index of the output to spent and used as input\n"
            "               \"scriptPubKey\" : value,   (string) The hex-encoded signature pubkey\n"
            "               \"scriptSig\" : value,   (string) The hex-encoded signature script\n"
            "               \"amount\" : value,   (numeric) The value of the output spent\n"
            "             },\n"
            "           \"errors\" : [                 (json array) standard flag errors with the input (if there are "
            "any)\n"
            "               \"reason\",           (string) A reason the input would be rejected with standard flags\n"
            "                ...\n"
            "             ]\n"
            "       ]\n"
            "    },\n"
            "  \"inputs_mandatoryFlags\" : {\n"
            "       \"isValid\" : true|false,        (boolean) Are all of the tx inputs valid with mandatory flags\n"
            "       \"inputs\" : [\n"
            "           \"isValid\" : true|false,        (boolean) is this input valid with mandatory flags\n"
            "           \"metadata\" : {\n"
            "               \"prevtx\" : value,        (string) The hash of the referenced, previous transaction\n"
            "               \"n\" : value,         (numeric) The index of the output to spent and used as input\n"
            "               \"scriptPubKey\" : value,   (string) The hex-encoded signature pubkey\n"
            "               \"scriptSig\" : value,   (string) The hex-encoded signature script\n"
            "               \"amount\" : value,   (numeric) The value of the output spent\n"
            "             },\n"
            "           \"errors\" : [                 (json array) mandatory flag errors with the input (if there are "
            "any)\n"
            "               \"reason\",           (string) A reason the input would be rejected with mandatory flags\n"
            "                ...\n"
            "             ]\n"
            "       ]\n"
            "    }\n"
            "}\n"
            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction",
                "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n" + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n" + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n" + HelpExampleRpc("validaterawtransaction", "\"signedhex\""));
    }

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL)(UniValue::VSTR));

    // parse hex string from parameter
    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef ptx(MakeTransactionRef(std::move(tx)));
    const uint256 &hashTx = ptx->GetHash();

    bool fOverrideFees = false;
    TransactionClass txClass = TransactionClass::DEFAULT;

    // 2nd parameter allows high fees
    if (params.size() > 1)
    {
        if (params[1].isBool())
        {
            fOverrideFees = params[1].get_bool();
        }
        else if (params[1].isStr())
        {
            std::string maybeOverride = params[1].get_str();
            if (maybeOverride == "allowhighfees")
            {
                fOverrideFees = true;
            }
            else if (maybeOverride == "allowhighfees")
            {
                fOverrideFees = false;
            }
            else
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid allowhighfees value");
            }
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid allowhighfees value");
        }
    }
    // 3rd parameter must be the transaction class
    if (params.size() > 2)
    {
        txClass = ParseTransactionClass(params[2].get_str());
        if (txClass == TransactionClass::INVALID)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid transaction class");
    }

    CCoinsViewCache &view = *pcoinsTip;
    bool fHaveChain = false;
    {
        for (size_t i = 0; !fHaveChain && i < tx.vout.size(); i++)
        {
            CoinAccessor existingCoin(view, COutPoint(hashTx, i));
            fHaveChain = !existingCoin->IsSpent();
        }
    }
    UniValue result(UniValue::VOBJ);
    bool fHaveMempool = mempool.exists(hashTx);
    CValidationDebugger debugger;
    if (!fHaveMempool && !fHaveChain)
    {
        CValidationState state;
        bool fMissingInputs = false;
        std::vector<COutPoint> vCoinsToUncache;
        bool isRespend = false;
        ParallelAcceptToMemoryPool(txHandlerSnap, mempool, state, std::move(ptx), false, &fMissingInputs, false,
            fOverrideFees, txClass, vCoinsToUncache, &isRespend, &debugger);
    }
    else if (fHaveChain)
    {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }

    result.pushKV("txid", debugger.txid);
    result.pushKV("isValid", debugger.IsValid());
    result.pushKV("isMineable", debugger.mineable);
    result.pushKV("isFutureMineable", debugger.futureMineable);
    result.pushKV("isStandard", debugger.standard);

    UniValue uv_txmetadata(UniValue::VOBJ);
    std::map<std::string, std::string>::iterator it;
    for (it = debugger.txMetadata.begin(); it != debugger.txMetadata.end(); ++it)
    {
        uv_txmetadata.pushKV(it->first, it->second);
    }
    result.pushKV("metadata", uv_txmetadata);

    UniValue uv_errors(UniValue::VARR);
    std::vector<std::string> strRejectReasons = debugger.GetRejectReasons();
    for (auto &error : strRejectReasons)
    {
        uv_errors.push_back(error);
    }
    result.pushKV("errors", uv_errors);

    UniValue uv_inputCheck1(UniValue::VOBJ);
    CInputDebugger input1 = debugger.GetInputCheck1();
    InputDebuggerToJSON(input1, uv_inputCheck1);
    result.pushKV("inputs_flags", uv_inputCheck1);

    UniValue uv_inputCheck2(UniValue::VOBJ);
    CInputDebugger input2 = debugger.GetInputCheck2();
    InputDebuggerToJSON(input2, uv_inputCheck2);
    result.pushKV("inputs_mandatoryFlags", uv_inputCheck2);
    return result;
}

UniValue enqueuerawtransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "enqueuerawtransaction \"hexstring\" ( options )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "This RPC by default does not wait for transaction validation and so is very fast.\n"
            "\nAlso see sendrawtransaction, createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. \"options\"      (string, optional) \"flush\" to wait for every enqueued transaction to be handled\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction",
                "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n" + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n" + HelpExampleCli("enqueuerawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n" + HelpExampleRpc("enqueuerawtransaction", "\"signedhex\""));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str())) // parse hex string from parameter
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    CTxInputData txd;
    txd.tx = MakeTransactionRef(std::move(tx));
    txd.nodeName = "rpc";
    EnqueueTxForAdmission(txd);

    if (params.size() > 1)
    {
        if (params[1].get_str() == "flush")
        {
            FlushTxAdmission();
        }
    }

    return txd.tx->GetHash().GetHex();
}

static const CRPCCommand commands[] = {
    //  category              name                      actor (function)         okSafeMode
    //  --------------------- ------------------------  -----------------------  ----------
    {"rawtransactions", "getrawtransaction", &getrawtransaction, true},
    {"rawtransactions", "getrawblocktransactions", &getrawblocktransactions, true},
    {"rawtransactions", "getrawtransactionssince", &getrawtransactionssince, true},
    {"rawtransactions", "createrawtransaction", &createrawtransaction, true},
    {"rawtransactions", "decoderawtransaction", &decoderawtransaction, true},
    {"rawtransactions", "decodescript", &decodescript, true},
    {"rawtransactions", "sendrawtransaction", &sendrawtransaction, false},
    {"rawtransactions", "validaterawtransaction", validaterawtransaction, false},
    {"rawtransactions", "enqueuerawtransaction", &enqueuerawtransaction, false},
    {"rawtransactions", "signrawtransaction", &signrawtransaction, false}, /* uses wallet if enabled */

    {"blockchain", "gettxoutproof", &gettxoutproof, true}, {"blockchain", "gettxoutproofs", &gettxoutproofs, true},
    {"blockchain", "verifytxoutproof", &verifytxoutproof, true},
};

void RegisterRawTransactionRPCCommands(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}
