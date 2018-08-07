// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "blockstorage/blockstorage.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "coins.h"
#include "consensus/validation.h"
#include "hash.h"
#include "main.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "streams.h"
#include "sync.h"
#include "tweak.h"
#include "txdb.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"

#include <stdint.h>

#include <univalue.h>

#include <boost/thread/thread.hpp> // boost::thread::interrupt

using namespace std;

extern void TxToJSON(const CTransaction &tx, const uint256 hashBlock, UniValue &entry);
void ScriptPubKeyToJSON(const CScript &scriptPubKey, UniValue &out, bool fIncludeHex);

double GetDifficulty(const CBlockIndex *blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (chainActive.Tip() == NULL)
            return 1.0;
        else
            blockindex = chainActive.Tip();
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff = (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

UniValue blockheaderToJSON(const CBlockIndex *blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", blockindex->nVersion)));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)blockindex->nNonce));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock &block, const CBlockIndex *blockindex, bool txDetails = false, bool listTxns = true)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
    if (listTxns)
    {
        for (const auto &tx : block.vtx)
        {
            if (txDetails)
            {
                UniValue objTx(UniValue::VOBJ);
                TxToJSON(*tx, uint256(), objTx);
                txs.push_back(objTx);
            }
            else
            {
                txs.push_back(tx->GetHash().GetHex());
            }
        }
        result.push_back(Pair("tx", txs));
    }
    else
    {
        result.push_back(Pair("txcount", (uint64_t)block.vtx.size()));
    }
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue getblockcount(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getblockcount\n"
                            "\nReturns the number of blocks in the longest block chain.\n"
                            "\nResult:\n"
                            "n    (numeric) The current block count\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getblockcount", "") + HelpExampleRpc("getblockcount", ""));
    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getbestblockhash\n"
                            "\nReturns the hash of the best (tip) block in the longest block chain.\n"
                            "\nResult\n"
                            "\"hex\"      (string) the block hash hex encoded\n"
                            "\nExamples\n" +
                            HelpExampleCli("getbestblockhash", "") + HelpExampleRpc("getbestblockhash", ""));

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

UniValue getdifficulty(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n" +
            HelpExampleCli("getdifficulty", "") + HelpExampleRpc("getdifficulty", ""));

    LOCK(cs_main);
    return GetDifficulty();
}

UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose)
    {
        READLOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        for (const CTxMemPoolEntry &e : mempool.mapTx)
        {
            const uint256 &hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            info.push_back(Pair("size", (int)e.GetTxSize()));
            info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
            info.push_back(Pair("modifiedfee", ValueFromAmount(e.GetModifiedFee())));
            info.push_back(Pair("time", e.GetTime()));
            info.push_back(Pair("height", (int)e.GetHeight()));
            info.push_back(Pair("startingpriority", e.GetPriority(e.GetHeight())));
            info.push_back(Pair("currentpriority", e.GetPriority(chainActive.Height())));
            info.push_back(Pair("descendantcount", e.GetCountWithDescendants()));
            info.push_back(Pair("descendantsize", e.GetSizeWithDescendants()));
            info.push_back(Pair("descendantfees", e.GetModFeesWithDescendants()));
            const CTransaction &tx = e.GetTx();
            set<string> setDepends;
            for (const CTxIn &txin : tx.vin)
            {
                if (mempool._exists(txin.prevout.hash))
                    setDepends.insert(txin.prevout.hash.ToString());
            }

            UniValue depends(UniValue::VARR);
            for (const string &dep : setDepends)
            {
                depends.push_back(dep);
            }

            info.push_back(Pair("depends", depends));
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        for (const uint256 &hash : vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nArguments:\n"
            "1. verbose           (boolean, optional, default=false) true for a json object, false for array of "
            "transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            "    \"size\" : n,             (numeric) transaction size in bytes\n"
            "    \"fee\" : n,              (numeric) transaction fee in " +
            CURRENCY_UNIT +
            "\n"
            "    \"modifiedfee\" : n,      (numeric) transaction fee with fee deltas used for mining priority\n"
            "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 "
            "GMT\n"
            "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
            "    \"startingpriority\" : n, (numeric) priority when transaction entered pool\n"
            "    \"currentpriority\" : n,  (numeric) transaction priority now\n"
            "    \"descendantcount\" : n,  (numeric) number of in-mempool descendant transactions (including this "
            "one)\n"
            "    \"descendantsize\" : n,   (numeric) size of in-mempool descendants (including this one)\n"
            "    \"descendantfees\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including "
            "this one)\n"
            "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
            "        \"transactionid\",    (string) parent transaction id\n"
            "       ... ]\n"
            "  }, ...\n"
            "}\n"
            "\nExamples\n" +
            HelpExampleCli("getrawmempool", "true") + HelpExampleRpc("getrawmempool", "true"));

    LOCK(cs_main);

    bool fVerbose = false;
    if (params.size() > 0)
        fVerbose = params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getblockhash(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("getblockhash index\n"
                            "\nReturns hash of block in best-block-chain at index provided.\n"
                            "\nArguments:\n"
                            "1. index         (numeric, required) The block index\n"
                            "\nResult:\n"
                            "\"hash\"         (string) The block hash\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getblockhash", "1000") + HelpExampleRpc("getblockhash", "1000"));

    LOCK(cs_main);

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex *pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about blockheader <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded "
            "data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main "
            "chain\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the next block\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of hashes required to produce the current "
            "chain (in hex)\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"") +
            HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\""));

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex *pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "getblock \"hash\" ( verbose ) ( listtransactions )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbose is true, returns an Object with information about block <hash>.\n"
            "If listtransactions is true, a list of the IDs of all the transactions included in the block will be "
            "shown.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash or height\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded "
            "data\n"
            "3. listtransactions  (boolean, optional, default=true) true to get a list of all txns, false to get just "
            "txns count\n"
            "\nResult (for verbose = true, listtransactions = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main "
            "chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this "
            "block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n" +
            HelpExampleCli("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"") +
            HelpExampleRpc("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\""));

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CBlockIndex *pblockindex = NULL;
    bool fVerbose = true;
    bool fListTxns = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();
    if (params.size() == 3)
        fListTxns = params[2].get_bool();

    if (mapBlockIndex.count(hash) == 0)
    {
        arith_uint256 h = UintToArith256(hash);
        if (h.bits() < 65)
        {
            uint64_t height = boost::lexical_cast<unsigned int>(strHash);
            if (height > (uint64_t)chainActive.Height())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block index out of range");
            pblockindex = chainActive[height];
        }
        else
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }
    else
    {
        pblockindex = mapBlockIndex[hash];
    }

    CBlock block;

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, false, fListTxns);
}

static void ApplyStats(CCoinsStats &stats,
    CHashWriter &ss,
    const uint256 &hash,
    const std::map<uint32_t, Coin> &outputs)
{
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(
        outputs.begin()->second.nHeight * 2 + outputs.begin()->second.fCoinBase, VarIntMode::NONNEGATIVE_SIGNED);
    stats.nTransactions++;
    for (const auto output : outputs)
    {
        ss << VARINT(output.first + 1);
        ss << *(const CScriptBase *)(&output.second.out.scriptPubKey);
        ss << VARINT(output.second.out.nValue, VarIntMode::NONNEGATIVE_SIGNED);
        stats.nTransactionOutputs++;
        stats.nTotalAmount += output.second.out.nValue;
    }
    ss << VARINT(0u);
}

//! Calculate statistics about the unspent transaction output set
static bool GetUTXOStats(CCoinsView *view, CCoinsStats &stats)
{
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());
    assert(pcursor);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, Coin> outputs;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin))
        {
            if (!outputs.empty() && key.hash != prevkey)
            {
                ApplyStats(stats, ss, prevkey, outputs);
                outputs.clear();
            }
            prevkey = key.hash;
            outputs[key.n] = std::move(coin);
        }
        else
        {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    if (!outputs.empty())
    {
        ApplyStats(stats, ss, prevkey, outputs);
    }
    stats.hashSerialized = ss.GetHash();
    stats.nDiskSize = view->EstimateSize();
    return true;
}


