// Copyright (c) 2015 G. Andrew Stone
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "unlimited.h"

#include "base58.h"
#include "blockrelay/graphene.h"
#include "blockrelay/thinblock.h"
#include "blockstorage/blockstorage.h"
#include "cashaddrenc.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "connmgr.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "dosman.h"
#include "dstencode.h"
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
#include "script/standard.h"
#include "stat.h"
#include "timedata.h"
#include "tinyformat.h"
#include "tweak.h"
#include "txadmission.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validation/validation.h"
#include "validationinterface.h"
#include "version.h"

#include <atomic>
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

// We always start with true so that when ActivateBestChain is called during the startup (init.cpp)
// and we havn't finished initial sync then we don't accidentally trigger the auto-dbcache
// resize. After ActivateBestChain the fIsInitialBlockDownload flag is set to true or false depending
// on whether we really have finished sync or not.
static std::atomic<bool> fIsInitialBlockDownload{true};

extern CTweakRef<uint64_t> miningBlockSize;
extern CTweakRef<uint64_t> ebTweak;

int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;
static const int NEW_CANDIDATE_INTERVAL = 30; // seconds

bool IsTrafficShapingEnabled();
UniValue validateblocktemplate(const UniValue &params, bool fHelp);
UniValue validatechainhistory(const UniValue &params, bool fHelp);

bool MiningAndExcessiveBlockValidatorRule(const uint64_t newExcessiveBlockSize, const uint64_t newMiningBlockSize)
{
    // The mined block size must be less then or equal too the excessive block size.
    LOGA("newMiningBlockSize: %d - newExcessiveBlockSize: %d\n", newMiningBlockSize, newExcessiveBlockSize);
    return (newMiningBlockSize <= newExcessiveBlockSize);
}

