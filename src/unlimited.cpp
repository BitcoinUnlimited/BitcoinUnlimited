// Copyright (c) 2015 G. Andrew Stone
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "unlimited.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "clientversion.h"
#include "connmgr.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "dosman.h"
#include "expedited.h"
#include "hash.h"
#include "leakybucket.h"
#include "miner.h"
#include "net.h"
#include "parallel.h"
#include "policy/policy.h"
#include "primitives/block.h"
#include "requestManager.h"
#include "rpc/server.h"
#include "stat.h"
#include "thinblock.h"
#include "timedata.h"
#include "tinyformat.h"
#include "tweak.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "version.h"

#include <atomic>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <inttypes.h>
#include <iomanip>
#include <limits>
#include <queue>

using namespace std;

extern CTxMemPool mempool; // from main.cpp
static atomic<uint64_t> nLargestBlockSeen{BLOCKSTREAM_CORE_MAX_BLOCK_SIZE}; // track the largest block we've seen
static atomic<bool> fIsChainNearlySyncd{false};
extern atomic<bool> fIsInitialBlockDownload;
extern CTweakRef<uint64_t> miningBlockSize;
extern CTweakRef<uint64_t> ebTweak;

int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

bool IsTrafficShapingEnabled();
UniValue validateblocktemplate(const UniValue &params, bool fHelp);
UniValue validatechainhistory(const UniValue &params, bool fHelp);

int64_t nLastOrphanCheck = GetTime(); // Used in EraseOrphansByTime()
uint64_t nBytesOrphanPool = 0; // Current in memory size of the orphan pool.

bool MiningAndExcessiveBlockValidatorRule(const uint64_t newExcessiveBlockSize, const uint64_t newMiningBlockSize)
{
    // The mined block size must be less then or equal too the excessive block size.
    LogPrintf("newMiningBlockSizei: %d - newExcessiveBlockSize: %d", newMiningBlockSize, newExcessiveBlockSize);
    return (newMiningBlockSize <= newExcessiveBlockSize);
}

std::string ExcessiveBlockValidator(const uint64_t &value, uint64_t *item, bool validate)
{
    if (validate)
    {
        if (!MiningAndExcessiveBlockValidatorRule(value, maxGeneratedBlock))
        {
            std::ostringstream ret;
            ret << "Sorry, your maximum mined block (" << maxGeneratedBlock
                << ") is larger than your proposed excessive size (" << value
                << ").  This would cause you to orphan your own blocks.";
            return ret.str();
        }
    }
    else // Do anything to "take" the new value
    {
        // nothing needed
    }
    return std::string();
}

std::string MiningBlockSizeValidator(const uint64_t &value, uint64_t *item, bool validate)
{
    if (validate)
    {
        if (!MiningAndExcessiveBlockValidatorRule(excessiveBlockSize, value))
        {
            std::ostringstream ret;
            ret << "Sorry, your excessive block size (" << excessiveBlockSize
                << ") is smaller than your proposed mined block size (" << value
                << ").  This would cause you to orphan your own blocks.";
            return ret.str();
        }
    }
    else // Do anything to "take" the new value
    {
        // nothing needed
    }
    return std::string();
}

std::string OutboundConnectionValidator(const int &value, int *item, bool validate)
{
    if (validate)
    {
        if ((value < 0) || (value > 10000)) // sanity check
        {
            return "Invalid Value";
        }
        if (value < *item)
        {
            return "This field cannot be reduced at run time since that would kick out existing connections";
        }
    }
    else // Do anything to "take" the new value
    {
        if (value < *item) // note that now value is the old value and *item has been set to the new.
        {
            int diff = *item - value;
            if (semOutboundAddNode) // Add the additional slots to the outbound semaphore
                for (int i = 0; i < diff; i++)
                    semOutboundAddNode->post();
        }
    }
    return std::string();
}

std::string SubverValidator(const std::string &value, std::string *item, bool validate)
{
    if (validate)
    {
        if (value.size() > MAX_SUBVERSION_LENGTH)
        {
            return (std::string("Subversion string is too long"));
        }
    }
    return std::string();
}


// Push all transactions in the mempool to another node
void UnlimitedPushTxns(CNode *dest);

int32_t UnlimitedComputeBlockVersion(const CBlockIndex *pindexPrev, const Consensus::Params &params, uint32_t nTime)
{
    if (blockVersion != 0) // BU: allow override of block version
    {
        return blockVersion;
    }

    int32_t nVersion = ComputeBlockVersion(pindexPrev, params);

    // turn BIP109 off by default by commenting this out:
    // if (nTime <= params.SizeForkExpiration())
    //          nVersion |= FORK_BIT_2MB;

    return nVersion;
}


void UpdateSendStats(CNode *pfrom, const char *strCommand, int msgSize, int64_t nTime)
{
    sendAmt += msgSize;
    std::string name("net/send/msg/");
    name.append(strCommand);
    LOCK(cs_statMap);
    CStatMap::iterator obj = statistics.find(name);
    CStatMap::iterator end = statistics.end();
    if (obj != end)
    {
        CStatBase *base = obj->second;
        if (base)
        {
            CStatHistory<uint64_t> *stat = dynamic_cast<CStatHistory<uint64_t> *>(base);
            if (stat)
                *stat << msgSize;
        }
    }
}

void UpdateRecvStats(CNode *pfrom, const std::string &strCommand, int msgSize, int64_t nTimeReceived)
{
    recvAmt += msgSize;
    std::string name = "net/recv/msg/" + strCommand;
    LOCK(cs_statMap);
    CStatMap::iterator obj = statistics.find(name);
    CStatMap::iterator end = statistics.end();
    if (obj != end)
    {
        CStatBase *base = obj->second;
        if (base)
        {
            CStatHistory<uint64_t> *stat = dynamic_cast<CStatHistory<uint64_t> *>(base);
            if (stat)
                *stat << msgSize;
        }
    }
}


std::string FormatCoinbaseMessage(const std::vector<std::string> &comments, const std::string &customComment)
{
    std::ostringstream ss;
    if (!comments.empty())
    {
        std::vector<std::string>::const_iterator it(comments.begin());
        ss << "/" << *it;
        for (++it; it != comments.end(); ++it)
            ss << "/" << *it;
        ss << "/";
    }
    std::string ret = ss.str() + minerComment;
    return ret;
}

CNodeRef FindLikelyNode(const std::string &addrName)
{
    LOCK(cs_vNodes);
    bool wildcard = (addrName.find_first_of("*?") != std::string::npos);

    BOOST_FOREACH (CNode *pnode, vNodes)
    {
        if (wildcard)
        {
            if (match(addrName.c_str(), pnode->addrName.c_str()))
                return (pnode);
        }
        else if (pnode->addrName.find(addrName) != std::string::npos)
            return (pnode);
    }
    return NULL;
}

UniValue expedited(const UniValue &params, bool fHelp)
{
    std::string strCommand;
    if (fHelp || params.size() < 2)
        throw runtime_error("expedited block|tx \"node IP addr\" on|off\n"
                            "\nRequest expedited forwarding of blocks and/or transactions from a node.\nExpedited "
                            "forwarding sends blocks or transactions to a node before the node requests them.  This "
                            "reduces latency, potentially at the expense of bandwidth.\n"
                            "\nArguments:\n"
                            "1. \"block | tx\"        (string, required) choose block to send expedited blocks, tx to "
                            "send expedited transactions\n"
                            "2. \"node ip addr\"     (string, required) The node's IP address or IP and port (see "
                            "getpeerinfo for nodes)\n"
                            "3. \"on | off\"     (string, required) Turn expedited service on or off\n"
                            "\nExamples:\n" +
                            HelpExampleCli("expedited", "block \"192.168.0.6:8333\" on") +
                            HelpExampleRpc("expedited", "\"block\", \"192.168.0.6:8333\", \"on\""));

    std::string obj = params[0].get_str();
    std::string strNode = params[1].get_str();

    CNodeRef node(FindLikelyNode(strNode));
    if (!node)
    {
        throw runtime_error("Unknown node");
    }

    uint64_t flags = 0;
    if (obj.find("block") != std::string::npos)
        flags |= EXPEDITED_BLOCKS;
    if (obj.find("blk") != std::string::npos)
        flags |= EXPEDITED_BLOCKS;
    if (obj.find("tx") != std::string::npos)
        flags |= EXPEDITED_TXNS;
    if (obj.find("transaction") != std::string::npos)
        flags |= EXPEDITED_TXNS;
    if ((flags & (EXPEDITED_BLOCKS | EXPEDITED_TXNS)) == 0)
    {
        throw runtime_error("Unknown object, give 'block' or 'transaction'");
    }

    if (params.size() >= 3)
    {
        std::string onoff = params[2].get_str();
        if (onoff == "off")
            flags |= EXPEDITED_STOP;
        if (onoff == "OFF")
            flags |= EXPEDITED_STOP;
    }

    connmgr->PushExpeditedRequest(node.get(), flags);

    return NullUniValue;
}

UniValue pushtx(const UniValue &params, bool fHelp)
{
    string strCommand;
    if (fHelp || params.size() != 1)
        throw runtime_error("pushtx \"node\"\n"
                            "\nPush uncommitted transactions to a node.\n"
                            "\nArguments:\n"
                            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
                            "\nExamples:\n" +
                            HelpExampleCli("pushtx", "\"192.168.0.6:8333\" ") +
                            HelpExampleRpc("pushtx", "\"192.168.0.6:8333\", "));

    string strNode = params[0].get_str();

    CNodeRef node(FindLikelyNode(strNode));
    if (!node)
        throw runtime_error("Unknown node");

    UnlimitedPushTxns(node.get());

    return NullUniValue;
}

void UnlimitedPushTxns(CNode *dest)
{
    // LOCK2(cs_main, pfrom->cs_filter);
    LOCK(dest->cs_filter);
    std::vector<uint256> vtxid;
    mempool.queryHashes(vtxid);
    vector<CInv> vInv;
    BOOST_FOREACH (uint256 &hash, vtxid)
    {
        CInv inv(MSG_TX, hash);
        CTransaction tx;
        bool fInMemPool = mempool.lookup(hash, tx);
        if (!fInMemPool)
            continue; // another thread removed since queryHashes, maybe...
        if ((dest->pfilter && dest->pfilter->IsRelevantAndUpdate(tx)) || (!dest->pfilter))
            vInv.push_back(inv);
        if (vInv.size() == MAX_INV_SZ)
        {
            dest->PushMessage("inv", vInv);
            vInv.clear();
        }
    }
    if (vInv.size() > 0)
        dest->PushMessage("inv", vInv);
}