UniValue gettxoutsetinfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("gettxoutsetinfo\n"
                            "\nReturns statistics about the unspent transaction output set.\n"
                            "Note this call may take some time.\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"height\":n,     (numeric) The current block height (index)\n"
                            "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
                            "  \"transactions\": n,      (numeric) The number of transactions\n"
                            "  \"txouts\": n,            (numeric) The number of output transactions\n"
                            "  \"hash_serialized\": \"hash\",   (string) The serialized hash\n"
                            "  \"disk_size\": n,         (numeric) The estimated size of the chainstate on disk\n"
                            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
                            "}\n"
                            "\nExamples:\n" +
                            HelpExampleCli("gettxoutsetinfo", "") + HelpExampleRpc("gettxoutsetinfo", ""));

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (GetUTXOStats(pcoinsdbview, stats))
    {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("hash_serialized_2", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("disk_size", stats.nDiskSize));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    }
    return ret;
}

UniValue gettxout(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout value\n"
            "3. includemempool  (boolean, optional) Whether to included the mem pool\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in " +
            CURRENCY_UNIT + "\n"
                            "  \"scriptPubKey\" : {         (json object)\n"
                            "     \"asm\" : \"code\",       (string) \n"
                            "     \"hex\" : \"hex\",        (string) \n"
                            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
                            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
                            "     \"addresses\" : [          (array of string) array of bitcoin addresses\n"
                            "        \"bitcoinaddress\"     (string) bitcoin address\n"
                            "        ,...\n"
                            "     ]\n"
                            "  },\n"
                            "  \"version\" : n,            (numeric) The version\n"
                            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
                            "}\n"

                            "\nExamples:\n"
                            "\nGet unspent transactions\n" +
            HelpExampleCli("listunspent", "") + "\nView the details\n" + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n" + HelpExampleRpc("gettxout", "\"txid\", 1"));

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = params[1].get_int();
    COutPoint out(hash, n);
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    Coin coin;
    if (fMempool)
    {
        READLOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        // TODO: filtering spent coins should be done by the CCoinsViewMemPool
        if (!view.GetCoin(out, coin) || mempool.isSpent(out))
        {
            return NullUniValue;
        }
    }
    else
    {
        if (!pcoinsTip->GetCoin(out, coin))
        {
            return NullUniValue;
        }
    }

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if (coin.nHeight == MEMPOOL_HEIGHT)
    {
        ret.push_back(Pair("confirmations", 0));
    }
    else
    {
        ret.push_back(Pair("confirmations", (int64_t)(pindex->nHeight - coin.nHeight + 1)));
    }
    ret.push_back(Pair("value", ValueFromAmount(coin.out.nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coin.out.scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("coinbase", (bool)coin.fCoinBase));

    return ret;
}

UniValue verifychain(const UniValue &params, bool fHelp)
{
    int nCheckLevel = GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (fHelp || params.size() > 2)
        throw runtime_error("verifychain ( checklevel numblocks )\n"
                            "\nVerifies blockchain database.\n"
                            "\nArguments:\n"
                            "1. checklevel   (numeric, optional, 0-4, default=" +
                            strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
                                                           "2. numblocks    (numeric, optional, default=" +
                            strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
                                                           "\nResult:\n"
                                                           "true|false       (boolean) Verified or not\n"
                                                           "\nExamples:\n" +
                            HelpExampleCli("verifychain", "") + HelpExampleRpc("verifychain", ""));

    LOCK(cs_main);

    if (params.size() > 0)
        nCheckLevel = params[0].get_int();
    if (params.size() > 1)
        nCheckDepth = params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip, nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex *pindex, const Consensus::Params &consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    switch (version)
    {
    case 2:
        activated = pindex->nHeight >= consensusParams.BIP34Height;
        break;
    case 3:
        activated = pindex->nHeight >= consensusParams.BIP66Height;
        break;
    case 4:
        activated = pindex->nHeight >= consensusParams.BIP65Height;
        break;
    }
    rv.push_back(Pair("status", activated));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name,
    int version,
    CBlockIndex *pindex,
    const Consensus::Params &consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams)));
    return rv;
}