std::string AcceptDepthValidator(const unsigned int &value, unsigned int *item, bool validate)
{
    if (!validate)
    {
        settingsToUserAgentString();
    }
    return std::string();
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
        if ((value < MIN_EXCESSIVE_BLOCK_SIZE) &&
            ((Params().NetworkIDString() == "main") || (Params().NetworkIDString() == "test")))
        {
            std::ostringstream ret;
            ret << Params().NetworkIDString() << "Sorry, your proposed excessive block size (" << value
                << ") is smaller than the minimum EB size (" << MIN_EXCESSIVE_BLOCK_SIZE
                << ").  This would cause you to orphan blocks from the rest of the net.";
            return ret.str();
        }
        if ((value < MIN_EXCESSIVE_BLOCK_SIZE_REGTEST) && (Params().NetworkIDString() == "regtest"))
        {
            std::ostringstream ret;
            ret << Params().NetworkIDString() << "Sorry, your proposed excessive block size (" << value
                << ") is smaller than the minimum EB size (" << MIN_EXCESSIVE_BLOCK_SIZE_REGTEST
                << ").  This would cause you to orphan blocks from the rest of the net.";
            return ret.str();
        }
    }
    else // Do anything to "take" the new value
    {
        settingsToUserAgentString();
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

std::string MaxDataCarrierValidator(const unsigned int &value, unsigned int *item, bool validate)
{
    if (validate)
    {
        if (value < MAX_OP_RETURN_RELAY) // sanity check
        {
            return "Invalid Value. Data Carrier minimum size has to be greater of equal to 223 bytes";
        }
    }
    else // Do anything to "take" the new value
    {
        // nothing needed
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

std::string Bip135VoteValidator(const std::string &value, std::string *item, bool validate)
{
    if (validate)
    {
        bool categoriesValid = AssignBip135Votes(value, -1);
        if (!categoriesValid)
            return std::string("Invalid/unknown features specified");
    }
    else // Do what is needed to use the new value already stored in item
    {
        ClearBip135Votes();
        AssignBip135Votes(*item, 1);
        SignalBlockTemplateChange();
    }
    return std::string();
}

// Ensure that only one fork can be active at a time, update the UA string, and convert values of 1 to the
// fork time default.
std::string ForkTimeValidator(const uint64_t &value, uint64_t *item, bool validate)
{
    if (validate)
    {
        if (value != 0 && miningSvForkTime.Value() != 0)
        {
            std::ostringstream ret;
            ret << "Only one fork can be enabled at a time";
            return ret.str();
        }
    }
    else // If it was just turned "on" then set to the default activation time.
    {
        if (*item == 1)
        {
            *item = Params().GetConsensus().nov2018ActivationTime;
        }
        settingsToUserAgentString();
    }
    return std::string();
}

// Ensure that only one fork can be active at a time, update the UA string, and convert values of 1 to the
// fork time default.
std::string ForkTimeValidatorSV(const uint64_t &value, uint64_t *item, bool validate)
{
    if (validate)
    {
        if (value != 0 && miningForkTime.Value() != 0)
        {
            std::ostringstream ret;
            ret << "Only one fork can be enabled at a time";
            return ret.str();
        }
    }
    else // If it was just turned "on" then set to the default activation time.
    {
        if (*item == 1)
        {
            *item = Params().GetConsensus().nov2018ActivationTime;
        }
        settingsToUserAgentString();
    }
    return std::string();
}
// Push all transactions in the mempool to another node
void UnlimitedPushTxns(CNode *dest);

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
    std::string ret = ss.str() + customComment;
    return ret;
}

CNodeRef FindLikelyNode(const std::string &addrName)
{
    LOCK(cs_vNodes);
    // always match any beginning part of string to be
    // compatible with old implementation of FindLikelyNode(..)
    std::string match_str = (addrName[addrName.size() - 1] == '*') ? addrName : addrName + "*";
    for (CNode *pnode : vNodes)
    {
        if (wildmatch(match_str, pnode->addrName))
            return pnode;
    }
    return nullptr;
}

UniValue expedited(const UniValue &params, bool fHelp)
{
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
    LOCK(dest->cs_filter);
    std::vector<uint256> vtxid;
    mempool.queryHashes(vtxid);
    vector<CInv> vInv;
    for (uint256 &hash : vtxid)
    {
        CInv inv(MSG_TX, hash);
        CTransactionRef ptx = nullptr;
        ptx = mempool.get(hash);
        if (ptx == nullptr)
            continue; // another thread removed since queryHashes, maybe...
        if ((dest->pfilter && dest->pfilter->IsRelevantAndUpdate(ptx)) || (!dest->pfilter))
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

    std::string flavor;
    if (miningSvForkTime.Value() != 0)
        BUComments.push_back("SV");

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

    // If the user configures it to 1, assume this means default
    if (miningForkTime.Value() == 1)
        miningForkTime = Params().GetConsensus().nov2018ActivationTime;
    if (miningSvForkTime.Value() == 1)
        miningSvForkTime = Params().GetConsensus().nov2018ActivationTime;

    if (miningForkTime.Value() != 0 && miningSvForkTime.Value() != 0)
    {
        LOGA("Both the SV and ABC forks are enabled.  You must choose one.");
        printf("Both the SV and ABC forks are enabled.  You must choose one.\n");
        exit(1);
    }

    if (maxGeneratedBlock > excessiveBlockSize)
    {
        LOGA("Reducing the maximum mined block from the configured %d to your excessive block size %d.  Otherwise "
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
        LOGA(
            "Reducing -maxoutconnections from %d to %d, because this value is higher than max available connections.\n",
            nUserMaxOutConnections, nMaxConnections);
        nMaxOutConnections = nMaxConnections;
    }

    // Start Internal CPU miner
    // Generate coins in the background
    GenerateBitcoins(GetBoolArg("-gen", DEFAULT_GENERATE), GetArg("-genproclimit", DEFAULT_GENERATE_THREADS), Params());


    // Modify checkpoints depending on whether BCH or SV fork
    if (Params().NetworkIDString() == "main")
    {
        CCheckpointData &checkpoints = ModifiableParams().ModifiableCheckpoints();
        if (nMiningSvForkTime == 0)
        {
            // Nov 15th 2018 activate LTOR, DSV op_code
            checkpoints.mapCheckpoints[556767] =
                uint256S("0000000000000000004626ff6e3b936941d341c5932ece4357eeccac44e6d56c");
            // * UNIX timestamp of last checkpoint block
            checkpoints.nTimeLastCheckpoint = 1542304936;
            // * total number of transactions between genesis and last checkpoint
            checkpoints.nTransactionsLastCheckpoint = 265567564;
            // * estimated number of transactions per day after checkpoint (~3.5 TPS)
            checkpoints.fTransactionsPerDay = 280000.0;
        }
        else if (nMiningSvForkTime != 0)
        {
            // Nov 15th 2018 SV fork, 128MB blocks, re-enable bitcoin 0.1.0 op_codes
            checkpoints.mapCheckpoints[556767] =
                uint256S("000000000000000001d956714215d96ffc00e0afda4cd0a96c96f8d802b1662b");
            // * UNIX timestamp of last checkpoint block
            checkpoints.nTimeLastCheckpoint = 1542305817;
            // * total number of transactions between genesis and last checkpoint
            checkpoints.nTransactionsLastCheckpoint = 265615408;
            // * estimated number of transactions per day after checkpoint (~3.5 TPS)
            checkpoints.fTransactionsPerDay = 280000.0;
        }
        else
        {
            // unknown scenario, dont update these values or add a new checkpoint
            // * UNIX timestamp of last checkpoint block
            checkpoints.nTimeLastCheckpoint = 1526410186;
            // * total number of transactions between genesis and last checkpoint
            checkpoints.nTransactionsLastCheckpoint = 249416375;
            // * estimated number of transactions per day after checkpoint (~3.5 TPS)
            checkpoints.fTransactionsPerDay = 280000.0;
        }
    }
}

FILE *blockReceiptLog = nullptr;

void UnlimitedCleanup()
{
    txAdded.Stop();
    poolSize.Stop();
    recvAmt.Stop();
    sendAmt.Stop();
    nTxValidationTime.Stop();
    {
        LOCK(cs_blockvalidationtime);
        nBlockValidationTime.Stop();
    }

    CStatBase *obj = nullptr;
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
        long int byteLen = block.GetBlockSize();
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
    LOGA("%s\n", pblock->ToString());
    LOGA("generated %s\n", FormatMoney(pblock->vtx[0]->vout[0].nValue));

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
        if (!ProcessNewBlock(state, chainparams, nullptr, pblock, true, nullptr, false))
            return error("BitcoinMiner: ProcessNewBlock, block not accepted");
    }

    return true;
}


void static BitcoinMiner(const CChainParams &chainparams)
{
    LOGA("BitcoinMiner started\n");
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
                LOGA("Error in BitcoinMiner: Keypool ran out, please call keypoolrefill before restarting the "
                     "mining thread\n");
                return;
            }
            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, nExtraNonce);

            LOGA("Running BitcoinMiner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
                pblock->GetBlockSize());

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
                        LOGA("BitcoinMiner:\n");
                        LOGA("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
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
        LOGA("BitcoinMiner terminated\n");
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LOGA("BitcoinMiner runtime error: %s\n", e.what());
        return;
    }
}

void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams &chainparams)
{
    static boost::thread_group *minerThreads = nullptr;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads != nullptr)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = nullptr;
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
        LOGA("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64 " Sig:%d  :too many bytes\n",
            block.nVersion, block.nTime, blockSize, nTx, nSigOps);
        return true;
    }

    if (blockSize > BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
    {
        // Check transaction size to limit sighash
        if (largestTx > maxTxSize.Value())
        {
            LOGA("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64
                 " largest TX:%d  :tx too large.  Expected less than: %d\n",
                block.nVersion, block.nTime, blockSize, nTx, largestTx, maxTxSize.Value());
            return true;
        }

        // check proportional sigops
        uint64_t blockMbSize =
            1 + ((blockSize - 1) /
                    1000000); // block size in megabytes rounded up. 1-1000000 -> 1, 1000001-2000000 -> 2, etc.
        if (nSigOps > blockSigopsPerMb.Value() * blockMbSize)
        {
            LOGA("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64
                 " Sig:%d  :too many sigops.  Expected less than: %d\n",
                block.nVersion, block.nTime, blockSize, nTx, nSigOps, blockSigopsPerMb.Value() * blockMbSize);
            return true;
        }
    }
    else
    {
        // Within a 1MB block transactions can be 1MB, so nothing to check WRT transaction size

        // Check max sigops
        if (nSigOps > BLOCKSTREAM_CORE_MAX_BLOCK_SIGOPS)
        {
            LOGA("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64
                 " Sig:%d  :too many sigops.  Expected < 1MB defined constant: %d\n",
                block.nVersion, block.nTime, blockSize, nTx, nSigOps, BLOCKSTREAM_CORE_MAX_BLOCK_SIGOPS);
            return true;
        }
    }

    LOGA("Acceptable block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64 " Sig:%d\n", block.nVersion, block.nTime,
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
    ret.pushKV("excessiveBlockSize", (uint64_t)excessiveBlockSize);
    ret.pushKV("excessiveAcceptDepth", (uint64_t)excessiveAcceptDepth);
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
        ret.pushKV("sendBurst", max / 1024);
        ret.pushKV("sendAve", avg / 1024);
    }
    receiveShaper.get(&max, &avg);
    if (avg != std::numeric_limits<long long>::max() || max != std::numeric_limits<long long>::max())
    {
        ret.pushKV("recvBurst", max / 1024);
        ret.pushKV("recvAve", avg / 1024);
    }
    return ret;
}