void settingsToUserAgentString()
{
    BUComments.clear();

    std::stringstream ebss;
    ebss << (excessiveBlockSize / 100000);
    std::string eb = ebss.str();
    eb.insert(eb.size() - 1, ".", 1);
    if (eb.substr(0, 1) == ".")
        eb = "0" + eb;
    if (eb.at(eb.size() - 1) == '0')
        eb = eb.substr(0, eb.size() - 2);

    BUComments.push_back("EB" + eb);

    int ad_formatted;
    ad_formatted = (excessiveAcceptDepth >= 9999999 ? 9999999 : excessiveAcceptDepth);
    BUComments.push_back("AD" + boost::lexical_cast<std::string>(ad_formatted));
}

void UnlimitedSetup(void)
{
    MIN_TX_REQUEST_RETRY_INTERVAL = GetArg("-txretryinterval", DEFAULT_MIN_TX_REQUEST_RETRY_INTERVAL);
    MIN_BLK_REQUEST_RETRY_INTERVAL = GetArg("-blkretryinterval", DEFAULT_MIN_BLK_REQUEST_RETRY_INTERVAL);
    maxGeneratedBlock = GetArg("-blockmaxsize", maxGeneratedBlock);
    blockVersion = GetArg("-blockversion", blockVersion);
    excessiveBlockSize = GetArg("-excessiveblocksize", excessiveBlockSize);
    excessiveAcceptDepth = GetArg("-excessiveacceptdepth", excessiveAcceptDepth);
    LoadTweaks(); // The above options are deprecated so the same parameter defined as a tweak will override them

    if (maxGeneratedBlock > excessiveBlockSize)
    {
        LogPrintf("Reducing the maximum mined block from the configured %d to your excessive block size %d.  Otherwise "
                  "you would orphan your own blocks.\n",
            maxGeneratedBlock, excessiveBlockSize);
        maxGeneratedBlock = excessiveBlockSize;
    }

    settingsToUserAgentString();
    //  Init network shapers
    int64_t rb = GetArg("-receiveburst", DEFAULT_MAX_RECV_BURST);
    // parameter is in KBytes/sec, leaky bucket is in bytes/sec.  But if it is "off" then don't multiply
    if (rb != std::numeric_limits<long long>::max())
        rb *= 1024;
    int64_t ra = GetArg("-receiveavg", DEFAULT_AVE_RECV);
    if (ra != std::numeric_limits<long long>::max())
        ra *= 1024;
    int64_t sb = GetArg("-sendburst", DEFAULT_MAX_SEND_BURST);
    if (sb != std::numeric_limits<long long>::max())
        sb *= 1024;
    int64_t sa = GetArg("-sendavg", DEFAULT_AVE_SEND);
    if (sa != std::numeric_limits<long long>::max())
        sa *= 1024;

    receiveShaper.set(rb, ra);
    sendShaper.set(sb, sa);

    txAdded.init("memPool/txAdded");
    poolSize.init("memPool/size", STAT_OP_AVE | STAT_KEEP);
    recvAmt.init("net/recv/total");
    recvAmt.init("net/send/total");
    std::vector<std::string> msgTypes = getAllNetMessageTypes();

    for (std::vector<std::string>::const_iterator i = msgTypes.begin(); i != msgTypes.end(); ++i)
    {
        mallocedStats.push_front(new CStatHistory<uint64_t>("net/recv/msg/" + *i));
        mallocedStats.push_front(new CStatHistory<uint64_t>("net/send/msg/" + *i));
    }

    // make outbound conns modifiable by the user
    int nUserMaxOutConnections = GetArg("-maxoutconnections", DEFAULT_MAX_OUTBOUND_CONNECTIONS);
    nMaxOutConnections = std::max(nUserMaxOutConnections, 0);

    if (nMaxConnections < nMaxOutConnections)
    {
        // uiInterface.ThreadSafeMessageBox((strprintf(_("Reducing -maxoutconnections from %d to %d, because this value
        // is higher than max available connections."), nUserMaxOutConnections, nMaxConnections)),"",
        // CClientUIInterface::MSG_WARNING);
        LogPrintf(
            "Reducing -maxoutconnections from %d to %d, because this value is higher than max available connections.\n",
            nUserMaxOutConnections, nMaxConnections);
        nMaxOutConnections = nMaxConnections;
    }

    // Start Internal CPU miner
    // Generate coins in the background
    GenerateBitcoins(GetBoolArg("-gen", DEFAULT_GENERATE), GetArg("-genproclimit", DEFAULT_GENERATE_THREADS), Params());
}

FILE *blockReceiptLog = NULL;

void UnlimitedCleanup()
{
    CStatBase *obj = NULL;
    while (!mallocedStats.empty())
    {
        obj = mallocedStats.front();
        delete obj;
        mallocedStats.pop_front();
    }
}

extern void UnlimitedLogBlock(const CBlock &block, const std::string &hash, uint64_t receiptTime)
{
#if 0 // remove block logging for official release
    if (!blockReceiptLog)
        blockReceiptLog = fopen("blockReceiptLog.txt", "a");
    if (blockReceiptLog) {
        long int byteLen = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        CBlockHeader bh = block.GetBlockHeader();
        fprintf(blockReceiptLog, "%" PRIu64 ",%" PRIu64 ",%ld,%ld,%s\n", receiptTime, (uint64_t)bh.nTime, byteLen, block.vtx.size(), hash.c_str());
        fflush(blockReceiptLog);
    }
#endif
}


std::string LicenseInfo()
{
    return FormatParagraph(strprintf(_("Copyright (C) 2015-%i The Bitcoin Unlimited Developers"), COPYRIGHT_YEAR)) +
           "\n\n" +
           FormatParagraph(strprintf(_("Portions Copyright (C) 2009-%i The Bitcoin Core Developers"), COPYRIGHT_YEAR)) +
           "\n\n" +
           FormatParagraph(strprintf(_("Portions Copyright (C) 2014-%i The Bitcoin XT Developers"), COPYRIGHT_YEAR)) +
           "\n\n" + "\n" + FormatParagraph(_("This is experimental software.")) + "\n" + "\n" +
           FormatParagraph(_("Distributed under the MIT software license, see the accompanying file COPYING or "
                             "<http://www.opensource.org/licenses/mit-license.php>.")) +
           "\n" + "\n" + FormatParagraph(_("This product includes software developed by the OpenSSL Project for use in "
                                           "the OpenSSL Toolkit <https://www.openssl.org/> and cryptographic software "
                                           "written by Eric Young and UPnP software written by Thomas Bernard.")) +
           "\n";
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
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

static bool ProcessBlockFound(const CBlock *pblock, const CChainParams &chainparams)
{
    LogPrintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        pblock->GetHash().ToString(),
        pblock->nVersion,
        pblock->hashPrevBlock.ToString(),
        pblock->hashMerkleRoot.ToString(),
        pblock->nTime, pblock->nBits, pblock->nNonce,
        pblock->vtx.size());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("BitcoinMiner: generated block is stale");
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());


    {
        // We take a cs_main lock here even though it will also be aquired in ProcessNewBlock.  We want
        // to make sure we give priority to our own blocks.  This is in order to prevent any other Parallel
        // Blocks to validate when we've just mined one of our own blocks.
        LOCK(cs_main);

        // In we are mining our own block or not running in parallel for any reason
        // we must terminate any block validation threads that are currently running,
        // Unless they have more work than our own block or are processing a chain
        // that has more work than our block.
        PV->StopAllValidationThreads(pblock->GetBlockHeader().nBits);

        // Process this block the same as if we had received it from another node
        CValidationState state;
        if (!ProcessNewBlock(state, chainparams, NULL, pblock, true, NULL, false))
            return error("BitcoinMiner: ProcessNewBlock, block not accepted");
    }

    return true;
}


void static BitcoinMiner(const CChainParams &chainparams)
{
    LogPrintf("BitcoinMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("bitcoin-miner");

    unsigned int nExtraNonce = 0;

    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);

    try
    {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

        while (true)
        {
            if (chainparams.MiningRequiresPeers())
            {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                do
                {
                    bool fvNodesEmpty;
                    {
                        LOCK(cs_vNodes);
                        fvNodesEmpty = vNodes.empty();
                    }
                    if (!fvNodesEmpty && !IsInitialBlockDownload())
                        break;
                    MilliSleep(1000);
                } while (true);
            }

            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex *pindexPrev;
            {
                LOCK(cs_main);
                pindexPrev = chainActive.Tip();
            }

            unique_ptr<CBlockTemplate> pblocktemplate(
                BlockAssembler(chainparams).CreateNewBlock(coinbaseScript->reserveScript));
            if (!pblocktemplate.get())
            {
                LogPrintf("Error in BitcoinMiner: Keypool ran out, please call keypoolrefill before restarting the "
                          "mining thread\n");
                return;
            }
            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, nExtraNonce);

            LogPrintf("Running BitcoinMiner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
                ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

            //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
            uint256 hash;
            uint32_t nNonce = 0;
            while (true)
            {
                // Check if something found
                if (ScanHash(pblock, nNonce, &hash))
                {
                    if (UintToArith256(hash) <= hashTarget)
                    {
                        // Found a solution
                        pblock->nNonce = nNonce;
                        assert(hash == pblock->GetHash());

                        SetThreadPriority(THREAD_PRIORITY_NORMAL);
                        LogPrintf("BitcoinMiner:\n");
                        LogPrintf(
                            "proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
                        ProcessBlockFound(pblock, chainparams);
                        SetThreadPriority(THREAD_PRIORITY_LOWEST);
                        coinbaseScript->KeepScript();

                        // In regression test mode, stop mining after a block is found.
                        if (chainparams.MineBlocksOnDemand())
                            throw boost::thread_interrupted();

                        break;
                    }
                }

                // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                if (vNodes.empty() && chainparams.MiningRequiresPeers())
                    break;
                if (nNonce >= 0xffff0000)
                    break;
                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                    break;
                {
                    LOCK(cs_main);
                    if (pindexPrev != chainActive.Tip())
                        break;
                }

                // Update nTime every few seconds
                if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
                    break; // Recreate the block if the clock has run backwards,
                // so that we can use the correct time.
                if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks)
                {
                    // Changing pblock->nTime can change work required on testnet:
                    hashTarget.SetCompact(pblock->nBits);
                }
            }
        }
    }
    catch (const boost::thread_interrupted &)
    {
        LogPrintf("BitcoinMiner terminated\n");
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("BitcoinMiner runtime error: %s\n", e.what());
        return;
    }
}