static UniValue BIP9SoftForkDesc(const Consensus::Params &consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState)
    {
    case THRESHOLD_DEFINED:
        rv.push_back(Pair("status", "defined"));
        break;
    case THRESHOLD_STARTED:
        rv.push_back(Pair("status", "started"));
        break;
    case THRESHOLD_LOCKED_IN:
        rv.push_back(Pair("status", "locked_in"));
        break;
    case THRESHOLD_ACTIVE:
        rv.push_back(Pair("status", "active"));
        break;
    case THRESHOLD_FAILED:
        rv.push_back(Pair("status", "failed"));
        break;
    }
    if (THRESHOLD_STARTED == thresholdState)
    {
        rv.push_back(Pair("bit", consensusParams.vDeployments[id].bit));
    }
    rv.push_back(Pair("startTime", consensusParams.vDeployments[id].nStartTime));
    rv.push_back(Pair("timeout", consensusParams.vDeployments[id].nTimeout));
    return rv;
}

// bip135 begin
static UniValue BIP135ForkDesc(const Consensus::Params &consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("bit", (int)id));
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState)
    {
    case THRESHOLD_DEFINED:
        rv.push_back(Pair("status", "defined"));
        break;
    case THRESHOLD_STARTED:
        rv.push_back(Pair("status", "started"));
        break;
    case THRESHOLD_LOCKED_IN:
        rv.push_back(Pair("status", "locked_in"));
        break;
    case THRESHOLD_ACTIVE:
        rv.push_back(Pair("status", "active"));
        break;
    case THRESHOLD_FAILED:
        rv.push_back(Pair("status", "failed"));
        break;
    }
    rv.push_back(Pair("startTime", consensusParams.vDeployments[id].nStartTime));
    rv.push_back(Pair("timeout", consensusParams.vDeployments[id].nTimeout));
    rv.push_back(Pair("windowsize", consensusParams.vDeployments[id].windowsize));
    rv.push_back(Pair("threshold", consensusParams.vDeployments[id].threshold));
    rv.push_back(Pair("minlockedblocks", consensusParams.vDeployments[id].minlockedblocks));
    rv.push_back(Pair("minlockedtime", consensusParams.vDeployments[id].minlockedtime));
    return rv;
}
// bip135 end

