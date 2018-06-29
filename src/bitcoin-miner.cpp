// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "allowed_args.h"
#include "arith_uint256.h"
#include "chainparamsbase.h"
#include "fs.h"
#include "hash.h"
#include "primitives/block.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "streams.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"

#include <boost/thread.hpp>

#include <cstdlib>
#include <stdio.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <univalue.h>

using namespace std;

// BU add lockstack stuff here for bitcoin-miner, because I need to carefully
// order it in globals.cpp for bitcoind and bitcoin-qt
boost::mutex dd_mutex;
std::map<std::pair<void *, void *>, LockStack> lockorders;
boost::thread_specific_ptr<LockStack> lockstack;

// Internal miner
//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
bool static ScanHash(const CBlockHeader *pblock, uint32_t &nNonce, uint256 *phash)
{
    // Write the first 76 bytes of the block header to a double-SHA256 state.
    CHash256 hasher;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *pblock;
    assert(ss.size() == 80);
    hasher.Write((unsigned char *)&ss[0], 76);

    while (true)
    {
        nNonce++;

        // Write the last 4 bytes of the block header (the nonce) to a copy of
        // the double-SHA256 state, and compute the result.
        CHash256(hasher).Write((unsigned char *)&nNonce, 4).Finalize((unsigned char *)phash);

        // Return the nonce if the hash has at least some zero bits,
        // caller will check if it has enough to reach the target
        if (((uint16_t *)phash)[15] == 0)
            return true;

        // If nothing found after trying for a while, return -1
        if ((nNonce & 0xfff) == 0)
            return false;
    }
}


class BitcoinMinerArgs : public AllowedArgs::BitcoinCli
{
public:
    BitcoinMinerArgs(CTweakMap *pTweaks = nullptr)
    {
        addHeader(_("Mining options:"))
            .addArg("blockversion=<n>", ::AllowedArgs::requiredInt,
                _("Set the block version number. For testing only.  Value must be an integer"))
            .addArg("cpus=<n>", ::AllowedArgs::requiredInt,
                _("Number of cpus to use for mining (default: 1).  Value must be an integer"))
            .addArg("duration=<n>", ::AllowedArgs::requiredInt,
                _("Number of seconds to mine a particular block candidate (default: 30). Value must be an integer"))
            .addArg("nblocks=<n>", ::AllowedArgs::requiredInt,
                _("Number of blocks to mine (default: mine forever / -1). Value must be an integer"));
    }
};


static CBlockHeader CpuMinerJsonToHeader(const UniValue &params)
{
    // Does not set hashMerkleRoot (Does not exist in Mining-Candidate params).
    CBlockHeader blockheader;

    // nVersion
    blockheader.nVersion = params["version"].get_int();

    // hashPrevBlock
    string tmpstr = params["prevhash"].get_str();
    std::vector<unsigned char> vec = ParseHex(tmpstr);
    std::reverse(vec.begin(), vec.end()); // sent reversed
    blockheader.hashPrevBlock = uint256(vec);

    // nTime:
    blockheader.nTime = params["time"].get_int();

    // nBits
    {
        std::stringstream ss;
        ss << std::hex << params["nBits"].get_str();
        ss >> blockheader.nBits;
    }

    return blockheader;
}


static void CalculateNextMerkleRoot(uint256 &merkle_root, const uint256 &merkle_branch)
{
    // Append a branch to the root. Double SHA256 the whole thing:
    uint256 hash;
    CHash256()
        .Write(merkle_root.begin(), merkle_root.size())
        .Write(merkle_branch.begin(), merkle_branch.size())
        .Finalize(hash.begin());
    merkle_root = hash;
}

static uint256 CalculateMerkleRoot(uint256 &coinbase_hash, const std::vector<uint256> &merkleproof)
{
    uint256 merkle_root = coinbase_hash;
    for (unsigned int i = 0; i < merkleproof.size(); i++)
    {
        CalculateNextMerkleRoot(merkle_root, merkleproof[i]);
    }
    return merkle_root;
}