void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams &chainparams)
{
    static boost::thread_group *minerThreads = NULL;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&BitcoinMiner, boost::cref(chainparams)));
}

// RPC read mining status
UniValue getgenerate(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getgenerate\n"
                            "\nReturn if the server is set to generate coins or not. The default is false.\n"
                            "It is set with the command line argument -gen (or " +
                            std::string(BITCOIN_CONF_FILENAME) +
                            " setting gen)\n"
                            "It can also be set with the setgenerate call.\n"
                            "\nResult\n"
                            "true|false      (boolean) If the server is set to generate coins or not\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getgenerate", "") + HelpExampleRpc("getgenerate", ""));

    LOCK(cs_main);
    return GetBoolArg("-gen", DEFAULT_GENERATE);
}

// RPC activate internal miner
UniValue setgenerate(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setgenerate generate ( genproclimit )\n"
            "\nSet 'generate' true or false to turn generation on or off.\n"
            "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
            "See the getgenerate call for the current setting.\n"
            "\nArguments:\n"
            "1. generate         (boolean, required) Set to true to turn on generation, off to turn off.\n"
            "2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for "
            "unlimited.\n"
            "\nExamples:\n"
            "\nSet the generation on with a limit of one processor\n" +
            HelpExampleCli("setgenerate", "true 1") + "\nCheck the setting\n" + HelpExampleCli("getgenerate", "") +
            "\nTurn off generation\n" + HelpExampleCli("setgenerate", "false") + "\nUsing json rpc\n" +
            HelpExampleRpc("setgenerate", "true, 1"));

    if (Params().MineBlocksOnDemand())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Use the generate method instead of setgenerate on this network");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    int nGenProcLimit = GetArg("-genproclimit", DEFAULT_GENERATE_THREADS);
    if (params.size() > 1)
    {
        nGenProcLimit = params[1].get_int();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    mapArgs["-gen"] = (fGenerate ? "1" : "0");
    mapArgs["-genproclimit"] = itostr(nGenProcLimit);
    GenerateBitcoins(fGenerate, nGenProcLimit, Params());

    return NullUniValue;
}

// End generate block internal CPU miner section

int chainContainsExcessive(const CBlockIndex *blk, unsigned int goBack)
{
    if (goBack == 0)
        goBack = excessiveAcceptDepth + EXCESSIVE_BLOCK_CHAIN_RESET;
    for (unsigned int i = 0; i < goBack; i++, blk = blk->pprev)
    {
        if (!blk)
            break; // we hit the beginning
        if (blk->nStatus & BLOCK_EXCESSIVE)
            return true;
    }
    return false;
}

int isChainExcessive(const CBlockIndex *blk, unsigned int goBack)
{
    if (goBack == 0)
        goBack = excessiveAcceptDepth;
    bool recentExcessive = false;
    bool oldExcessive = false;
    for (unsigned int i = 0; i < goBack; i++, blk = blk->pprev)
    {
        if (!blk)
            break; // we hit the beginning
        if (blk->nStatus & BLOCK_EXCESSIVE)
            recentExcessive = true;
    }

    // Once an excessive block is built upon the chain is not excessive even if more large blocks appear.
    // So look back to make sure that this is the "first" excessive block for a while
    for (unsigned int i = 0; i < EXCESSIVE_BLOCK_CHAIN_RESET; i++, blk = blk->pprev)
    {
        if (!blk)
            break; // we hit the beginning
        if (blk->nStatus & BLOCK_EXCESSIVE)
            oldExcessive = true;
    }

    return (recentExcessive && !oldExcessive);
}

bool CheckExcessive(const CBlock &block, uint64_t blockSize, uint64_t nSigOps, uint64_t nTx, uint64_t largestTx)
{
    if (blockSize > excessiveBlockSize)
    {
        LogPrintf("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64 " Sig:%d  :too many bytes\n",
            block.nVersion, block.nTime, blockSize, nTx, nSigOps);
        return true;
    }

    if (blockSize > BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
    {
        // Check transaction size to limit sighash
        if (largestTx > maxTxSize.value)
        {
            LogPrintf("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64
                      " largest TX:%d  :tx too large.  Expected less than: %d\n",
                block.nVersion, block.nTime, blockSize, nTx, largestTx, maxTxSize.value);
            return true;
        }

        // check proportional sigops
        uint64_t blockMbSize =
            1 + ((blockSize - 1) /
                    1000000); // block size in megabytes rounded up. 1-1000000 -> 1, 1000001-2000000 -> 2, etc.
        if (nSigOps > blockSigopsPerMb.value * blockMbSize)
        {
            LogPrintf("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64
                      " Sig:%d  :too many sigops.  Expected less than: %d\n",
                block.nVersion, block.nTime, blockSize, nTx, nSigOps, blockSigopsPerMb.value * blockMbSize);
            return true;
        }
    }
    else
    {
        // Within a 1MB block transactions can be 1MB, so nothing to check WRT transaction size

        // Check max sigops
        if (nSigOps > BLOCKSTREAM_CORE_MAX_BLOCK_SIGOPS)
        {
            LogPrintf("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64
                      " Sig:%d  :too many sigops.  Expected < 1MB defined constant: %d\n",
                block.nVersion, block.nTime, blockSize, nTx, nSigOps, BLOCKSTREAM_CORE_MAX_BLOCK_SIGOPS);
            return true;
        }
    }

    LogPrintf("Acceptable block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64 " Sig:%d\n", block.nVersion, block.nTime,
        blockSize, nTx, nSigOps);
    return false;
}

extern UniValue getminercomment(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getminercomment\n"
                            "\nReturn the comment that will be put into each mined block's coinbase\n transaction "
                            "after the Bitcoin Unlimited parameters."
                            "\nResult\n"
                            "  minerComment (string) miner comment\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getminercomment", "") + HelpExampleRpc("getminercomment", ""));

    return minerComment;
}

extern UniValue setminercomment(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("setminercomment\n"
                            "\nSet the comment that will be put into each mined block's coinbase\n transaction after "
                            "the Bitcoin Unlimited parameters.\n Comments that are too long will be truncated."
                            "\nExamples:\n" +
                            HelpExampleCli("setminercomment", "\"bitcoin is fundamentally emergent consensus\"") +
                            HelpExampleRpc("setminercomment", "\"bitcoin is fundamentally emergent consensus\""));

    minerComment = params[0].getValStr();
    return NullUniValue;
}

UniValue getexcessiveblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getexcessiveblock\n"
                            "\nReturn the excessive block size and accept depth."
                            "\nResult\n"
                            "  excessiveBlockSize (integer) block size in bytes\n"
                            "  excessiveAcceptDepth (integer) if the chain gets this much deeper than the excessive "
                            "block, then accept the chain as active (if it has the most work)\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getexcessiveblock", "") + HelpExampleRpc("getexcessiveblock", ""));

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("excessiveBlockSize", (uint64_t)excessiveBlockSize));
    ret.push_back(Pair("excessiveAcceptDepth", (uint64_t)excessiveAcceptDepth));
    return ret;
}

UniValue setexcessiveblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() >= 3)
        throw runtime_error("setexcessiveblock blockSize acceptDepth\n"
                            "\nSet the excessive block size and accept depth.  Excessive blocks will not be used in "
                            "the active chain or relayed until they are several blocks deep in the blockchain.  This "
                            "discourages the propagation of blocks that you consider excessively large.  However, if "
                            "the mining majority of the network builds upon the block then you will eventually accept "
                            "it, maintaining consensus."
                            "\nResult\n"
                            "  blockSize (integer) excessive block size in bytes\n"
                            "  acceptDepth (integer) if the chain gets this much deeper than the excessive block, then "
                            "accept the chain as active (if it has the most work)\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getexcessiveblock", "") + HelpExampleRpc("getexcessiveblock", ""));

    unsigned int ebs = 0;
    if (params[0].isNum())
        ebs = params[0].get_int64();
    else
    {
        string temp = params[0].get_str();
        if (temp[0] == '-')
            boost::throw_exception(boost::bad_lexical_cast());
        ebs = boost::lexical_cast<unsigned int>(temp);
    }

    std::string estr = ebTweak.Validate(ebs);
    if (!estr.empty())
        throw runtime_error(estr);
    ebTweak.Set(ebs);

    if (params[1].isNum())
        excessiveAcceptDepth = params[1].get_int64();
    else
    {
        string temp = params[1].get_str();
        if (temp[0] == '-')
            boost::throw_exception(boost::bad_lexical_cast());
        excessiveAcceptDepth = boost::lexical_cast<unsigned int>(temp);
    }

    settingsToUserAgentString();
    std::ostringstream ret;
    ret << "Excessive Block set to " << excessiveBlockSize << " bytes.  Accept Depth set to " << excessiveAcceptDepth
        << " blocks.";
    return UniValue(ret.str());
}


UniValue getminingmaxblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getminingmaxblock\n"
                            "\nReturn the max generated (mined) block size"
                            "\nResult\n"
                            "      (integer) maximum generated block size in bytes\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getminingmaxblock", "") + HelpExampleRpc("getminingmaxblock", ""));

    return maxGeneratedBlock;
}


UniValue setminingmaxblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "setminingmaxblock blocksize\n"
            "\nSet the maximum number of bytes to include in a generated (mined) block.  This command does not turn "
            "generation on/off.\n"
            "\nArguments:\n"
            "1. blocksize         (integer, required) the maximum number of bytes to include in a block.\n"
            "\nExamples:\n"
            "\nSet the generated block size limit to 8 MB\n" +
            HelpExampleCli("setminingmaxblock", "8000000") + "\nCheck the setting\n" +
            HelpExampleCli("getminingmaxblock", ""));

    uint64_t arg = 0;
    if (params[0].isNum())
        arg = params[0].get_int64();
    else
    {
        string temp = params[0].get_str();
        if (temp[0] == '-')
            boost::throw_exception(boost::bad_lexical_cast());
        arg = boost::lexical_cast<uint64_t>(temp);
    }

    // I don't want to waste time testing edge conditions where no txns can fit in a block, so limit the minimum block
    // size
    // This also fixes issues user issues where people provide the value as MB
    if (arg < 100)
        throw runtime_error("max generated block size must be greater than 100 bytes");

    std::string ret = miningBlockSize.Validate(params[0]);
    if (!ret.empty())
        throw runtime_error(ret.c_str());
    return miningBlockSize.Set(params[0]);
}