UniValue settrafficshaping(const UniValue &params, bool fHelp)
{
    bool disable = false;
    bool badArg = false;
    CLeakyBucket *bucket = nullptr;
    if (params.size() >= 2)
    {
        const string strCommand = params[0].get_str();
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

    if (fHelp || badArg || bucket == nullptr)
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
void IsInitialBlockDownloadInit(bool *fInit)
{
    // For unit testing purposes, this step allows us to explicitly set the state of block sync.
    if (fInit)
    {
        fIsInitialBlockDownload.store(*fInit);
        return;
    }

    const CChainParams &chainParams = Params();
    LOCK(cs_main);
    if (!pindexBestHeader.load())
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

    // Using fInitialSyncComplete, once the chain is caught up the first time, and if we fall behind again due to a
    // large re-org or for lack of mined blocks, then we continue to return false for IsInitialBlockDownload().
    static bool fInitialSyncComplete = false;
    if (fInitialSyncComplete)
    {
        fIsInitialBlockDownload.store(false);
        return;
    }

    bool state = (chainActive.Height() < pindexBestHeader.load()->nHeight - 24 * 6 ||
                  std::max(chainActive.Tip()->GetBlockTime(), pindexBestHeader.load()->GetBlockTime()) <
                      GetTime() - nMaxTipAge);
    if (!state)
        fInitialSyncComplete = true;
    fIsInitialBlockDownload.store(state);
    return;
}

bool IsInitialBlockDownload() { return fIsInitialBlockDownload.load(); }
// fIsChainNearlySyncd is updated only during startup and whenever we receive a header.
// This way we avoid having to lock cs_main so often which tends to be a bottleneck.
void IsChainNearlySyncdInit()
{
    LOCK(cs_main);
    if (!pindexBestHeader.load())
    {
        // Not nearly synced if we don't have any blocks!
        fIsChainNearlySyncd.store(false);
    }
    else
    {
        if (chainActive.Height() < pindexBestHeader.load()->nHeight - 2)
            fIsChainNearlySyncd.store(false);
        else
            fIsChainNearlySyncd.store(true);
    }
}

bool IsChainNearlySyncd() { return fIsChainNearlySyncd.load(); }
// Used for unit tests to artificially set the state of chain sync
void IsChainNearlySyncdSet(bool fSync) { fIsChainNearlySyncd.store(fSync); }
bool IsChainSyncd()
{
    // lock free since both are atomics
    return pindexBestHeader.load() == chainActive.Tip();
}
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
        dosMan.Misbehaving(pfrom, 100);
    else
    {
        uint64_t nSizeFilter;
        {
            LOCK(pfrom->cs_filter);
            nSizeFilter = ::GetSerializeSize(*pfrom->pThinBlockFilter, SER_NETWORK, PROTOCOL_VERSION);
            thindata.UpdateInBoundBloomFilter(nSizeFilter);
            delete pfrom->pThinBlockFilter;
            pfrom->pThinBlockFilter = new CBloomFilter(*filter);
        }
        LOG(THIN, "Thinblock Bloom filter size: %d\n", nSizeFilter);
    }
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
    // Ensure that if there is a checkpoint on this height, that this block is the one.
    if (fCheckpointsEnabled && !CheckAgainstCheckpoint(pindexPrev->nHeight + 1, block.GetHash(), chainparams))
        return error("%s: CheckAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, state, fCheckPOW, fCheckMerkleRoot))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev, true))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, chainparams, true))
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
    return nullptr;
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
    bool verbose = false;

    // check for param  --verbose or -v
    string::size_type params_offset = 0;
    if (params[0].isStr() && (params[0].get_str() == "--verbose" || params[0].get_str() == "-v"))
    {
        verbose = true;
        ++params_offset;
    }

    int count = 0;
    if (params.size() < (3 + params_offset))
        count = 1; // if a count is not specified, give the latest sample
    else
    {
        if (!params[2 + params_offset].isNum())
        {
            try
            {
                count = boost::lexical_cast<int>(params[2 + params_offset].get_str());
            }
            catch (const boost::bad_lexical_cast &)
            {
                fHelp = true;
                specificIssue = "Invalid argument 3 \"count\" -- not a number";
            }
        }
        else
        {
            count = params[2 + params_offset].get_int();
        }
    }
    if (fHelp || (params.size() < (1 + params_offset)))
        throw runtime_error("getstat"
                            "\nReturns the current settings for the network send and receive bandwidth and burst in "
                            "kilobytes per second.\nTo get a list of available statistics use \"getstatlist\".\n"
                            "\nArguments: \n"
                            "1. \"-v\" or \"--verbose\" (string, optional) full details\n"
                            "2. \"statistic\"     (string, required) Specify what statistic you want\n"
                            "3. \"series\"  (string, optional) Specify what data series you want.  Options are "
                            "\"total\", \"now\",\"all\", \"sec10\", \"min5\", \"hourly\", \"daily\",\"monthly\".  "
                            "Default is all.\n"
                            "4. \"count\"  (string, optional) Specify the number of samples you want.\n"

                            "\nResult:\n"
                            "  {\n"
                            "    \"<statistic name>\"\n"
                            "    {\n"
                            "    \"<series meta>\"\n (Only with --verbose|-v) "
                            "      [\n"
                            "        \"Series\": Requested series.\n"
                            "        \"SampleSize\": Requested sample group size.\"\n"
                            "      ],\n"
                            "    \"<series name>\"\n"
                            "      [\n"
                            "      <data>, (any type) The data points in the series\n"
                            "      ],\n"
                            "    \"timestamp\"\n"
                            "      [\n"
                            "      <time> (time only with --verbose|-v)\n"
                            "      ],\n"
                            "    ...\n"
                            "    },\n"
                            "  ...\n"
                            "  }\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getstat", "") + HelpExampleRpc("getstat", "") + "\n" + specificIssue);

    UniValue ret(UniValue::VARR);

    string seriesStr;
    if (params.size() < (2 + params_offset))
        seriesStr = "total";
    else
        seriesStr = params[1 + params_offset].get_str();
    // uint_t series = 0;
    // if (series == "now") series |= 1;
    // if (series == "all") series = 0xfffffff;
    LOCK(cs_statMap);

    CStatBase *base = FindStatistic(params[0 + params_offset].get_str().c_str());
    if (base)
    {
        UniValue ustat(UniValue::VOBJ);
        if (seriesStr == "now")
        {
            ustat.pushKV("now", base->GetNow());
        }
        else if (seriesStr == "total")
        {
            ustat.pushKV("total", base->GetTotal());
        }
        else
        {
            UniValue series;
            if (verbose)
            {
                series = base->GetSeriesTime(seriesStr, count);

                string metaStr = "meta";
                UniValue metaData(UniValue::VARR);
                metaData.push_back("Series:" + seriesStr);
                metaData.push_back("SampleSize:" + boost::lexical_cast<std::string>(count));
                ustat.pushKV(metaStr, metaData);
                ustat.pushKV(seriesStr, series[0]);
                ustat.pushKV("timestamp", series[1]);
            }
            else
            {
                series = base->GetSeries(seriesStr, count);
                ustat.pushKV(seriesStr, series);
            }
        }

        ret.push_back(ustat);
    }
    return ret;
}

UniValue setlog(const UniValue &params, bool fHelp)
{
    // Uses internal log functions
    // Logging::
    // Don't use them in other places.

    UniValue ret = UniValue("");
    uint64_t catg = NONE;
    int nparm = params.size();
    bool action = false;

    if (fHelp || nparm > 2)
    {
        throw runtime_error(
            "log \"category|all\" \"on|off\""
            "\nTurn categories on or off\n"
            "\nWith no arguments it returns a list of currently on log categories\n"
            "\nArguments:\n"
            "1. \"category|all\" (string, required) Category or all categories\n"
            "2. \"on\"           (string, optional) Turn a category, or all categories, on\n"
            "2. \"off\"          (string, optional) Turn a category, or all categories, off\n"
            "2.                (string, optional) No argument. Show a category, or all categories, state: on|off\n" +
            HelpExampleCli("log", "\"NET\" on") + HelpExampleCli("log", "\"all\" off") +
            HelpExampleCli("log", "\"tor\" ") + HelpExampleCli("log", "\"ALL\" ") + HelpExampleCli("log", " "));
    }

    try
    {
        if (nparm == 0)
            ret = UniValue(Logging::LogGetAllString(true));

        if (nparm > 0)
        {
            std::string category;
            std::string data = params[0].get_str();
            std::transform(data.begin(), data.end(), std::back_inserter(category), ::tolower);
            catg = Logging::LogFindCategory(category);
            if (catg == NONE)
                return UniValue("Category not found: " + params[0].get_str()); // quit
        }

        switch (nparm)
        {
        case 1:
            if (catg == ALL)
                ret = UniValue(Logging::LogGetAllString());
            else
                ret = UniValue(Logging::LogAcceptCategory(catg) ? "on" : "off");
            break;

        case 2:
            try
            {
                action = IsStringTrue(params[1].get_str());
            }
            catch (...)
            {
                ret = UniValue("Please pass on|off as last argument.");
                break; // quit
            }

            Logging::LogToggleCategory(catg, action);

        default:
            break;
        }
    }
    catch (...)
    {
        LOG(ALL, "LOG: Something went wrong in setlog function \n");
        ret = UniValue("Something went wrong. That is all we know.");
    }

    return ret;
}

/** Mining-Candidate begin */

/** Oustanding candidates are removed 30 sec after a new block has been found*/
static void RmOldMiningCandidates()
{
    LOCK(cs_main);
    static unsigned int prevheight = 0;
    unsigned int height = GetBlockchainHeight();

    if (height <= prevheight)
        return;

    int64_t tdiff = GetTime() - (chainActive.Tip()->nTime + NEW_CANDIDATE_INTERVAL);
    if (tdiff >= 0)
    {
        // Clean out mining candidates that are the same height as a discovered block.
        for (auto it = miningCandidatesMap.cbegin(); it != miningCandidatesMap.cend();)
        {
            if (it->second.block.GetHeight() <= prevheight)
            {
                it = miningCandidatesMap.erase(it);
            }
            else
            {
                ++it;
            }
        }
        prevheight = height;
    }
}

static void AddMiningCandidate(CMiningCandidate &candid, int64_t id)
{
    // Save candidate so can be looked up:
    LOCK(cs_main);
    miningCandidatesMap[id] = candid;
}

std::vector<uint256> GetMerkleProofBranches(CBlock *pblock)
{
    std::vector<uint256> ret;
    std::vector<uint256> leaves;
    int len = pblock->vtx.size();

    for (int i = 0; i < len; i++)
    {
        leaves.push_back(pblock->vtx[i].get()->GetHash());
    }

    ret = ComputeMerkleBranch(leaves, 0);
    return ret;
}

/** Create Mining-Candidate JSON to send to miner */
static UniValue MkMiningCandidateJson(CMiningCandidate &candid)
{
    static int64_t id = 0;
    UniValue ret(UniValue::VOBJ);
    CBlock &block = candid.block;

    RmOldMiningCandidates();

    // Save candidate so can be looked up:
    id++;
    AddMiningCandidate(candid, id);
    ret.pushKV("id", id);

    ret.pushKV("prevhash", block.hashPrevBlock.GetHex());

    {
        const CTransaction *tran = block.vtx[0].get();
        ret.pushKV("coinbase", EncodeHexTx(*tran));
    }

    ret.pushKV("version", block.nVersion);
    ret.pushKV("nBits", strprintf("%08x", block.nBits));
    ret.pushKV("time", block.GetBlockTime());

    // merkleProof:
    {
        std::vector<uint256> brancharr = GetMerkleProofBranches(&block);
        UniValue merkleProof(UniValue::VARR);
        for (const auto &i : brancharr)
        {
            merkleProof.push_back(i.GetHex());
        }
        ret.pushKV("merkleProof", merkleProof);

        // merklePath parameter:
        // If the coinbase is ever allowed to be anywhere in the hash tree via a hard fork, we will need to communicate
        // how to calculate the merkleProof by supplying a bit for every level in the proof.
        // This bit tells the calculator whether the next hash is on the left or right side of the tree.
        // In other words, whether to do cat(A,B) or cat(B,A).  Specifically, if the bit is 0,the proof calcuation uses
        // Hash256(concatentate(running hash, next hash in proof)), if the bit is 1, the proof calculates
        // Hash256(concatentate(next hash in proof, running hash))

        // ret.pushKV("merklePath", 0);
    }

    return ret;
}

/** RPC Get a block candidate*/
UniValue getminingcandidate(const UniValue &params, bool fHelp)
{
    UniValue ret(UniValue::VOBJ);
    CMiningCandidate candid;
    int64_t coinbaseSize = -1; // If -1 then not used to set coinbase size

    if (fHelp || params.size() > 1)
    {
        throw runtime_error("getminingcandidate"
                            "\nReturns Mining-Candidate protocol data.\n"
                            "\nArguments:\n"
                            "1. \"coinbasesize\" (int, optional) Get a fixed size coinbase transaction.\n" +
                            HelpExampleCli("", "") + HelpExampleCli("coinbasesize", "100"));
    }

    if (params.size() == 1)
    {
        coinbaseSize = params[0].get_int64();
        if (coinbaseSize < 0)
        {
            throw std::runtime_error("Requested coinbase size is less than 0");
        }

        if (coinbaseSize > BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
        {
            throw std::runtime_error(
                strprintf("Requested coinbase size too big. Max allowed: %u", BLOCKSTREAM_CORE_MAX_BLOCK_SIZE));
        }
    }

    mkblocktemplate(UniValue(UniValue::VARR), coinbaseSize, &candid.block);

    ret = MkMiningCandidateJson(candid);
    return ret;
}

/** RPC Submit a solved block candidate*/
UniValue submitminingsolution(const UniValue &params, bool fHelp)
{
    UniValue rcvd;
    CBlock block;
    LOCK(cs_main);

    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "submitminingsolution \"Mining-Candidate data\" ( \"jsonparametersobject\" )\n"
            "\nAttempts to submit a new block to the network.\n"
            "\nArguments\n"
            "1. \"submitminingsolutiondata\"    (string, required) the mining solution (JSON encoded) data to submit\n"
            "\nResult:\n"
            "\nNothing on success, error string if block was rejected.\n"
            "Identical to \"submitblock\".\n"
            "\nExamples:\n" +
            HelpExampleRpc("submitminingsolution", "\"mydata\""));
    }

    rcvd = params[0].get_obj();

    int64_t id = rcvd["id"].get_int64();

    // Needs LOCK(cs_main); above:
    if (miningCandidatesMap.count(id) == 1)
    {
        block = miningCandidatesMap[id].block;
        miningCandidatesMap.erase(id);
    }
    else
    {
        return UniValue("id not found");
    }

    UniValue nonce = rcvd["nonce"];
    if (nonce.isNull())
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "nonce not found");
    }
    block.nNonce = (uint32_t)nonce.get_int64(); // 64 bit to deal with sign bit in 32 bit unsigned int

    UniValue time = rcvd["time"];
    if (!time.isNull())
    {
        block.nTime = (uint32_t)time.get_int64();
    }

    UniValue version = rcvd["version"];
    if (!version.isNull())
    {
        block.nVersion = version.get_int(); // version signed 32 bit int
    }

    // Coinbase:
    CTransaction coinbase;
    UniValue cbhex = rcvd["coinbase"];
    if (!cbhex.isNull())
    {
        if (DecodeHexTx(coinbase, cbhex.get_str()))
            block.vtx[0] = MakeTransactionRef(std::move(coinbase));
        else
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "coinbase decode failed");
        }
    }

    // MerkleRoot:
    {
        std::vector<uint256> merkleProof = GetMerkleProofBranches(&block);
        uint256 t = block.vtx[0]->GetHash();
        block.hashMerkleRoot = CalculateMerkleRoot(t, merkleProof);
    }

    UniValue uvsub = SubmitBlock(block); // returns string on failure
    RmOldMiningCandidates();
    return uvsub;
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

