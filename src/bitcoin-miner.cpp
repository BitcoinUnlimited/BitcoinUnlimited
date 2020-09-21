// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "allowed_args.h"
#include "arith_uint256.h"
#include "chainparamsbase.h"
#include "fs.h"
#include "hashwrapper.h"
#include "primitives/block.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "streams.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"

#include <cstdlib>
#include <functional>
#include <random>
#include <stdio.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <univalue.h>


// below two require C++11
#include <functional>
#include <random>

#ifdef DEBUG_LOCKORDER
std::atomic<bool> lockdataDestructed{false};
LockData lockdata;
#endif

// Lambda used to generate entropy, per-thread (see CpuMiner, et al below)
typedef std::function<uint32_t(void)> RandFunc;

using namespace std;

// Internal miner
//
// ScanHash increments nonces looking for a hash with at least some zero bits.
// If found, it returns out and the caller is responsible for verifying if
// the generated hash is below the difficulty target. The nonce is usually
// preserved between calls, however periodically calling code rebuilds the block
// and nNonce starts over at a random value.
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
    }
}


class BitcoinMinerArgs : public AllowedArgs::BitcoinCli
{
public:
    BitcoinMinerArgs(CTweakMap *pTweaks = nullptr)
    {
        addHeader(_("Mining options:"))
            .addArg("blockversion=<n>", ::AllowedArgs::requiredInt,
                _("Set the block version number. For testing only. Value must be an integer"))
            .addArg("cpus=<n>", ::AllowedArgs::requiredInt,
                _("Number of cpus to use for mining (default: 1). Value must be an integer"))
            .addArg("duration=<n>", ::AllowedArgs::requiredInt,
                _("Number of seconds to mine a particular block candidate (default: 30). Value must be an integer"))
            .addArg("nblocks=<n>", ::AllowedArgs::requiredInt,
                _("Number of blocks to mine (default: mine forever / -1). Value must be an integer"))
            .addArg("coinbasesize=<n>", ::AllowedArgs::requiredInt,
                _("Get a fixed size coinbase Tx (default: do not use / 0). Value must be an integer"))
            // requiredAmount here validates a float
            .addArg("maxdifficulty=<f>", ::AllowedArgs::requiredAmount,
                _("Set the maximum difficulty (default: no maximum) we will mine. If difficulty exceeds this value we "
                  "sleep and poll every <duration> seconds until difficulty drops below this threshold. Value must be "
                  "a float or integer"))
            .addArg("address=<string>", ::AllowedArgs::requiredStr,
                _("The address to send the newly generated bitcoin to. If omitted, will default to an address in the "
                  "bitcoin daemon's wallet."));
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
    const std::vector<uint256> &merkleproof,
    const RandFunc &randFunc)
{
    uint32_t nExtraNonce = randFunc(); // Grab random 4-bytes from thread-safe generator we were passed
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
            *(uint32_t *)(pbytes + 48) = nExtraNonce;
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

// trvially-constructible/copyable info for use in CpuMineBlock below to check if mining a stale block
struct BlkInfo
{
    uint64_t prevCheapHash, nBits;
};
// Thread-safe version of above for the shared variable. We do it this way
// because std::atomic<struct> isn't always available on all platforms.
class SharedBlkInfo : protected BlkInfo
{
    mutable CCriticalSection lock;

public:
    void store(const BlkInfo &o)
    {
        LOCK(lock);
        prevCheapHash = o.prevCheapHash;
        nBits = o.nBits;
    }
    bool operator==(const BlkInfo &o) const
    {
        LOCK(lock);
        return prevCheapHash == o.prevCheapHash && nBits == o.nBits;
    }
};
// shared variable: used to inform all threads when the latest block or difficulty has changed
static SharedBlkInfo sharedBlkInfo;

static UniValue CpuMineBlock(unsigned int searchDuration, const UniValue &params, bool &found, const RandFunc &randFunc)
{
    UniValue ret(UniValue::VARR);
    CBlockHeader header;
    const double maxdiff = GetDoubleArg("-maxdifficulty", 0.0);
    searchDuration *= 1000; // convert to millis

    found = false;

    header = CpuMinerJsonToHeader(params);

    // save the prev block CheapHash & current difficulty to the global shared variable right away: this will
    // potentially signal to other threads to return early if they are still mining on top of an old block (assumption
    // here is that this block is the latest result from the RPC server, which is true 99.99999% of the time.)
    const BlkInfo blkInfo = {header.hashPrevBlock.GetCheapHash(), header.nBits};
    sharedBlkInfo.store(blkInfo);

    // first check difficulty, and abort if it's lower than maxdifficulty from CLI
    const double difficulty = GetDifficulty(header.nBits);

    if (maxdiff > 0.0 && difficulty > maxdiff)
    {
        printf("Current difficulty: %3.2f > maxdifficulty: %3.2f, sleeping for %d seconds...\n", difficulty, maxdiff,
            searchDuration / 1000);
        MilliSleep(searchDuration);
        return ret;
    }

    // ok, difficulty check passed or not applicable, proceed
    UniValue tmp(UniValue::VOBJ);
    string tmpstr;
    std::vector<uint256> merkleproof;
    vector<unsigned char> coinbaseBytes(ParseHex(params["coinbase"].get_str()));


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

    // Set the version (only to test):
    {
        int blockversion = GetArg("-blockversion", header.nVersion);
        if (blockversion != header.nVersion)
            printf("Force header.nVersion to %d\n", blockversion);
        header.nVersion = blockversion;
    }

    uint32_t startNonce = header.nNonce = randFunc();

    printf("Mining: id: %x parent: %s bits: %x difficulty: %3.2f time: %d\n", (unsigned int)params["id"].get_int64(),
        header.hashPrevBlock.ToString().c_str(), header.nBits, difficulty, header.nTime);

    int64_t start = GetTimeMillis();
    while ((GetTimeMillis() < start + searchDuration) && !found && sharedBlkInfo == blkInfo)
    {
        // When mining mainnet, you would normally want to advance the time to keep the block time as close to the
        // real time as possible.  However, this CPU miner is only useful on testnet and in testnet the block difficulty
        // resets to 1 after 20 minutes.  This will cause the block's difficulty to mismatch the expected difficulty
        // and the block will be rejected.  So do not advance time (let it be advanced by bitcoind every time we
        // request a new block).
        // header.nTime = (header.nTime < GetTime()) ? GetTime() : header.nTime;
        found = CpuMineBlockHasher(&header, coinbaseBytes, merkleproof, randFunc);
    }

    const uint32_t nChecked = header.nNonce - startNonce;

    // Leave if not found:
    if (!found)
    {
        const int64_t elapsed = GetTimeMillis() - start;
        printf("Checked %d possibilities in %ld secs, %3.3f MH/s\n", nChecked, elapsed / 1000,
            (nChecked / 1e6) / (elapsed / 1e3));
        return ret;
    }

    printf("Solution! Checked %d possibilities\n", nChecked);

    tmpstr = HexStr(coinbaseBytes.begin(), coinbaseBytes.end());
    tmp.pushKV("coinbase", tmpstr);
    tmp.pushKV("id", params["id"]);
    tmp.pushKV("time", UniValue(header.nTime)); // Optional. We have changed so must send.
    tmp.pushKV("nonce", UniValue(header.nNonce));
    tmp.pushKV("version", UniValue(header.nVersion)); // Optional. We may have changed so sending.
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
        fprintf(stderr, "id: %d  time: %d  nonce: %d  version: 0x%x\n", (unsigned int)dbg["id"].get_int64(),
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
    // Initialize random number generator lambda. This is per-thread and
    // is thread-safe.  std::rand() is not thread-safe and can result
    // in multiple threads doing redundant proof-of-work.
    std::random_device rd;
    // seed random number generator from system entropy source (implementation defined: usually HW)
    std::default_random_engine e1(rd());
    // returns a uniformly distributed random number in the inclusive range: [0, UINT_MAX]
    std::uniform_int_distribution<uint32_t> uniformGen(0);
    auto randFunc = [&](void) -> uint32_t { return uniformGen(e1); };

    int searchDuration = GetArg("-duration", 30);
    int nblocks = GetArg("-nblocks", -1); //-1 mine forever
    int coinbasesize = GetArg("-coinbasesize", 0);
    std::string address = GetArg("-address", "");

    if (coinbasesize < 0)
    {
        printf("Negative coinbasesize not reasonable/supported.\n");
        return 0;
    }

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
                    UniValue params(UniValue::VARR);
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
                        if (coinbasesize > 0)
                        {
                            params.push_back(UniValue(coinbasesize));
                        }
                        if (!address.empty())
                        {
                            if (params.empty())
                            {
                                // param[0] must be coinbaseSize:
                                // push null in position 0 to use server default coinbaseSize
                                params.push_back(UniValue());
                            }
                            // this must be in position 1
                            params.push_back(UniValue(address));
                        }
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
            PrintExceptionContinue(nullptr, "CommandLineRPC()");
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
                mineresult = CpuMineBlock(searchDuration, result, found, randFunc);
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
            PrintExceptionContinue(nullptr, "CommandLineRPC()");
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
        PrintExceptionContinue(nullptr, "AppInitRPC()");
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
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
    }
    return ret;
}