static bool CpuMineBlockHasher(CBlockHeader *pblock,
    vector<unsigned char> &coinbaseBytes,
    const std::vector<uint256> &merkleproof)
{
    uint32_t nExtraNonce = std::rand();
    uint32_t nNonce = pblock->nNonce;
    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
    bool found = false;
    int ntries = 10;
    unsigned char *pbytes = (unsigned char *)&coinbaseBytes[0];

    while (!found)
    {
        // hashMerkleRoot:
        {
            ++nExtraNonce;
            // 48 - next in arr after Height. (Height in coinbase required for block.version=2):
            *(unsigned int *)(pbytes + 48) = nExtraNonce;
            uint256 hash;
            CHash256().Write(pbytes, coinbaseBytes.size()).Finalize(hash.begin());

            pblock->hashMerkleRoot = CalculateMerkleRoot(hash, merkleproof);
        }

        //
        // Search
        //
        uint256 hash;
        while (!found)
        {
            // Check if something found
            if (ScanHash(pblock, nNonce, &hash))
            {
                if (UintToArith256(hash) <= hashTarget)
                {
                    // Found a solution
                    pblock->nNonce = nNonce;
                    found = true;
                    printf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex().c_str(),
                        hashTarget.GetHex().c_str());
                    break;
                }
                else
                {
                    if (ntries-- < 1)
                    {
                        pblock->nNonce = nNonce; // report the last nonce checked for accounting
                        return false; // Give up leave
                    }
                }
            }
        }
    }

    return found;
}