uint256 CalculateMerkleRoot(uint256 &coinbase_hash, const std::vector<uint256> &merkleProof)
{
    uint256 merkle_root = coinbase_hash;
    for (unsigned int i = 0; i < merkleProof.size(); i++)
    {
        CalculateNextMerkleRoot(merkle_root, merkleProof[i]);
    }
    return merkle_root;
}

/** Mining-Candidate end */

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
    { "mining",             "getminingcandidate",     &getminingcandidate,     true  },
    { "mining",             "submitminingsolution",   &submitminingsolution,   true  },

    /* Utility functions */
    { "util",               "getstatlist",            &getstatlist,            true  },
    { "util",               "getstat",                &getstat,                true  },
    { "util",               "get",                    &gettweak,               true  },
    { "util",               "set",                    &settweak,               true  },
    { "util",               "validatechainhistory",   &validatechainhistory,   true  },
#ifdef DEBUG
    { "util",               "getstructuresizes",      &getstructuresizes,      true  },  // BU
#endif
    { "util",               "getaddressforms",        &getaddressforms,        true  },
    { "util",               "log",                    &setlog,                 true  },
    /* Coin generation */
    { "generating",         "getgenerate",            &getgenerate,            true  },
    { "generating",         "setgenerate",            &setgenerate,            true  },
};
/* clang-format on */