UniValue getblockchaininfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding block chain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",        (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,         (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,        (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\", (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,     (numeric) the current difficulty\n"
            "  \"mediantime\": xxxxxx,     (numeric) median time for the current best block\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"chainwork\": \"xxxx\"     (string) total amount of work in active chain, in hexadecimal\n"
            "  \"pruned\": xx,             (boolean) if the blocks are subject to pruning\n"
            "  \"pruneheight\": xxxxxx,    (numeric) lowest-height complete block stored\n"
            "  \"softforks\": [            (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of softfork\n"
            "        \"version\": xx,         (numeric) block version\n"
            "        \"reject\": {            (object) progress toward rejecting pre-softfork blocks\n"
            "           \"status\": xx,       (boolean) true if threshold reached\n"
            "        },\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": {          (object) status of BIP9 softforks in progress\n"
            "     \"xxxx\" : {                (string) name of the softfork\n"
            "        \"status\": \"xxxx\",    (string) one of \"defined\", \"started\", \"lockedin\", \"active\", "
            "\"failed\"\n"
            "        \"bit\": xx,             (numeric) the bit, 0-28, in the block version field used to signal this "
            "soft fork\n"
            "        \"startTime\": xx,       (numeric) the minimum median time past of a block at which the bit gains "
            "its meaning\n"
            "        \"timeout\": xx          (numeric) the median time past of a block at which the deployment is "
            "considered failed if not yet locked in\n"
            "     }\n"
            "  }\n"
            // bip135 begin
            "  \"bip135_forks\": {            (object) status of BIP135 forks in progress\n"
            "     \"xxxx\" : {                (string) name of the fork\n"
            "        \"status\": \"xxxx\",      (string) one of \"defined\", \"started\", \"locked_in\", \"active\", "
            "\"failed\"\n"
            "        \"bit\": xx,             (numeric) the bit (0-28) in the block version field used to signal this "
            "fork (only for \"started\" status)\n"
            "        \"startTime\": xx,       (numeric) the minimum median time past of a block at which the bit gains "
            "its meaning\n"
            "        \"windowsize\": xx,      (numeric) the number of blocks over which the fork status is tallied\n"
            "        \"threshold\": xx,       (numeric) the number of blocks in a window that must signal for fork to "
            "lock in\n"
            "        \"minlockedblocks\": xx, (numeric) the minimum number of blocks to elapse after lock-in and "
            "before activation\n"
            "        \"minlockedtime\": xx,   (numeric) the minimum number of seconds to elapse after median time past "
            "of lock-in until activation\n"
            "     }\n"
            "  }\n"
            // bip135 end
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockchaininfo", "") + HelpExampleRpc("getblockchaininfo", ""));

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain", Params().NetworkIDString()));
    obj.push_back(Pair("blocks", (int)chainActive.Height()));
    obj.push_back(Pair("headers", pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash", chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty", (double)GetDifficulty()));
    obj.push_back(Pair("mediantime", (int64_t)chainActive.Tip()->GetMedianTimePast()));
    obj.push_back(Pair(
        "verificationprogress", Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip())));
    obj.push_back(Pair("chainwork", chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("pruned", fPruneMode));

    const Consensus::Params &consensusParams = Params().GetConsensus();
    CBlockIndex *tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    UniValue bip135_forks(UniValue::VOBJ); // bip135 added
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    // bip135 begin : add all the configured forks
    for (int i = 0; i < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++)
    {
        Consensus::DeploymentPos bit = static_cast<Consensus::DeploymentPos>(i);
        const struct ForkDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[bit];
        if (IsConfiguredDeployment(consensusParams, bit))
        {
            bip9_softforks.push_back(Pair(vbinfo.name, BIP9SoftForkDesc(consensusParams, bit)));
            bip135_forks.push_back(Pair(vbinfo.name, BIP135ForkDesc(consensusParams, bit)));
        }
    }

    obj.push_back(Pair("softforks", softforks));
    obj.push_back(Pair("bip9_softforks", bip9_softforks));
    // to maintain backward compat initially, we introduce a new list for the full BIP135 data
    obj.push_back(Pair("bip135_forks", bip135_forks));
    // bip135 end

    if (fPruneMode)
    {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA))
            block = block->pprev;

        if (block != nullptr)
            obj.push_back(Pair("pruneheight", block->nHeight));
    }
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex *a, const CBlockIndex *b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
            return (a->nHeight > b->nHeight);

        return a < b;
    }
};