UniValue getblockversion(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("getblockversion\n"
                            "\nReturn the block version used when mining."
                            "\nResult\n"
                            "      (integer) block version number\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getblockversion", "") + HelpExampleRpc("getblockversion", ""));
    const CBlockIndex *pindex = chainActive.Tip();
    return UnlimitedComputeBlockVersion(pindex, Params().GetConsensus(), pindex->nTime);
}

UniValue setblockversion(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error("setblockversion blockVersionNumber\n"
                            "\nSet the block version number.\n"
                            "\nArguments:\n"
                            "1. blockVersionNumber         (integer, hex integer, 'BIP109', 'BASE' or 'default'.  "
                            "Required) The block version number.\n"
                            "\nExamples:\n"
                            "\nVote for 2MB blocks\n" +
                            HelpExampleCli("setblockversion", "BIP109") + "\nCheck the setting\n" +
                            HelpExampleCli("getblockversion", ""));

    uint32_t arg = 0;

    string temp = params[0].get_str();
    if (temp == "default")
    {
        arg = 0;
    }
    else if (temp == "BIP109")
    {
        arg = BASE_VERSION | FORK_BIT_2MB;
    }
    else if (temp == "BASE")
    {
        arg = BASE_VERSION;
    }
    else if ((temp[0] == '0') && (temp[1] == 'x'))
    {
        std::stringstream ss;
        ss << std::hex << (temp.c_str() + 2);
        ss >> arg;
    }
    else
    {
        arg = boost::lexical_cast<unsigned int>(temp);
    }

    blockVersion = arg;

    return NullUniValue;
}

bool IsTrafficShapingEnabled()
{
    int64_t max, avg;

    sendShaper.get(&max, &avg);
    if (avg != std::numeric_limits<long long>::max() || max != std::numeric_limits<long long>::max())
        return true;

    receiveShaper.get(&max, &avg);
    if (avg != std::numeric_limits<long long>::max() || max != std::numeric_limits<long long>::max())
        return true;

    return false;
}

UniValue gettrafficshaping(const UniValue &params, bool fHelp)
{
    string strCommand;
    if (params.size() == 1)
    {
        strCommand = params[0].get_str();
    }

    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "gettrafficshaping"
            "\nReturns the current settings for the network send and receive bandwidth and burst in kilobytes per "
            "second.\n"
            "\nArguments: None\n"
            "\nResult:\n"
            "  {\n"
            "    \"sendBurst\" : 40,   (string) The maximum send bandwidth in Kbytes/sec\n"
            "    \"sendAve\" : 30,   (string) The average send bandwidth in Kbytes/sec\n"
            "    \"recvBurst\" : 20,   (string) The maximum receive bandwidth in Kbytes/sec\n"
            "    \"recvAve\" : 10,   (string) The average receive bandwidth in Kbytes/sec\n"
            "  }\n"
            "\n NOTE: if the send and/or recv parameters do not exist, shaping in that direction is disabled.\n"
            "\nExamples:\n" +
            HelpExampleCli("gettrafficshaping", "") + HelpExampleRpc("gettrafficshaping", ""));

    UniValue ret(UniValue::VOBJ);
    int64_t max, avg;
    sendShaper.get(&max, &avg);
    if (avg != std::numeric_limits<long long>::max() || max != std::numeric_limits<long long>::max())
    {
        ret.push_back(Pair("sendBurst", max / 1024));
        ret.push_back(Pair("sendAve", avg / 1024));
    }
    receiveShaper.get(&max, &avg);
    if (avg != std::numeric_limits<long long>::max() || max != std::numeric_limits<long long>::max())
    {
        ret.push_back(Pair("recvBurst", max / 1024));
        ret.push_back(Pair("recvAve", avg / 1024));
    }
    return ret;
}

UniValue settrafficshaping(const UniValue &params, bool fHelp)
{
    bool disable = false;
    bool badArg = false;
    string strCommand;
    CLeakyBucket *bucket = NULL;
    if (params.size() >= 2)
    {
        strCommand = params[0].get_str();
        if (strCommand == "send")
            bucket = &sendShaper;
        if (strCommand == "receive")
            bucket = &receiveShaper;
        if (strCommand == "recv")
            bucket = &receiveShaper;
    }
    if (params.size() == 2)
    {
        if (params[1].get_str() == "disable")
            disable = true;
        else
            badArg = true;
    }
    else if (params.size() != 3)
        badArg = true;

    if (fHelp || badArg || bucket == NULL)
        throw runtime_error(
            "settrafficshaping \"send|receive\" \"burstKB\" \"averageKB\""
            "\nSets the network send or receive bandwidth and burst in kilobytes per second.\n"
            "\nArguments:\n"
            "1. \"send|receive\"     (string, required) Are you setting the transmit or receive bandwidth\n"
            "2. \"burst\"  (integer, required) Specify the maximum burst size in Kbytes/sec (actual max will be 1 "
            "packet larger than this number)\n"
            "2. \"average\"  (integer, required) Specify the average throughput in Kbytes/sec\n"
            "\nExamples:\n" +
            HelpExampleCli("settrafficshaping", "\"receive\" 10000 1024") +
            HelpExampleCli("settrafficshaping", "\"receive\" disable") +
            HelpExampleRpc("settrafficshaping", "\"receive\" 10000 1024"));

    if (disable)
    {
        if (bucket)
            bucket->disable();
    }
    else
    {
        uint64_t burst;
        uint64_t ave;
        if (params[1].isNum())
            burst = params[1].get_int64();
        else
        {
            string temp = params[1].get_str();
            burst = boost::lexical_cast<uint64_t>(temp);
        }
        if (params[2].isNum())
            ave = params[2].get_int64();
        else
        {
            string temp = params[2].get_str();
            ave = boost::lexical_cast<uint64_t>(temp);
        }
        if (burst < ave)
        {
            throw runtime_error("Burst rate must be greater than the average rate"
                                "\nsettrafficshaping \"send|receive\" \"burst\" \"average\"");
        }

        bucket->set(burst * 1024, ave * 1024);
    }

    return NullUniValue;
}

// fIsInitialBlockDownload is updated only during startup and whenever we receive a header.
// This way we avoid having to lock cs_main so often which tends to be a bottleneck.
void IsInitialBlockDownloadInit()
{
    const CChainParams &chainParams = Params();
    LOCK(cs_main);
    if (!pindexBestHeader)
    {
        // Not nearly synced if we don't have any blocks!
        fIsInitialBlockDownload.store(true);
        return;
    }
    if (fImporting || fReindex)
    {
        fIsInitialBlockDownload.store(true);
        return;
    }
    if (fCheckpointsEnabled && chainActive.Height() < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()))
    {
        fIsInitialBlockDownload.store(true);
        return;
    }
    static bool lockIBDState = false;
    if (lockIBDState)
    {
        fIsInitialBlockDownload.store(false);
        return;
    }
    bool state =
        (chainActive.Height() < pindexBestHeader->nHeight - 24 * 6 ||
            std::max(chainActive.Tip()->GetBlockTime(), pindexBestHeader->GetBlockTime()) < GetTime() - nMaxTipAge);
    if (!state)
        lockIBDState = true;
    fIsInitialBlockDownload.store(state);
    return;
}

bool IsInitialBlockDownload() { return fIsInitialBlockDownload.load(); }
// fIsChainNearlySyncd is updated only during startup and whenever we receive a header.
// This way we avoid having to lock cs_main so often which tends to be a bottleneck.
void IsChainNearlySyncdInit()
{
    LOCK(cs_main);
    if (!pindexBestHeader)
    {
        // Not nearly synced if we don't have any blocks!
        fIsChainNearlySyncd.store(false);
    }
    else
    {
        if (chainActive.Height() < pindexBestHeader->nHeight - 2)
            fIsChainNearlySyncd.store(false);
        else
            fIsChainNearlySyncd.store(true);
    }
}

bool IsChainNearlySyncd() { return fIsChainNearlySyncd.load(); }
uint64_t LargestBlockSeen(uint64_t nBlockSize)
{
    // C++98 lacks the capability to do static initialization properly
    // so we need a runtime check to make sure it is.
    // This can be removed when moving to C++11 .
    if (nBlockSize < BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
    {
        nBlockSize = BLOCKSTREAM_CORE_MAX_BLOCK_SIZE;
    }

    // Return the largest block size that we have seen since startup
    uint64_t nSize = nLargestBlockSeen.load();
    while (nBlockSize > nSize)
    {
        if (nLargestBlockSeen.compare_exchange_weak(nSize, nBlockSize))
        {
            return nBlockSize;
        }
    }

    return nSize;
}

/** Returns the block height of the current active chain tip. **/
int GetBlockchainHeight()
{
    LOCK(cs_main);
    return chainActive.Height();
}

void LoadFilter(CNode *pfrom, CBloomFilter *filter)
{
    if (!filter->IsWithinSizeConstraints())
        // There is no excuse for sending a too-large filter
        MISBEHAVING(pfrom, 100, "bloom filter too large");
    else
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pThinBlockFilter;
        pfrom->pThinBlockFilter = new CBloomFilter(*filter);
    }
    uint64_t nSizeFilter = ::GetSerializeSize(*pfrom->pThinBlockFilter, SER_NETWORK, PROTOCOL_VERSION);
    LogPrint("thin", "Thinblock Bloom filter size: %d\n", nSizeFilter);
    thindata.UpdateInBoundBloomFilter(nSizeFilter);
}

// Similar to TestBlockValidity but is very conservative in parameters (used in mining)
bool TestConservativeBlockValidity(CValidationState &state,
    const CChainParams &chainparams,
    const CBlock &block,
    CBlockIndex *pindexPrev,
    bool fCheckPOW,
    bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == chainActive.Tip());
    if (fCheckpointsEnabled && !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams, block.GetHash()))
        return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, state, fCheckPOW, fCheckMerkleRoot, true))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, true))
        return false;
    assert(state.IsValid());

    return true;
}

// Statistics:

CStatBase *FindStatistic(const char *name)
{
    LOCK(cs_statMap);
    CStatMap::iterator item = statistics.find(name);
    if (item != statistics.end())
        return item->second;
    return NULL;
}