void RegisterUnlimitedRPCCommands(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}


UniValue validatechainhistory(const UniValue &params, bool fHelp)
{
    if (fHelp)
        throw runtime_error("validatechainhistory [hash]\n"
                            "\nUpdates a chain's valid/invalid status based on parent blocks.\n");

    std::stack<CBlockIndex *> stk;
    CBlockIndex *pos = pindexBestHeader.load();
    bool failedChain = false;
    UniValue ret = NullUniValue;

    if (params.size() >= 1)
    {
        std::string strHash = params[0].get_str();
        uint256 hash(uint256S(strHash));

        if ((pos = LookupBlockIndex(hash)) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    LOGA("validatechainhistory starting at %d %s\n", pos->nHeight, pos->phashBlock->ToString());

    LOCK(cs_main); // modifying contents of CBlockIndex

    while (pos && !failedChain)
    {
        // LOGA("validate %d %s\n", pos->nHeight, pos->phashBlock->ToString());
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

    CBlockIndex *pindexPrev = nullptr;

    pindexPrev = LookupBlockIndex(block.hashPrevBlock);
    if (!pindexPrev)
        throw runtime_error("invalid block: unknown parent");

    if (pindexPrev != chainActive.Tip())
    {
        throw runtime_error("invalid block: does not build on chain tip");
    }

    DbgAssert(pindexPrev, throw runtime_error("invalid block: unknown parent"));

    const CChainParams &chainparams = Params();
    CValidationState state;

    {
        LOCK(cs_main); // to freeze the state during block validity test

        if (block.GetBlockSize() <= BLOCKSTREAM_CORE_MAX_BLOCK_SIZE)
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
extern std::map<uint256, CTxCommitData> *txCommitQ;
extern std::queue<CTxInputData> txDeferQ;
extern std::queue<CTxInputData> txInQ;
extern UniValue getstructuresizes(const UniValue &params, bool fHelp)
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("time", GetTime());
    ret.pushKV("requester.mapTxnInfo", (uint64_t)requester.mapTxnInfo.size());
    ret.pushKV("requester.mapBlkInfo", (uint64_t)requester.mapBlkInfo.size());
    unsigned long int max = 0;
    unsigned long int size = 0;
    for (CRequestManager::OdMap::iterator i = requester.mapTxnInfo.begin(); i != requester.mapTxnInfo.end(); i++)
    {
        unsigned long int temp = i->second.availableFrom.size();
        size += temp;
        if (max < temp)
            max = temp;
    }
    ret.pushKV("requester.mapTxnInfo.maxobj", max);
    ret.pushKV("requester.mapTxnInfo.totobj", size);

    max = 0;
    size = 0;
    for (CRequestManager::OdMap::iterator i = requester.mapBlkInfo.begin(); i != requester.mapBlkInfo.end(); i++)
    {
        unsigned long int temp = i->second.availableFrom.size();
        size += temp;
        if (max < temp)
            max = temp;
    }
    ret.pushKV("requester.mapBlkInfo.maxobj", max);
    ret.pushKV("requester.mapBlkInfo.totobj", size);

    ret.pushKV("mapBlockIndex", (int64_t)mapBlockIndex.size());
    // CChain
    {
        LOCK(cs_xval);
        ret.pushKV("setPreVerifiedTxHash", (int64_t)setPreVerifiedTxHash.size());
        ret.pushKV("setUnVerifiedOrphanTxHash", (int64_t)setUnVerifiedOrphanTxHash.size());
    }
    ret.pushKV("mapLocalHost", (int64_t)mapLocalHost.size());
    ret.pushKV("CDoSManager::vWhitelistedRange", (int64_t)dosMan.vWhitelistedRange.size());
    ret.pushKV("mapInboundConnectionTracker", (int64_t)mapInboundConnectionTracker.size());
    ret.pushKV("vUseDNSSeeds", (int64_t)vUseDNSSeeds.size());
    ret.pushKV("vAddedNodes", (int64_t)vAddedNodes.size());
    ret.pushKV("setservAddNodeAddresses", (int64_t)setservAddNodeAddresses.size());
    ret.pushKV("statistics", (int64_t)statistics.size());
    ret.pushKV("tweaks", (int64_t)tweaks.size());
    ret.pushKV("mapRelay", (int64_t)mapRelay.size());
    ret.pushKV("vRelayExpiration", (int64_t)vRelayExpiration.size());
    ret.pushKV("vNodes", (int64_t)vNodes.size());
    ret.pushKV("vNodesDisconnected", (int64_t)vNodesDisconnected.size());
    // CAddrMan
    ret.pushKV("mapOrphanTransactions", (int64_t)orphanpool.mapOrphanTransactions.size());
    ret.pushKV("mapOrphanTransactionsByPrev", (int64_t)orphanpool.mapOrphanTransactionsByPrev.size());

    uint32_t nExpeditedBlocks, nExpeditedTxs, nExpeditedUpstream;
    connmgr->ExpeditedNodeCounts(nExpeditedBlocks, nExpeditedTxs, nExpeditedUpstream);
    ret.pushKV("xpeditedBlk", (uint64_t)nExpeditedBlocks);
    ret.pushKV("xpeditedBlkUp", (uint64_t)nExpeditedUpstream);
    ret.pushKV("xpeditedTxn", (uint64_t)nExpeditedTxs);

    if (txCommitQ)
        ret.pushKV("txCommitQ", (uint64_t)txCommitQ->size());
    ret.pushKV("txInQ", (uint64_t)txInQ.size());
    ret.pushKV("txDeferQ", (uint64_t)txDeferQ.size());

#ifdef DEBUG_LOCKORDER
    ret.pushKV("lockorders", (uint64_t)lockorders.size());
#endif

    LOCK(cs_vNodes);
    std::vector<CNode *>::iterator n;
    uint64_t totalThinBlockSize = 0;
    int disconnected = 0; // watch # of disconnected nodes to ensure they are being cleaned up
    for (std::vector<CNode *>::iterator it = vNodes.begin(); it != vNodes.end(); ++it)
    {
        if (*it == nullptr)
            continue;
        CNode &inode = **it;
        UniValue node(UniValue::VOBJ);
        disconnected += (inode.fDisconnect) ? 1 : 0;

        node.pushKV("vSendMsg", (int64_t)inode.vSendMsg.size());
        node.pushKV("vRecvGetData", (int64_t)inode.vRecvGetData.size());
        node.pushKV("vRecvMsg", (int64_t)inode.vRecvMsg.size());
        {
            LOCK(inode.cs_filter);
            if (inode.pfilter)
            {
                node.pushKV("pfilter", (int64_t)::GetSerializeSize(*inode.pfilter, SER_NETWORK, PROTOCOL_VERSION));
            }
            if (inode.pThinBlockFilter)
            {
                node.pushKV("pThinBlockFilter",
                    (int64_t)::GetSerializeSize(*inode.pThinBlockFilter, SER_NETWORK, PROTOCOL_VERSION));
            }
        }
        node.pushKV("thinblock.vtx", (int64_t)inode.thinBlock.vtx.size());
        uint64_t thinBlockSize = ::GetSerializeSize(inode.thinBlock, SER_NETWORK, PROTOCOL_VERSION);
        totalThinBlockSize += thinBlockSize;
        node.pushKV("thinblock.size", thinBlockSize);
        node.pushKV("thinBlockHashes", (int64_t)inode.thinBlockHashes.size());
        node.pushKV("xThinBlockHashes", (int64_t)inode.xThinBlockHashes.size());
        node.pushKV("vAddrToSend", (int64_t)inode.vAddrToSend.size());
        node.pushKV("vInventoryToSend", (int64_t)inode.vInventoryToSend.size());
        ret.pushKV(inode.addrName, node);
    }
    ret.pushKV("totalThinBlockSize", totalThinBlockSize);
    ret.pushKV("disconnectedNodes", disconnected);

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

    for (const PAIRTYPE(const uint256, CBlockIndex *) & item : mapBlockIndex)
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

    for (CBlockIndex *tip : setTips)
    {
        if (tip->GetAncestor(invalidBlock->nHeight) == invalidBlock)
        {
            for (CBlockIndex *blk = tip; blk != invalidBlock; blk = blk->pprev)
            {
                blk->nStatus |= BLOCK_FAILED_VALID;

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

UniValue getaddressforms(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error("getaddressforms \"address\"\n"
                            "\nReturns all ways of displaying this address.\n"
                            "\nArguments\n"
                            "1. \"address\"    (string, required) the address\n"
                            "\nResult:\n"
                            "{\n"
                            "\"legacy\": \"1 or 3 prefixed address\",\n"
                            "\"bitcoincash\": \"bitcoincash prefixed address\",\n"
                            "\"bitpay\": \"C or H prefixed address\"\n"
                            "}\n"
                            "\nExamples:\n" +
                            HelpExampleCli("getaddressforms", "\"address\"") +
                            HelpExampleRpc("getaddressforms", "\"address\""));

    UniValue ret(UniValue::VARR);
    CBlock block;

    CTxDestination dest = DecodeDestination(params[0].get_str());

    if (!IsValidDestination(dest))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address or script");
    }

    std::string cashAddr = EncodeCashAddr(dest, Params());
    std::string legacyAddr = EncodeLegacyAddr(dest, Params());
    std::string bitpayAddr = EncodeBitpayAddr(dest);

    UniValue node(UniValue::VOBJ);
    node.pushKV("legacy", legacyAddr);
    node.pushKV("bitcoincash", cashAddr);
    node.pushKV("bitpay", bitpayAddr);
    return node;
}

std::string CStatusString::GetPrintable() const
{
    LOCK(cs);
    if (strSet.empty())
        return "ready";
    std::string ret;
    for (auto it : strSet)
    {
        if (!ret.empty())
            ret.append(" ");
        std::string &s = it;
        ret.append(s);
    }
    return ret;
}

void CStatusString::Set(const std::string &yourStatus)
{
    LOCK(cs);
    strSet.insert(yourStatus);
}

void CStatusString::Clear(const std::string &yourStatus)
{
    LOCK(cs);
    strSet.erase(yourStatus);
}