static double GetDifficulty(uint64_t nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff = (double)0x0000ffff / (double)(nBits & 0x00ffffff);

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

static UniValue CpuMineBlock(unsigned int searchDuration, const UniValue &params, bool &found)
{
    UniValue tmp(UniValue::VOBJ);
    UniValue ret(UniValue::VARR);
    string tmpstr;
    std::vector<uint256> merkleproof;
    CBlockHeader header;
    vector<unsigned char> coinbaseBytes(ParseHex(params["coinbase"].get_str()));

    found = false;

    // re-create merkle branches:
    {
        UniValue uvMerkleproof = params["merkleProof"];
        for (unsigned int i = 0; i < uvMerkleproof.size(); i++)
        {
            tmpstr = uvMerkleproof[i].get_str();
            std::vector<unsigned char> mbr = ParseHex(tmpstr);
            std::reverse(mbr.begin(), mbr.end());
            merkleproof.push_back(uint256(mbr));
        }
    }

    header = CpuMinerJsonToHeader(params);

    // Set the version (only to test):
    {
        int blockversion = GetArg("-blockversion", header.nVersion);
        if (blockversion != header.nVersion)
            printf("Force header.nVersion to %d\n", blockversion);
        header.nVersion = blockversion;
    }

    uint32_t startNonce = header.nNonce = std::rand();

    printf("Mining: id: %lx parent: %s bits: %x difficulty: %3.2f time: %d\n", (uint64_t)params["id"].get_int64(),
        header.hashPrevBlock.ToString().c_str(), header.nBits, GetDifficulty(header.nBits), header.nTime);

    int64_t start = GetTime();
    while ((GetTime() < start + searchDuration) && !found)
    {
        // When mining mainnet, you would normally want to advance the time to keep the block time as close to the
        // real time as possible.  However, this CPU miner is only useful on testnet and in testnet the block difficulty
        // resets to 1 after 20 minutes.  This will cause the block's difficulty to mismatch the expected difficulty
        // and the block will be rejected.  So do not advance time (let it be advanced by bitcoind every time we
        // request a new block).
        // header.nTime = (header.nTime < GetTime()) ? GetTime() : header.nTime;
        found = CpuMineBlockHasher(&header, coinbaseBytes, merkleproof);
    }

    // Leave if not found:
    if (!found)
    {
        printf("Checked %d possibilities\n", header.nNonce - startNonce);
        return ret;
    }

    printf("Solution! Checked %d possibilities\n", header.nNonce - startNonce);

    tmpstr = HexStr(coinbaseBytes.begin(), coinbaseBytes.end());
    tmp.push_back(Pair("coinbase", tmpstr));
    tmp.push_back(Pair("id", params["id"]));
    tmp.push_back(Pair("time", UniValue(header.nTime))); // Optional. We have changed so must send.
    tmp.push_back(Pair("nonce", UniValue(header.nNonce)));
    tmp.push_back(Pair("version", UniValue(header.nVersion))); // Optional. We may have changed so sending.
    ret.push_back(tmp);

    return ret;
}

static UniValue RPCSubmitSolution(const UniValue &solution, int &nblocks)
{
    UniValue reply = CallRPC("submitminingsolution", solution);

    const UniValue &error = find_value(reply, "error");

    if (!error.isNull())
    {
        fprintf(stderr, "Block Candidate submission error: %d %s\n", error["code"].get_int(),
            error["message"].get_str().c_str());
        return reply;
    }

    const UniValue &result = find_value(reply, "result");

    if (result.isStr())
    {
        fprintf(stderr, "Block Candidate rejected. Error: %s\n", result.get_str().c_str());
        // Print some debug info if the block is rejected
        UniValue dbg = solution[0].get_obj();
        fprintf(stderr, "id: %ld  time: %d  nonce: %d  version: 0x%x\n", dbg["id"].get_int64(),
            (uint32_t)dbg["time"].get_int64(), (uint32_t)dbg["nonce"].get_int64(), (uint32_t)dbg["version"].get_int());
        fprintf(stderr, "coinbase: %s\n", dbg["coinbase"].get_str().c_str());
    }
    else
    {
        if (result.isNull())
        {
            printf("Block Candidate accepted.\n");
            if (nblocks > 0)
                nblocks--; // Processed a block
        }
        else
        {
            fprintf(stderr, "Unknown \"submitminingsolution\" Error.\n");
        }
    }

    return reply;
}

int CpuMiner(void)
{
    int searchDuration = GetArg("-duration", 30);
    int nblocks = GetArg("-nblocks", -1); //-1 mine forever

    UniValue mineresult;
    bool found = false;

    if (0 == nblocks)
    {
        printf("Nothing to do for zero (0) blocks\n");
        return 0;
    }

    while (0 != nblocks)
    {
        UniValue reply;
        UniValue result;
        string strPrint;
        int nRet = 0;
        try
        {
            // Execute and handle connection failures with -rpcwait
            const bool fWait = true;
            do
            {
                try
                {
                    UniValue params;
                    if (found)
                    {
                        // Submit the solution.
                        // Called here so all exceptions are handled properly below.
                        reply = RPCSubmitSolution(mineresult, nblocks);
                        if (nblocks == 0)
                            return 0; // Done mining exit program
                        found = false; // Mine again
                    }

                    if (!found)
                    {
                        reply = CallRPC("getminingcandidate", params);
                    }

                    // Parse reply
                    result = find_value(reply, "result");
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
                catch (const CConnectionFailed &c)
                {
                    if (fWait)
                    {
                        printf("Warning: %s\n", c.what());
                        MilliSleep(1000);
                    }
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
            PrintExceptionContinue(NULL, "CommandLineRPC()");
            throw;
        }

        if (strPrint != "")
        {
            if (nRet != 0)
                fprintf(stderr, "%s\n", strPrint.c_str());
            // Actually do some mining
            if (result.isNull())
            {
                MilliSleep(1000);
            }
            else
            {
                found = false;
                mineresult = CpuMineBlock(searchDuration, result, found);
                if (!found)
                {
                    // printf("Mining did not succeed\n");
                    mineresult.setNull();
                }
                // The result is sent to bitcoind above when the loop gets to it.
                // See:   RPCSubmitSolution(mineresult,nblocks);
                // This is so RPC Exceptions are handled in one place.
            }
        }
    }
    return 0;
}

void static MinerThread()
{
    while (1)
    {
        try
        {
            CpuMiner();
        }
        catch (const std::exception &e)
        {
            PrintExceptionContinue(&e, "CommandLineRPC()");
        }
        catch (...)
        {
            PrintExceptionContinue(NULL, "CommandLineRPC()");
        }
    }
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
        std::string appname("bitcoin-miner");
        std::string usage = "\n" + _("Usage:") + "\n" + "  " + appname + " [options] " + "\n";
        int ret = AppInitRPC(usage, BitcoinMinerArgs(), argc, argv);
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
        PrintExceptionContinue(NULL, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int nThreads = GetArg("-cpus", 1);
    boost::thread_group minerThreads;
    for (int i = 0; i < nThreads - 1; i++)
        minerThreads.create_thread(MinerThread);

    int ret = EXIT_FAILURE;
    try
    {
        ret = CpuMiner();
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CommandLineRPC()");
    }
    catch (...)
    {
        PrintExceptionContinue(NULL, "CommandLineRPC()");
    }
    return ret;
}