UniValue getstatlist(const UniValue &params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error("getstatlist"
                            "\nReturns a list of all statistics available on this node.\n"
                            "\nArguments: None\n"
                            "\nResult:\n"
                            "  {\n"
                            "    \"name\" : (string) name of the statistic\n"
                            "    ...\n"
                            "  }\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getstatlist", "") + HelpExampleRpc("getstatlist", ""));

    CStatMap::iterator it;

    UniValue ret(UniValue::VARR);
    LOCK(cs_statMap);
    for (it = statistics.begin(); it != statistics.end(); ++it)
    {
        ret.push_back(it->first);
    }

    return ret;
}

UniValue getstat(const UniValue &params, bool fHelp)
{
    string specificIssue;

    int count = 0;
    if (params.size() < 3)
        count = 1; // if a count is not specified, give the latest sample
    else
    {
        if (!params[2].isNum())
        {
            try
            {
                count = boost::lexical_cast<int>(params[2].get_str());
            }
            catch (const boost::bad_lexical_cast &)
            {
                fHelp = true;
                specificIssue = "Invalid argument 3 \"count\" -- not a number";
            }
        }
        else
        {
            count = params[2].get_int();
        }
    }
    if (fHelp || (params.size() < 1))
        throw runtime_error("getstat"
                            "\nReturns the current settings for the network send and receive bandwidth and burst in "
                            "kilobytes per second.\n"
                            "\nArguments: \n"
                            "1. \"statistic\"     (string, required) Specify what statistic you want\n"
                            "2. \"series\"  (string, optional) Specify what data series you want.  Options are "
                            "\"total\", \"now\",\"all\", \"sec10\", \"min5\", \"hourly\", \"daily\",\"monthly\".  "
                            "Default is all.\n"
                            "3. \"count\"  (string, optional) Specify the number of samples you want.\n"

                            "\nResult:\n"
                            "  {\n"
                            "    \"<statistic name>\"\n"
                            "    {\n"
                            "    \"<series name>\"\n"
                            "      [\n"
                            "      <data>, (any type) The data points in the series\n"
                            "      ],\n"
                            "    ...\n"
                            "    },\n"
                            "  ...\n"
                            "  }\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getstat", "") + HelpExampleRpc("getstat", "") + "\n" + specificIssue);

    UniValue ret(UniValue::VARR);

    string seriesStr;
    if (params.size() < 2)
        seriesStr = "total";
    else
        seriesStr = params[1].get_str();
    // uint_t series = 0;
    // if (series == "now") series |= 1;
    // if (series == "all") series = 0xfffffff;
    LOCK(cs_statMap);

    CStatBase *base = FindStatistic(params[0].get_str().c_str());
    if (base)
    {
        UniValue ustat(UniValue::VOBJ);
        if (seriesStr == "now")
        {
            ustat.push_back(Pair("now", base->GetNow()));
        }
        else if (seriesStr == "total")
        {
            ustat.push_back(Pair("total", base->GetTotal()));
        }
        else
        {
            UniValue series = base->GetSeries(seriesStr, count);
            ustat.push_back(Pair(seriesStr, series));
        }

        ret.push_back(ustat);
    }
    return ret;
}

/* clang-format off */
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    /* P2P networking */
    { "network",            "settrafficshaping",      &settrafficshaping,      true  },
    { "network",            "gettrafficshaping",      &gettrafficshaping,      true  },
    { "network",            "pushtx",                 &pushtx,                 true  },
    { "network",            "getexcessiveblock",      &getexcessiveblock,      true  },
    { "network",            "setexcessiveblock",      &setexcessiveblock,      true  },
    { "network",            "expedited",              &expedited,              true  },

    /* Mining */
    { "mining",             "getminingmaxblock",      &getminingmaxblock,      true  },
    { "mining",             "setminingmaxblock",      &setminingmaxblock,      true  },
    { "mining",             "getminercomment",        &getminercomment,        true  },
    { "mining",             "setminercomment",        &setminercomment,        true  },
    { "mining",             "getblockversion",        &getblockversion,        true  },
    { "mining",             "setblockversion",        &setblockversion,        true  },
    { "mining",             "validateblocktemplate",  &validateblocktemplate,  true  },

    /* Utility functions */
    { "util",               "getstatlist",            &getstatlist,            true  },
    { "util",               "getstat",                &getstat,                true  },
    { "util",               "get",                    &gettweak,               true  },
    { "util",               "set",                    &settweak,               true  },
    { "util",               "validatechainhistory",   &validatechainhistory,   true  },
#ifdef DEBUG
    { "util",               "getstructuresizes",      &getstructuresizes,      true  },  // BU
#endif

    /* Coin generation */
    { "generating",         "getgenerate",            &getgenerate,            true  },
    { "generating",         "setgenerate",            &setgenerate,            true  },
};
/* clang-format on */

void RegisterUnlimitedRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}


UniValue validatechainhistory(const UniValue &params, bool fHelp)
{
    if (fHelp)
        throw runtime_error("validatechainhistory [hash]\n"
                            "\nUpdates a chain's valid/invalid status based on parent blocks.\n");

    std::stack<CBlockIndex *> stk;
    CBlockIndex *pos = pindexBestHeader;
    bool failedChain = false;
    UniValue ret = NullUniValue;

    LOCK(cs_main);

    if (params.size() >= 1)
    {
        std::string strHash = params[0].get_str();
        uint256 hash(uint256S(strHash));

        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlock block;
        pos = mapBlockIndex[hash];
    }

    LogPrintf("validatechainhistory starting at %d %s\n", pos->nHeight, pos->phashBlock->ToString());
    while (pos && !failedChain)
    {
        // LogPrintf("validate %d %s\n", pos->nHeight, pos->phashBlock->ToString());
        failedChain = pos->nStatus & BLOCK_FAILED_MASK;
        if (!failedChain)
        {
            stk.push(pos);
        }
        pos = pos->pprev;
    }
    if (failedChain)
    {
        ret = UniValue("Chain has a bad ancestor");
        while (!stk.empty())
        {
            pos = stk.top();
            if (pos)
            {
                pos->nStatus |= BLOCK_FAILED_CHILD;
            }
            setDirtyBlockIndex.insert(pos);
            stk.pop();
        }
        FlushStateToDisk();
        pindexBestHeader = FindMostWorkChain();
    }
    else
    {
        ret = UniValue("Chain is ok");
    }

    return ret;
}


UniValue validateblocktemplate(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
            "validateblocktemplate \"hexdata\"\n"
            "\nReturns whether this block template will be accepted if a hash solution is found.\n"
            "The 'jsonparametersobject' parameter is currently ignored.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"    (string, required) the hex-encoded block to validate (same format as submitblock)\n"
            "\nResult:\n"
            "true (boolean) submitted block template is valid\n"
            "JSONRPCException if submitted block template is invalid\n"
            "\nExamples:\n" +
            HelpExampleCli("validateblocktemplate", "\"mydata\"") +
            HelpExampleRpc("validateblocktemplate", "\"mydata\""));

    UniValue ret(UniValue::VARR);
    CBlock block;
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    if (block.nBlockSize == 0)
        block.nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);

    CBlockIndex *pindexPrev = NULL;
    {
        LOCK(cs_main);

        BlockMap::iterator i = mapBlockIndex.find(block.hashPrevBlock);
        if (i == mapBlockIndex.end())
        {
            throw runtime_error("invalid block: unknown parent");
        }

        pindexPrev = i->second;

        if (pindexPrev != chainActive.Tip())
        {
            throw runtime_error("invalid block: does not build on chain tip");
        }

        DbgAssert(pindexPrev, throw runtime_error("invalid block: unknown parent"));

        const CChainParams &chainparams = Params();
        CValidationState state;
        if (block.nBlockSize <= BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
        {
            if (!TestConservativeBlockValidity(state, chainparams, block, pindexPrev, false, true))
            {
                throw runtime_error(std::string("invalid block: ") + state.GetRejectReason());
            }
        }
        else
        {
            if (!TestBlockValidity(state, chainparams, block, pindexPrev, false, true))
            {
                throw runtime_error(std::string("invalid block: ") + state.GetRejectReason());
            }
        }

        if (block.fExcessive)
        {
            throw runtime_error("invalid block: excessive");
        }
    }

    return UniValue(true);
}

#ifdef DEBUG
#ifdef DEBUG_LOCKORDER
extern std::map<std::pair<void *, void *>, LockStack> lockorders;
#endif

extern std::vector<std::string> vUseDNSSeeds;
extern std::list<CNode *> vNodesDisconnected;
extern std::set<CNetAddr> setservAddNodeAddresses;
extern UniValue getstructuresizes(const UniValue &params, bool fHelp)
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("time", GetTime()));
    ret.push_back(Pair("requester.mapTxnInfo", requester.mapTxnInfo.size()));
    ret.push_back(Pair("requester.mapBlkInfo", requester.mapBlkInfo.size()));
    unsigned long int max = 0;
    unsigned long int size = 0;
    for (CRequestManager::OdMap::iterator i = requester.mapTxnInfo.begin(); i != requester.mapTxnInfo.end(); i++)
    {
        unsigned long int temp = i->second.availableFrom.size();
        size += temp;
        if (max < temp)
            max = temp;
    }
    ret.push_back(Pair("requester.mapTxnInfo.maxobj", max));
    ret.push_back(Pair("requester.mapTxnInfo.totobj", size));

    max = 0;
    size = 0;
    for (CRequestManager::OdMap::iterator i = requester.mapBlkInfo.begin(); i != requester.mapBlkInfo.end(); i++)
    {
        unsigned long int temp = i->second.availableFrom.size();
        size += temp;
        if (max < temp)
            max = temp;
    }
    ret.push_back(Pair("requester.mapBlkInfo.maxobj", max));
    ret.push_back(Pair("requester.mapBlkInfo.totobj", size));

    ret.push_back(Pair("mapBlockIndex", mapBlockIndex.size()));
    // CChain
    {
        LOCK(cs_xval);
        ret.push_back(Pair("setPreVerifiedTxHash", setPreVerifiedTxHash.size()));
        ret.push_back(Pair("setUnVerifiedOrphanTxHash", setUnVerifiedOrphanTxHash.size()));
    }
    ret.push_back(Pair("mapLocalHost", mapLocalHost.size()));
    ret.push_back(Pair("CDoSManager::vWhitelistedRange", dosMan.vWhitelistedRange.size()));
    ret.push_back(Pair("mapInboundConnectionTracker", mapInboundConnectionTracker.size()));
    ret.push_back(Pair("vUseDNSSeeds", vUseDNSSeeds.size()));
    ret.push_back(Pair("vAddedNodes", vAddedNodes.size()));
    ret.push_back(Pair("setservAddNodeAddresses", setservAddNodeAddresses.size()));
    ret.push_back(Pair("statistics", statistics.size()));
    ret.push_back(Pair("tweaks", tweaks.size()));
    ret.push_back(Pair("mapRelay", mapRelay.size()));
    ret.push_back(Pair("vRelayExpiration", vRelayExpiration.size()));
    ret.push_back(Pair("vNodes", vNodes.size()));
    ret.push_back(Pair("vNodesDisconnected", vNodesDisconnected.size()));
    // CAddrMan
    ret.push_back(Pair("mapOrphanTransactions", mapOrphanTransactions.size()));
    ret.push_back(Pair("mapOrphanTransactionsByPrev", mapOrphanTransactionsByPrev.size()));

    uint32_t nExpeditedBlocks, nExpeditedTxs, nExpeditedUpstream;
    connmgr->ExpeditedNodeCounts(nExpeditedBlocks, nExpeditedTxs, nExpeditedUpstream);
    ret.push_back(Pair("xpeditedBlk", (uint64_t)nExpeditedBlocks));
    ret.push_back(Pair("xpeditedBlkUp", (uint64_t)nExpeditedUpstream));
    ret.push_back(Pair("xpeditedTxn", (uint64_t)nExpeditedTxs));