UniValue getchaintips(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,         (numeric) height of the chain tip\n"
            "    \"chainwork\": \"xxxx\"     (string) total amount of work in this chain, in hexadecimal\n"
            "    \"hash\": \"xxxx\",         (string) block hash of the tip\n"
            "    \"branchlen\": 0          (numeric) length of branch connecting the tip to the main chain (zero for "
            "main chain)\n"
            "    \"status\": \"xxxx\"        (string) status of the chain (active, valid-fork, valid-headers, "
            "headers-only, invalid)\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully "
            "validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n" +
            HelpExampleCli("getchaintips", "") + HelpExampleRpc("getchaintips", ""));

    LOCK(cs_main);

    /*
     * Idea:  the set of chain tips is chainActive.tip, plus orphan blocks which do not have another orphan building off
     * of them.
     * Algorithm:
     *  - Make one pass through mapBlockIndex, picking out the orphan blocks, and also storing a set of the orphan
     * block's pprev pointers.
     *  - Iterate through the orphan blocks. If the block isn't pointed to by another orphan, it is a chain tip.
     *  - add chainActive.Tip()
     */
    std::set<const CBlockIndex *, CompareBlocksByHeight> setTips;
    std::set<const CBlockIndex *> setOrphans;
    std::set<const CBlockIndex *> setPrevs;

    for (const PAIRTYPE(const uint256, CBlockIndex *) & item : mapBlockIndex)
    {
        if (!chainActive.Contains(item.second))
        {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    for (std::set<const CBlockIndex *>::iterator it = setOrphans.begin(); it != setOrphans.end(); ++it)
    {
        if (setPrevs.erase(*it) == 0)
        {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for (const CBlockIndex *block : setTips)
    {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("chainwork", block->nChainWork.GetHex()));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));

        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        obj.push_back(Pair("branchlen", branchLen));

        string status;
        if (chainActive.Contains(block))
        {
            // This block is part of the currently active chain.
            status = "active";
        }
        else if (block->nStatus & BLOCK_FAILED_MASK)
        {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        }
        else if (block->nChainTx == 0)
        {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        }
        else if (block->IsValid(BLOCK_VALID_SCRIPTS))
        {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block
            // once, but was reorganized.
            status = "valid-fork";
        }
        else if (block->IsValid(BLOCK_VALID_TREE))
        {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the
            // most-work chain.
            status = "valid-headers";
        }
        else
        {
            // No clue.
            status = "unknown";
        }
        obj.push_back(Pair("status", status));

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t)mempool.size()));
    ret.push_back(Pair("bytes", (int64_t)mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t)mempool.DynamicMemoryUsage()));
    size_t maxmempool = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.push_back(Pair("maxmempool", (int64_t)maxmempool));
    ret.push_back(Pair("mempoolminfee", ValueFromAmount(mempool.GetMinFee(maxmempool).GetFeePerK())));
    try
    {
        ret.push_back(Pair("tps", boost::lexical_cast<double>(strprintf("%.2f", mempool.TransactionsPerSecond()))));
    }
    catch (boost::bad_lexical_cast &)
    {
        ret.push_back(Pair("tps", "N/A"));
    }

    return ret;
}

UniValue getmempoolinfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getmempoolinfo\n"
                            "\nReturns details on the active state of the TX memory pool.\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"size\": xxxxx,               (numeric) Current tx count\n"
                            "  \"bytes\": xxxxx,              (numeric) Sum of all tx sizes\n"
                            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
                            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
                            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee for tx to be accepted\n"
                            "  \"tps\": xxxxx                 (numeric) Transactions per second accepted\n"
                            "}\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getmempoolinfo", "") + HelpExampleRpc("getmempoolinfo", ""));

    return mempoolInfoToJSON();
}

UniValue orphanpoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t)orphanpool.GetOrphanPoolSize()));
    ret.push_back(Pair("bytes", (int64_t)orphanpool.GetOrphanPoolBytes()));

    return ret;
}

UniValue getorphanpoolinfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getorphanpoolinfo\n"
                            "\nReturns details on the active state of the TX orphan pool.\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"size\": xxxxx,               (numeric) Current tx count\n"
                            "  \"bytes\": xxxxx,              (numeric) Sum of all tx sizes\n"
                            "}\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getorphanpoolinfo", "") + HelpExampleRpc("getorphanoolinfo", ""));

    return orphanpoolInfoToJSON();
}

UniValue invalidateblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("invalidateblock \"hash\"\n"
                            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
                            "\nArguments:\n"
                            "1. hash   (string, required) the hash of the block to mark as invalid\n"
                            "\nResult:\n"
                            "\nExamples:\n" +
                            HelpExampleCli("invalidateblock", "\"blockhash\"") +
                            HelpExampleRpc("invalidateblock", "\"blockhash\""));

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex *pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, Params().GetConsensus(), pblockindex);
    }

    if (state.IsValid())
    {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid())
    {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "reconsiderblock \"hash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("reconsiderblock", "\"blockhash\"") + HelpExampleRpc("reconsiderblock", "\"blockhash\""));

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex *pblockindex = mapBlockIndex[hash];
        ReconsiderBlock(state, pblockindex);
    }

    if (state.IsValid())
    {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid())
    {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    uiInterface.NotifyBlockTip(false, chainActive.Tip());

    return NullUniValue;
}