#ifdef DEBUG_LOCKORDER
    ret.push_back(Pair("lockorders", lockorders.size()));
#endif

    LOCK(cs_vNodes);
    std::vector<CNode *>::iterator n;
    uint64_t totalThinBlockSize = 0;
    int disconnected = 0; // watch # of disconnected nodes to ensure they are being cleaned up
    for (std::vector<CNode *>::iterator it = vNodes.begin(); it != vNodes.end(); ++it)
    {
        if (*it == NULL)
            continue;
        CNode &n = **it;
        UniValue node(UniValue::VOBJ);
        disconnected += (n.fDisconnect) ? 1 : 0;

        node.push_back(Pair("vSendMsg", n.vSendMsg.size()));
        node.push_back(Pair("vRecvGetData", n.vRecvGetData.size()));
        node.push_back(Pair("vRecvMsg", n.vRecvMsg.size()));
        if (n.pfilter)
        {
            node.push_back(Pair("pfilter", n.pfilter->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
        }
        if (n.pThinBlockFilter)
        {
            node.push_back(
                Pair("pThinBlockFilter", n.pThinBlockFilter->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)));
        }
        node.push_back(Pair("thinblock.vtx", n.thinBlock.vtx.size()));
        uint64_t thinBlockSize = ::GetSerializeSize(n.thinBlock, SER_NETWORK, PROTOCOL_VERSION);
        totalThinBlockSize += thinBlockSize;
        node.push_back(Pair("thinblock.size", thinBlockSize));
        node.push_back(Pair("thinBlockHashes", n.thinBlockHashes.size()));
        node.push_back(Pair("xThinBlockHashes", n.xThinBlockHashes.size()));
        node.push_back(Pair("vAddrToSend", n.vAddrToSend.size()));
        node.push_back(Pair("vInventoryToSend", n.vInventoryToSend.size()));
        node.push_back(Pair("setAskFor", n.setAskFor.size()));
        node.push_back(Pair("mapAskFor", n.mapAskFor.size()));
        ret.push_back(Pair(n.addrName, node));
    }
    ret.push_back(Pair("totalThinBlockSize", totalThinBlockSize));
    ret.push_back(Pair("disconnectedNodes", disconnected));

    return ret;
}
#endif

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

void MarkAllContainingChainsInvalid(CBlockIndex *invalidBlock)
{
    LOCK(cs_main);

    bool dirty = false;
    DbgAssert(invalidBlock->nStatus & BLOCK_FAILED_MASK, return );

    // Find all the chain tips:
    std::set<CBlockIndex *, CompareBlocksByHeight> setTips;
    std::set<CBlockIndex *> setOrphans;
    std::set<CBlockIndex *> setPrevs;

    BOOST_FOREACH (const PAIRTYPE(const uint256, CBlockIndex *) & item, mapBlockIndex)
    {
        if (!chainActive.Contains(item.second))
        {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    for (std::set<CBlockIndex *>::iterator it = setOrphans.begin(); it != setOrphans.end(); ++it)
    {
        if (setPrevs.erase(*it) == 0)
        {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    BOOST_FOREACH (CBlockIndex *tip, setTips)
    {
        if (tip->GetAncestor(invalidBlock->nHeight) == invalidBlock)
        {
            for (CBlockIndex *blk = tip; blk != invalidBlock; blk = blk->pprev)
            {
                if ((blk->nStatus & BLOCK_FAILED_CHILD) == 0)
                {
                    blk->nStatus |= BLOCK_FAILED_CHILD;
                    setDirtyBlockIndex.insert(blk);
                    dirty = true;
                }
            }
        }
    }

    if (dirty)
        FlushStateToDisk();
}


//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//
bool AlreadyHaveOrphan(uint256 hash)
{
    READLOCK(cs_orphancache);
    if (mapOrphanTransactions.count(hash))
        return true;
    return false;
}
bool AddOrphanTx(const CTransaction &tx, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_orphancache)
{
    AssertLockHeld(cs_orphancache);

    if (mapOrphanTransactions.empty())
        DbgAssert(nBytesOrphanPool == 0, nBytesOrphanPool = 0);

    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore orphans larger than the largest txn size allowed.
    unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz > MAX_STANDARD_TX_SIZE)
    {
        LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash.ToString());
        return false;
    }

    uint64_t txSize = RecursiveDynamicUsage(tx);
    mapOrphanTransactions[hash].tx = tx;
    mapOrphanTransactions[hash].fromPeer = peer;
    mapOrphanTransactions[hash].nEntryTime = GetTime(); // BU - Xtreme Thinblocks;
    mapOrphanTransactions[hash].nOrphanTxSize = txSize;
    BOOST_FOREACH (const CTxIn &txin, tx.vin)
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    nBytesOrphanPool += txSize;
    LogPrint("mempool", "stored orphan tx %s bytes:%ld (mapsz %u prevsz %u), orphan pool bytes:%ld\n", hash.ToString(),
        txSize, mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size(), nBytesOrphanPool);
    return true;
}

void EraseOrphanTx(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(cs_orphancache)
{
    AssertLockHeld(cs_orphancache);

    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;
    BOOST_FOREACH (const CTxIn &txin, it->second.tx.vin)
    {
        std::map<uint256, std::set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }

    nBytesOrphanPool -= it->second.nOrphanTxSize;
    LogPrint("mempool", "Erased orphan tx %s of size %ld bytes, orphan pool bytes:%ld\n",
        it->second.tx.GetHash().ToString(), it->second.nOrphanTxSize, nBytesOrphanPool);
    mapOrphanTransactions.erase(it);
}


unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans, uint64_t nMaxBytes) EXCLUSIVE_LOCKS_REQUIRED(cs_orphancache)
{
    AssertLockHeld(cs_orphancache);

    // Limit the orphan pool size by either number of transactions or the max orphan pool size allowed.
    // Limiting by pool size to 1/10th the size of the maxmempool alone is not enough because the total number
    // of txns in the pool can adversely effect the size of the bloom filter in a get_xthin message.
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans || nBytesOrphanPool > nMaxBytes)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}


// BU - Xtreme Thinblocks: begin
void EraseOrphansByTime() EXCLUSIVE_LOCKS_REQUIRED(cs_orphancache)
{
    AssertLockHeld(cs_orphancache);

    // Because we have to iterate through the entire orphan cache which can be large we don't want to check this
    // every time a tx enters the mempool but just once every 5 minutes is good enough.
    if (GetTime() < nLastOrphanCheck + 5 * 60)
        return;
    int64_t nOrphanTxCutoffTime = GetTime() - GetArg("-orphanpoolexpiry", DEFAULT_ORPHANPOOL_EXPIRY) * 60 * 60;
    std::map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end())
    {
        std::map<uint256, COrphanTx>::iterator mi = iter++; // increment to avoid iterator becoming invalid
        int64_t nEntryTime = mi->second.nEntryTime;
        if (nEntryTime < nOrphanTxCutoffTime)
        {
            uint256 txHash = mi->second.tx.GetHash();
            LogPrint(
                "mempool", "Erased old orphan tx %s of age %d seconds\n", txHash.ToString(), GetTime() - nEntryTime);
            EraseOrphanTx(txHash);
        }
    }

    nLastOrphanCheck = GetTime();
}
// BU - Xtreme Thinblocks: end

class Snapshot
{
public:
    uint64_t tipHeight;
    uint64_t tipMedianTimePast;
    int64_t adjustedTime;
    CBlockIndex *tip;  // CBlockIndexes are never deleted once created (even if the tip changes) so we can use this ptr
    CCoinsViewCache *coins;
    CCoinsViewMemPool* cvMempool;

    void Load(void)
    {
        if (1)
        {
            LOCK(cs_main);
            tipHeight = chainActive.Height();
            tip = chainActive.Tip();
            tipMedianTimePast = tip->GetMedianTimePast();
            adjustedTime = GetAdjustedTime();
            coins = pcoinsTip; // TODO pcoinsTip can change
        }
        if (cvMempool) delete cvMempool;
        if (1)
        {
            READLOCK(mempool.cs);
            // ss.coins contains the UTXO set for the tip in ss
            cvMempool = new CCoinsViewMemPool(coins, mempool);
        }
    }

    Snapshot():coins(nullptr), cvMempool(nullptr) {}
    ~Snapshot()
    {
        if (cvMempool) delete cvMempool;
    }
};

bool CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints *lp, bool useExistingLockPoints, const Snapshot& ss)
{
    AssertLockHeld(mempool.cs);

    CBlockIndex *tip = ss.tip;
    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints)
    {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else
    {
         // CCoinsViewMemPool viewMemPool(ss.coins, mempool);
        CCoinsViewMemPool& viewMemPool(*ss.cvMempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++)
        {
            const CTxIn &txin = tx.vin[txinIndex];
            CCoins coins;
            if (!viewMemPool.GetCoins(txin.prevout.hash, coins))
            {
                return error("%s: Missing input", __func__);
            }
            if (coins.nHeight == MEMPOOL_HEIGHT)
            {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            }
            else
            {
                prevheights[txinIndex] = coins.nHeight;
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp)
        {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            BOOST_FOREACH (int height, prevheights)
            {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight + 1)
                {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}

bool CheckFinalTx(const CTransaction &tx, int flags, const Snapshot& ss)
{
    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = ss.tipHeight + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime =
        (flags & LOCKTIME_MEDIAN_TIME_PAST) ? ss.tipMedianTimePast: GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

bool ParallelAcceptToMemoryPool(Snapshot& ss, CTxMemPool &pool,
    CValidationState &state,
    const CTransaction &consttx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    std::vector<uint256> &vHashTxnToUncache)
{
    unsigned int forkVerifyFlags = 0;
    CTransaction tx = consttx;
    unsigned int nSigOps = 0;
    ValidationResourceTracker resourceTracker;
    unsigned int nSize = 0;
    uint64_t start = GetStopwatch();
    uint64_t now = GetTimeMicros();
    if (pfMissingInputs)
        *pfMissingInputs = false;

    if (!CheckTransaction(tx, state))
      {
        return false;
      }

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    // BUIP055: reject transactions that won't work on the fork, starting 2 hours before the fork
    // based on op_return.
    // But accept both sighashtypes because we will need all the tx types during the transition.
    // This code uses the system time to determine when to start rejecting which is inaccurate relative to the
    // actual activation time (defined by times in the blocks).
    // But its ok to reject these transactions from the mempool a little early (or late).
    if ((miningForkTime.value != 0) && (now / 1000000 >= miningForkTime.value - (2 * 60 * 60)))
    {
        forkVerifyFlags = SCRIPT_ENABLE_SIGHASH_FORKID;
        if (IsTxOpReturnInvalid(tx))
            return state.DoS(0, false, REJECT_WRONG_FORK, "wrong-fork");
    }

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    std::string reason;
    if (fRequireStandard && !IsStandardTx(tx, reason))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);

    // Don't relay version 2 transactions until CSV is active, and we can be
    // sure that such transactions will be mined (unless we're on
    // -testnet/-regtest).
    const CChainParams &chainparams = Params();
    if (fRequireStandard && tx.nVersion >= 2 &&
        VersionBitsTipState(chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV) != THRESHOLD_ACTIVE)
    {
        return state.DoS(0, false, REJECT_NONSTANDARD, "premature-version2-tx");
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, ss))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
      {
        return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool");
      }

    // Check for conflicts with in-memory transactions
    {
        READLOCK(pool.cs); // protect pool.mapNextTx
        BOOST_FOREACH (const CTxIn &txin, tx.vin)
        {
            auto itConflicting = pool.mapNextTx.find(txin.prevout);
            if (itConflicting != pool.mapNextTx.end())
            {
                // Disable replacement feature for good
                return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");
            }
        }
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        LockPoints lp;
        {
            READLOCK(pool.cs);
            CCoinsViewMemPool& viewMemPool(*ss.cvMempool);
            view.SetBackend(viewMemPool);

            // do all inputs exist?
            // Note that this does not check for the presence of actual outputs (see the next check for that),
            // and only helps with filling in pfMissingInputs (to determine missing vs spent).
            BOOST_FOREACH (const CTxIn txin, tx.vin)
            {
                if (!ss.coins->HaveCoinsInCache(txin.prevout.hash))
                    vHashTxnToUncache.push_back(txin.prevout.hash);
                if (!view.HaveCoins(txin.prevout.hash))
                {
                    if (pfMissingInputs)
                        *pfMissingInputs = true;
                    // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
                    return false;
                }
            }

            // are the actual inputs available?
            if (!view.HaveInputs(tx))
                return state.Invalid(false, REJECT_DUPLICATE, "bad-txns-inputs-spent");

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(tx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);

            // Only accept BIP68 sequence locked transactions that can be mined in the next
            // block; we don't want our mempool filled up with transactions that can't
            // be mined yet.
            // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
            // CoinsViewCache instead of create its own
            if (!CheckSequenceLocks(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp, false, ss))
                return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, view);

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        double nPriorityDummy = 0;
        pool.ApplyDeltas(hash, nPriorityDummy, nModifiedFees);

        CAmount inChainInputValue;
        double dPriority = view.GetPriority(tx, chainActive.Height(), inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        BOOST_FOREACH (const CTxIn &txin, tx.vin)
        {
            const CCoins *coins = view.AccessCoins(txin.prevout.hash);
            if (coins->IsCoinBase())
            {
                fSpendsCoinbase = true;
                break;
            }
        }

        CTxCommitData eData;
        CTxMemPoolEntry entryTemp(tx, nFees, GetTime(), dPriority, chainActive.Height(), pool.HasNoInputsOf(tx),
            inChainInputValue, fSpendsCoinbase, nSigOps, lp);
        eData.entry=entryTemp;
        CTxMemPoolEntry& entry(eData.entry);
        nSize = entry.GetTxSize();

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine.
        if (nSigOps > MAX_TX_SIGOPS)
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false, strprintf("%d", nSigOps));

        CAmount mempoolRejectFee =
            pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
        if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee)
        {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false,
                strprintf("%d < %d", nFees, mempoolRejectFee));
        }
        else if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nModifiedFees < ::minRelayTxFee.GetFee(nSize) &&
                 !AllowFree(entry.GetPriority(chainActive.Height() + 1)))
        {
            // Require that free transactions have sufficient priority to be mined in the next block.
	    LogPrint("mempool","Txn fee %lld (%d - %d), priority fee delta was %lld\n",nFees, nValueIn, nValueOut, nModifiedFees - nFees);
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
        }

        // BU - Xtreme Thinblocks Auto Mempool Limiter - begin section
        /* Continuously rate-limit free (really, very-low-fee) transactions
         * This mitigates 'penny-flooding' -- sending thousands of free transactions just to
         * be annoying or make others' transactions take longer to confirm. */
        // maximum feeCutoff in satoshi per byte
        static const double maxFeeCutoff =
            boost::lexical_cast<double>(GetArg("-maxlimitertxfee", DEFAULT_MAXLIMITERTXFEE));
        // starting value for feeCutoff in satoshi per byte
        static const double initFeeCutoff =
            boost::lexical_cast<double>(GetArg("-minlimitertxfee", DEFAULT_MINLIMITERTXFEE));
        static const int nLimitFreeRelay = GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY);

        // get current memory pool size
        uint64_t poolBytes = pool.GetTotalTxSize();

        // Calculate feeCutoff in satoshis per byte:
        //   When the feeCutoff is larger than the satoshiPerByte of the
        //   current transaction then spam blocking will be in effect. However
        //   Some free transactions will still get through based on -limitfreerelay
        static double feeCutoff;
        static double nFreeLimit = nLimitFreeRelay;
        static int64_t nLastTime;
        int64_t nNow = GetTime();

        // When the mempool starts falling use an exponentially decaying ~24 hour window:
        // nFreeLimit = nFreeLimit + ((double)(DEFAULT_LIMIT_FREE_RELAY - nFreeLimit) / pow(1.0 - 1.0/86400,
        // (double)(nNow - nLastTime)));
        nFreeLimit /= std::pow(1.0 - 1.0 / 86400, (double)(nNow - nLastTime));

        // When the mempool starts falling use an exponentially decaying ~24 hour window:
        feeCutoff *= std::pow(1.0 - 1.0 / 86400, (double)(nNow - nLastTime));

        uint64_t nLargestBlockSeen = LargestBlockSeen();

        if (poolBytes < nLargestBlockSeen)
        {
            feeCutoff = std::max(feeCutoff, initFeeCutoff);
            nFreeLimit = std::min(nFreeLimit, (double)nLimitFreeRelay);
        }
        else if (poolBytes < (nLargestBlockSeen * MAX_BLOCK_SIZE_MULTIPLIER))
        {
            // Gradually choke off what is considered a free transaction
            feeCutoff =
                std::max(feeCutoff, initFeeCutoff + ((maxFeeCutoff - initFeeCutoff) * (poolBytes - nLargestBlockSeen) /
                                                        (nLargestBlockSeen * (MAX_BLOCK_SIZE_MULTIPLIER - 1))));

            // Gradually choke off the nFreeLimit as well but leave at least DEFAULT_MIN_LIMITFREERELAY
            // So that some free transactions can still get through
            nFreeLimit = std::min(
                nFreeLimit, ((double)nLimitFreeRelay - ((double)(nLimitFreeRelay - DEFAULT_MIN_LIMITFREERELAY) *
                                                           (double)(poolBytes - nLargestBlockSeen) /
                                                           (nLargestBlockSeen * (MAX_BLOCK_SIZE_MULTIPLIER - 1)))));
            if (nFreeLimit < DEFAULT_MIN_LIMITFREERELAY)
                nFreeLimit = DEFAULT_MIN_LIMITFREERELAY;
        }
        else
        {
            feeCutoff = maxFeeCutoff;
            nFreeLimit = DEFAULT_MIN_LIMITFREERELAY;
        }

        minRelayTxFee = CFeeRate(feeCutoff * 1000);
        LogPrint("mempool",
            "MempoolBytes:%d  LimitFreeRelay:%.5g  FeeCutOff:%.4g  FeesSatoshiPerByte:%.4g  TxBytes:%d  TxFees:%d\n",
            poolBytes, nFreeLimit, ((double)::minRelayTxFee.GetFee(nSize)) / nSize, ((double)nFees) / nSize, nSize,
            nFees);
        if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize))
        {
            static double dFreeCount;

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= std::pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;

            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount + nSize);
            if ((dFreeCount + nSize) >= (nFreeLimit * 10 * 1000 * nLargestBlockSeen / BLOCKSTREAM_CORE_MAX_BLOCK_SIZE))
            {
                thindata.UpdateMempoolLimiterBytesSaved(nSize);
                LogPrint(
                    "mempool", "AcceptToMemoryPool : free transaction %s rejected by rate limiter\n", hash.ToString());
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "rate limited free transaction");
            }
            dFreeCount += nSize;
        }
        nLastTime = nNow;
        // BU - Xtreme Thinblocks Auto Mempool Limiter - end section

        // BU: we calculate the recommended fee by looking at what's in the mempool.  This starts at 0 though for an
        // empty mempool.  So set the minimum "absurd" fee to 10000 satoshies per byte.  If for some reason fees rise
        // above that, you can specify up to 100x what other txns are paying in the mempool
        if (fRejectAbsurdFee && nFees > std::max((int64_t)100L * nSize, maxTxFee.value) * 100)
            return state.Invalid(false, REJECT_HIGHFEE, "absurdly-high-fee",
                strprintf("%d > %d", nFees, std::max((int64_t)1L, maxTxFee.value) * 10000));

        eData.hash = hash;
        // Calculate in-mempool ancestors, up to a limit.
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
        std::string errString;
        if (!pool.CalculateMemPoolAncestors(entry, eData.setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants,
                nLimitDescendantSize, errString))
        {
            return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        unsigned char sighashType = 0;
        if (!CheckInputs(tx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS | forkVerifyFlags, true, &resourceTracker,
                NULL, &sighashType))
        {
            LogPrint("mempool", "txn CheckInputs failed");
            return false;
        }
        entry.UpdateRuntimeSigOps(resourceTracker.GetSigOps(), resourceTracker.GetSighashBytes());

#if 0  // to be removed -- bugs in this should have been shaken out
        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        unsigned char sighashType2 = 0;
        if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS | forkVerifyFlags, true, NULL, NULL,
                &sighashType2))
        {
            return error(
                "%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                __func__, hash.ToString(), FormatStateMessage(state));
        }
        entry.sighashType = sighashType | sighashType2;
#endif

        entry.sighashType = sighashType;
        // This code denies old style tx from entering the mempool as soon as we fork
        if (chainActive.Tip()->IsforkActiveOnNextBlock(miningForkTime.value) && !IsTxBUIP055Only(entry))
        {
            return state.Invalid(false, REJECT_WRONG_FORK, "txn-uses-old-sighash-algorithm");
        }

        if (1)
        {
            LOCK(csTxCommitQ);
            txCommitQ.push(eData);
        }
    }



    uint64_t interval = (GetStopwatch()-start)/1000;
    LogPrint("bench", "ValidateTransaction, time: %d, tx: %s, len: %d, sigops: %llu (legacy: %u), sighash: %llu, Vin: "
                      "%llu, Vout: %llu\n",
        interval, tx.GetHash().ToString(), nSize, resourceTracker.GetSigOps(), (unsigned int)nSigOps,
        resourceTracker.GetSighashBytes(), tx.vin.size(), tx.vout.size());
    nTxValidationTime << interval;

    return true;
}