UniValue rollbackchain(const UniValue &params, bool fHelp)
{
    // In case of operator error, limit the rollback to 100 blocks
    uint32_t nLimit = 100;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "rollbackchain \"blockheight\"\n"
            "\nRolls back the blockchain to the height indicated.\n"
            "\nArguments:\n"
            "1. blockheight   (int, required) the height that you want to roll the chain \
                            back to (only maxiumum rollback of " +
            std::to_string(nLimit) + " blocks allowed)\n"
                                     "2. override      (boolean, optional, default=false) rollback more than the \
                            allowed default limit of " +
            std::to_string(nLimit) + " blocks)\n"
                                     "\nResult:\n"
                                     "\nExamples:\n" +
            HelpExampleCli("rollbackchain", "\"501245\"") + HelpExampleCli("rollbackchain", "\"495623 true\"") +
            HelpExampleRpc("rollbackchain", "\"blockheight\""));

    int nRollBackHeight = params[0].get_int();
    bool fOverride = false;
    if (params.size() > 1)
        fOverride = params[1].get_bool();

    LOCK(cs_main);
    uint32_t nRollBack = chainActive.Height() - nRollBackHeight;
    if (nRollBack > nLimit && !fOverride)
        throw runtime_error("You are attempting to rollback the chain by " + std::to_string(nRollBack) +
                            " blocks, however the limit is " + std::to_string(nLimit) + " blocks. Set " +
                            "the override to true if you want rollback more than the default");

    while (chainActive.Height() > nRollBackHeight)
    {
        // save the current tip
        CBlockIndex *pindex = chainActive.Tip();

        CValidationState state;
        // Disconnect the tip and by setting the third param (fRollBack) to true we avoid having to resurrect
        // the transactions from the block back into the mempool, which saves a great deal of time.
        if (!DisconnectTip(state, Params().GetConsensus(), true))
            throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());

        if (!state.IsValid())
            throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());

        // Invalidate the now previous block tip after it was diconnected so that the chain will not reconnect
        // if another block arrives.
        InvalidateBlock(state, Params().GetConsensus(), pindex);
        if (!state.IsValid())
        {
            throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
        }

        uiInterface.NotifyBlockTip(false, chainActive.Tip());
    }
    return NullUniValue;
}

static const CRPCCommand commands[] = {
    //  category              name                      actor (function)         okSafeMode
    //  --------------------- ------------------------  -----------------------  ----------
    {"blockchain", "getblockchaininfo", &getblockchaininfo, true},
    {"blockchain", "getbestblockhash", &getbestblockhash, true}, {"blockchain", "getblockcount", &getblockcount, true},
    {"blockchain", "getblock", &getblock, true}, {"blockchain", "getblockhash", &getblockhash, true},
    {"blockchain", "getblockheader", &getblockheader, true}, {"blockchain", "getchaintips", &getchaintips, true},
    {"blockchain", "getdifficulty", &getdifficulty, true}, {"blockchain", "getmempoolinfo", &getmempoolinfo, true},
    {"blockchain", "getorphanpoolinfo", &getorphanpoolinfo, true},
    {"blockchain", "getrawmempool", &getrawmempool, true}, {"blockchain", "gettxout", &gettxout, true},
    {"blockchain", "gettxoutsetinfo", &gettxoutsetinfo, true}, {"blockchain", "verifychain", &verifychain, true},

    /* Not shown in help */
    {"hidden", "invalidateblock", &invalidateblock, true}, {"hidden", "reconsiderblock", &reconsiderblock, true},
    {"hidden", "rollbackchain", &rollbackchain, true},
};

void RegisterBlockchainRPCCommands(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}