bool TxAlreadyHave(const CInv &inv)
{
    switch (inv.type)
    {
    case MSG_TX:
    {
        bool rrc = false;
        if (1)
        {
            READLOCK(csRecentRejects);
            rrc = recentRejects ? recentRejects->contains(inv.hash) : false;
        }
        bool mrc = mempool.exists(inv.hash);
        return rrc || mrc || AlreadyHaveOrphan(inv.hash) || pcoinsTip->HaveCoins(inv.hash);
    }
    }
    DbgAssert(0, return false); // this fn should only be called if CInv is a tx
}

void processOrphans(std::vector<uint256>& vWorkQueue)
{
    std::vector<uint256> vEraseQueue;

    // Recursively process any orphan transactions that depended on this one
    if (1)
    {
        READLOCK(cs_orphancache);
        std::set<NodeId> setMisbehaving;
        for (unsigned int i = 0; i < vWorkQueue.size(); i++)
        {
            std::map<uint256, std::set<uint256> >::iterator itByPrev = mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
            if (itByPrev == mapOrphanTransactionsByPrev.end())
                continue;
            for (std::set<uint256>::iterator mi = itByPrev->second.begin(); mi != itByPrev->second.end(); ++mi)
            {
                const uint256 &orphanHash = *mi;

                // Make sure we actually have an entry on the orphan cache. While this should never fail because
                // we always erase orphans and any mapOrphanTransactionsByPrev at the same time, still we need
                // to
                // be sure.
                bool fOk = true;
                DbgAssert(mapOrphanTransactions.count(orphanHash), fOk = false);
                if (!fOk)
                    continue;

                const CTransaction &orphanTx = mapOrphanTransactions[orphanHash].tx;
                NodeId fromPeer = mapOrphanTransactions[orphanHash].fromPeer;
                // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                // anyone relaying LegitTxX banned)
                CValidationState stateDummy;


                if (setMisbehaving.count(fromPeer))
                    continue;
                if (1)
                {
                    CTxInputData txd;
                    txd.tx = orphanTx;
                    txd.nodeId = fromPeer;
                    txd.nodeName = "orphan";
                    txd.whitelisted = false;
                    boost::unique_lock<boost::mutex> lock(csTxInQ);
                    txInQ.push(txd); // add this transaction onto the processing queue
                    cvTxInQ.notify_one();
                }
                vEraseQueue.push_back(orphanHash);
            }
        }
    }

    if (1)
    {
        WRITELOCK(cs_orphancache);
        BOOST_FOREACH (uint256 hash, vEraseQueue)
            EraseOrphanTx(hash);
        //  BU: Xtreme thinblocks - purge orphans that are too old
        EraseOrphansByTime();
    }

}

void ThreadCommitToMempool()
{
    std::vector<uint256> vWorkQueue;

    while (1)
    {
        MilliSleep(5000);
        if (1)
        {
            LOCK(csTxCommitQ);
            if (txCommitQ.empty())
                continue;
        }

        if (1)
        {
            LOCK2(cs_main, csTxCommitQ);
            while (!txCommitQ.empty())
            {
                CTxCommitData &data = txCommitQ.front();
                // Store transaction in memory
                mempool.addUnchecked(data.hash, data.entry, data.setAncestors, !IsInitialBlockDownload());
                if (mempool.exists(data.hash))
                    SyncWithWallets(data.entry.GetTx(), NULL);
                vWorkQueue.push_back(data.hash);

                txCommitQ.pop();
            }

            processOrphans(vWorkQueue);
            mempool.check(pcoinsTip);
            // BU - Xtreme Thinblocks - trim the orphan pool by entry time and do not allow it to be overidden.
            LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

            // BU: update tx per second when a tx is valid and accepted
            mempool.UpdateTransactionsPerSecond();
        }

        CValidationState state;
        FlushStateToDisk(state, FLUSH_STATE_PERIODIC);
        // The flush to disk above is only periodic therefore we need to continuously trim any excess from the
        // cache.
        pcoinsTip->Trim(nCoinCacheUsage);
    }
}

void ThreadTxHandler()
{
    while (1)
    {
        boost::this_thread::interruption_point();

        bool fMissingInputs = false;
        CValidationState state;
        Snapshot ss;
        CTxInputData txd;
        if (1)
        {
            boost::unique_lock<boost::mutex> lock(csTxInQ);
            while (txInQ.empty())
            {
                cvTxInQ.wait(lock);
            }
            txd = txInQ.front(); // make copy so I can pop & release
            txInQ.pop();
        }

        if (1)
        {
            CTransaction& tx = txd.tx;
            CInv inv(MSG_TX, tx.GetHash());

            std::vector<uint256> vHashTxToUncache;
            if (1)
            {
                ss.Load();
                // Check for recently rejected (and do other quick existence checks)
                bool have = TxAlreadyHave(inv);
                if (have)
                {
                    if (1)
                    {
                        WRITELOCK(csRecentRejects);
                        if (recentRejects)
                            recentRejects->insert(tx.GetHash()); // should always be true
                    }

                    if (txd.whitelisted && GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY))
                    {
                        // Always relay transactions received from whitelisted peers, even
                        // if they were already in the mempool or rejected from it due
                        // to policy, allowing the node to function as a gateway for
                        // nodes hidden behind it.
                        //
                        // Never relay transactions that we would assign a non-zero DoS
                        // score for, as we expect peers to do the same with us in that
                        // case.
                        int nDoS = 0;
                        if (!state.IsInvalid(nDoS) || nDoS == 0)
                        {
                            LogPrintf("Force relaying tx %s from whitelisted peer=%s\n", tx.GetHash().ToString(),
                                txd.nodeName);
                            RelayTransaction(tx);
                        }
                        else
                        {
                            LogPrintf("Not relaying invalid transaction %s from whitelisted peer=%d (%s)\n",
                                tx.GetHash().ToString(), txd.nodeName, FormatStateMessage(state));
                        }
                    }
                    continue;
                }
            }

            if (ParallelAcceptToMemoryPool(ss, mempool, state, tx, true, &fMissingInputs, false, false, vHashTxToUncache))
            {
                mempool.check(pcoinsTip);
                RelayTransaction(tx);

                LogPrint("mempool", "AcceptToMemoryPool: peer=%s: accepted %s (poolsz %u txn, %u kB)\n",
                    txd.nodeName, tx.GetHash().ToString(), mempool.size(), mempool.DynamicMemoryUsage() / 1000);

            }
            else
            {
                BOOST_FOREACH (const uint256 &hashTx, vHashTxToUncache)
                   pcoinsTip->Uncache(hashTx);
                if (fMissingInputs)
                {
                    // If we've forked and this is probably not a valid tx, then skip adding it to the orphan pool
                    if (!chainActive.Tip()->IsforkActiveOnNextBlock(miningForkTime.value) || IsTxProbablyNewSigHash(tx))
                    {
                        WRITELOCK(cs_orphancache);
                        AddOrphanTx(tx, txd.nodeId);

                        // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
                        static unsigned int nMaxOrphanTx =
                            (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
                        static uint64_t nMaxOrphanPoolSize = (uint64_t)std::max(
                            (int64_t)0, (GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000 / 10));
                        unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx, nMaxOrphanPoolSize);
                        if (nEvicted > 0)
                            LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
                    }
                }
            }
            int nDoS = 0;
            if (state.IsInvalid(nDoS))
            {
                LogPrint("mempoolrej", "%s from peer=%s was not accepted: %s\n", tx.GetHash().ToString(), txd.nodeName,
                    FormatStateMessage(state));
                if (state.GetRejectCode() < REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
                {
                    // TODO
                    //pfrom->PushMessage(NetMsgType::REJECT, strCommand, (unsigned char)state.GetRejectCode(),
                    //    state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
#if 0                    
                    if (nDoS > 0)
                    {
#ifdef BITCOIN_CASH
                        // this is just temporary and will be taken out on the next release.
                        if (pfrom->nVersion != 80002)
                            dosMan.Misbehaving(pfrom, nDoS);
#else
                        dosMan.Misbehaving(pfrom, nDoS);
#endif
                    }
#endif                    
                }
            }
        }
    }
}
