// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "blockstorage/blockstorage.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "dstencode.h"
#include "init.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "parallel.h"
#include "pow.h"
#include "rpc/server.h"
#include "txadmission.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation/validation.h"
#include "validationinterface.h"

#include <cstdlib>
#include <stdint.h>

#include <boost/assign/list_of.hpp>
#include <boost/shared_ptr.hpp>

#include <univalue.h>

using namespace std;

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
UniValue GetNetworkHashPS(int lookup, int height)
{
    CBlockIndex *pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (pb == nullptr || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pb->nHeight % Params().GetConsensus().DifficultyAdjustmentInterval() + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex *pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++)
    {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return workDiff.getdouble() / timeDiff;
}

UniValue getnetworkhashps(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getnetworkhashps ( blocks height )\n"
            "\nReturns the estimated network hashes per second based on the last n blocks.\n"
            "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
            "Pass in [height] to estimate the network speed at the time when a certain block was found.\n"
            "\nArguments:\n"
            "1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks since last "
            "difficulty change.\n"
            "2. height     (numeric, optional, default=-1) To estimate at the time of the given height.\n"
            "\nResult:\n"
            "x             (numeric) Hashes per second estimated\n"
            "\nExamples:\n" +
            HelpExampleCli("getnetworkhashps", "") + HelpExampleRpc("getnetworkhashps", ""));

    LOCK(cs_main);
    return GetNetworkHashPS(
        params.size() > 0 ? params[0].get_int() : 120, params.size() > 1 ? params[1].get_int() : -1);
}

UniValue generateBlocks(boost::shared_ptr<CReserveScript> coinbaseScript,
    int nGenerate,
    uint64_t nMaxTries,
    bool keepScript)
{
    static const int nInnerLoopCount = 0x10000;
    int nHeightStart = 0;
    int nHeightEnd = 0;
    int nHeight = 0;

    { // Don't keep cs_main locked
        LOCK(cs_main);
        nHeightStart = chainActive.Height();
        nHeight = nHeightStart;
        nHeightEnd = nHeightStart + nGenerate;
    }
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    while (nHeight < nHeightEnd)
    {
        std::unique_ptr<CBlockTemplate> pblocktemplate;
        {
            TxAdmissionPause lock; // flush any tx waiting to enter the mempool
            pblocktemplate = BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript);
        }
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        CBlock *pblock = &pblocktemplate->block;
        {
            // LOCK(cs_main);
            IncrementExtraNonce(pblock, nExtraNonce);
        }
        while (nMaxTries > 0 && pblock->nNonce < nInnerLoopCount &&
               !CheckProofOfWork(pblock->GetHash(), pblock->nBits, Params().GetConsensus()))
        {
            ++pblock->nNonce;
            --nMaxTries;
        }
        if (nMaxTries == 0)
        {
            break;
        }
        if (pblock->nNonce == nInnerLoopCount)
        {
            continue;
        }

        // In we are mining our own block or not running in parallel for any reason
        // we must terminate any block validation threads that are currently running,
        // Unless they have more work than our own block or are processing a chain
        // that has more work than our block.
        PV->StopAllValidationThreads(pblock->GetBlockHeader().nBits);

        CValidationState state;
        if (!ProcessNewBlock(state, Params(), nullptr, pblock, true, nullptr, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        // mark script as important because it was used at least for one coinbase output if the script came from the
        // wallet
        if (keepScript)
        {
            coinbaseScript->KeepScript();
        }
    }

    CValidationState state;
    LOCK(cs_main);
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS); // we made lots of blocks
    CBlockIndex *pindexNewTip = chainActive.Tip();
    uiInterface.NotifyBlockTip(false, pindexNewTip, false);
    return blockHashes;
}

UniValue generate(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error("generate numblocks ( maxtries )\n"
                            "\nMine up to numblocks blocks immediately (before the RPC call returns)\n"
                            "\nArguments:\n"
                            "1. numblocks    (numeric, required) How many blocks are generated immediately.\n"
                            "2. maxtries     (numeric, optional) How many iterations to try (default = 1000000).\n"
                            "\nResult\n"
                            "[ blockhashes ]     (array) hashes of blocks generated\n"
                            "\nExamples:\n"
                            "\nGenerate 11 blocks\n" +
                            HelpExampleCli("generate", "11"));

    int nGenerate = params[0].get_int();
    uint64_t nMaxTries = 1000000;
    if (params.size() > 1)
    {
        nMaxTries = params[1].get_int();
    }

    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);

    // If the keypool is exhausted, no script is returned at all.  Catch this.
    if (!coinbaseScript)
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    // throw an error if no script was provided
    if (coinbaseScript->reserveScript.empty())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No coinbase script available (mining requires a wallet)");

    return generateBlocks(coinbaseScript, nGenerate, nMaxTries, true);
}

UniValue generatetoaddress(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error("generatetoaddress numblocks address (maxtries)\n"
                            "\nMine blocks immediately to a specified address (before the RPC call returns)\n"
                            "\nArguments:\n"
                            "1. numblocks    (numeric, required) How many blocks are generated immediately.\n"
                            "2. address    (string, required) The address to send the newly generated bitcoin to.\n"
                            "3. maxtries     (numeric, optional) How many iterations to try (default = 1000000).\n"
                            "\nResult\n"
                            "[ blockhashes ]     (array) hashes of blocks generated\n"
                            "\nExamples:\n"
                            "\nGenerate 11 blocks to myaddress\n" +
                            HelpExampleCli("generatetoaddress", "11 \"myaddress\""));

    int nGenerate = params[0].get_int();
    uint64_t nMaxTries = 1000000;
    if (params.size() > 2)
    {
        nMaxTries = params[2].get_int();
    }

    CTxDestination destination = DecodeDestination(params[1].get_str());
    if (!IsValidDestination(destination))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    boost::shared_ptr<CReserveScript> coinbaseScript(new CReserveScript());
    coinbaseScript->reserveScript = GetScriptForDestination(destination);

    return generateBlocks(coinbaseScript, nGenerate, nMaxTries, false);
}

UniValue getmininginfo(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmininginfo\n"
            "\nReturns a json object containing mining-related information."
            "\nResult:\n"
            "{\n"
            "  \"blocks\": nnn,             (numeric) The current block\n"
            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
            "  \"difficulty\": xxx.xxxxx    (numeric) The current difficulty\n"
            "  \"errors\": \"...\"          (string) Current errors\n"
            "  \"generate\": true|false     (boolean) If the generation is on or off (see getgenerate or setgenerate "
            "calls)\n"
            "  \"genproclimit\": n          (numeric) The processor limit for generation. -1 if no generation. (see "
            "getgenerate or setgenerate calls)\n"
            "  \"pooledtx\": n              (numeric) The size of the mem pool\n"
            "  \"testnet\": true|false      (boolean) If using testnet or not\n"
            "  \"chain\": \"xxxx\",         (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmininginfo", "") + HelpExampleRpc("getmininginfo", ""));


    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks", (int)chainActive.Height());
    obj.pushKV("currentblocksize", (uint64_t)nLastBlockSize);
    obj.pushKV("currentblocktx", (uint64_t)nLastBlockTx);
    obj.pushKV("difficulty", (double)GetDifficulty());
    obj.pushKV("errors", GetWarnings("statusbar"));
    obj.pushKV("genproclimit", (int)GetArg("-genproclimit", DEFAULT_GENERATE_THREADS));
    obj.pushKV("networkhashps", getnetworkhashps(params, false));
    obj.pushKV("pooledtx", (uint64_t)mempool.size());
    obj.pushKV("testnet", Params().TestnetToBeDeprecatedFieldRPC());
    obj.pushKV("chain", Params().NetworkIDString());
    obj.pushKV("generate", getgenerate(params, false));
    return obj;
}


// NOTE: Unlike wallet RPC (which use BCH values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
UniValue prioritisetransaction(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "prioritisetransaction <txid> <priority delta> <fee delta>\n"
            "Accepts the transaction into mined blocks at a higher (or lower) priority\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id.\n"
            "2. priority delta (numeric, required) The priority to add or subtract.\n"
            "                  The transaction selection algorithm considers the tx as it would have a higher "
            "priority.\n"
            "                  (priority of a transaction is calculated: coinage * value_in_satoshis / txsize) \n"
            "3. fee delta      (numeric, required) The fee value (in satoshis) to add (or subtract, if negative).\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a "
            "block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee.\n"
            "\nResult\n"
            "true              (boolean) Returns true\n"
            "\nExamples:\n" +
            HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000") +
            HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000"));

    LOCK(cs_main);

    uint256 hash = ParseHashStr(params[0].get_str(), "txid");
    CAmount nAmount = params[2].get_int64();

    mempool.PrioritiseTransaction(hash, params[0].get_str(), params[1].get_real(), nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState &state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid())
    {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

std::string gbt_vb_name(const Consensus::DeploymentPos pos)
{
    const struct ForkDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
    std::string s = vbinfo.name;
    if (!vbinfo.gbt_force)
    {
        s.insert(s.begin(), '!');
    }
    return s;
}

// Sets the version bits in a block
static int32_t UtilMkBlockTmplVersionBits(int32_t version,
    std::set<std::string> setClientRules,
    CBlockIndex *pindexPrev,
    UniValue *paRules,
    UniValue *pvbavailable)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();

    for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j)
    {
        Consensus::DeploymentPos pos = Consensus::DeploymentPos(j);
        ThresholdState state = VersionBitsState(pindexPrev, consensusParams, pos, versionbitscache);
        switch (state)
        {
        case THRESHOLD_DEFINED:
        case THRESHOLD_FAILED:
            // Not exposed to GBT at all
            break;
        case THRESHOLD_LOCKED_IN:
            // Ensure bit is set in block version
            version |= VersionBitsMask(consensusParams, pos);
        // FALLTHROUGH
        // to get vbavailable set...
        case THRESHOLD_STARTED:
        {
            const struct ForkDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
            if (pvbavailable != nullptr)
            {
                pvbavailable->pushKV(gbt_vb_name(pos), consensusParams.vDeployments[pos].bit);
            }
            if (setClientRules.find(vbinfo.name) == setClientRules.end())
            {
                if (!vbinfo.gbt_force)
                {
                    // If the client doesn't support this, don't indicate it in the [default] version
                    version &= ~VersionBitsMask(consensusParams, pos);
                }
                if (vbinfo.myVote == true) // let the client vote for this feature
                    version |= VersionBitsMask(consensusParams, pos);
            }
            break;
        }
        case THRESHOLD_ACTIVE:
        {
            // Add to rules only
            const struct ForkDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
            if (paRules != nullptr)
            {
                paRules->push_back(gbt_vb_name(pos));
            }
            if (setClientRules.find(vbinfo.name) == setClientRules.end())
            {
                // Not supported by the client; make sure it's safe to proceed
                if (!vbinfo.gbt_force)
                {
                    // If we do anything other than throw an exception here, be sure version/force isn't sent to old
                    // clients
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                        strprintf("Support for '%s' rule requires explicit client support", vbinfo.name));
                }
            }
            break;
        }
        }
    }
    return version;
}


static UniValue MkFullMiningCandidateJson(std::set<std::string> setClientRules,
    CBlockIndex *pindexPrev,
    int64_t coinbaseSize,
    std::unique_ptr<CBlockTemplate> &pblocktemplate,
    const int nMaxVersionPreVB,
    const unsigned int nTransactionsUpdatedLast)
{
    bool may2020Enabled = IsMay2020Activated(Params().GetConsensus(), pindexPrev);
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience
    UniValue aCaps(UniValue::VARR);
    aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    map<uint256, int64_t> setTxIndex;
    int i = 0;
    int sigcheckTotal = 0;
    for (const auto &it : pblock->vtx)
    {
        const CTransaction &tx = *it;
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));

        entry.pushKV("hash", txHash.GetHex());

        UniValue deps(UniValue::VARR);
        for (const CTxIn &in : tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        if (!may2020Enabled)
            entry.pushKV("sigops", pblocktemplate->vTxSigOps[index_in_template]);
        else
        {
            // sigops is deprecated and not part of this block's consensus so report 0
            entry.pushKV("sigops", 0);
            entry.pushKV("sigchecks", pblocktemplate->vTxSigOps[index_in_template]);
            sigcheckTotal += pblocktemplate->vTxSigOps[index_in_template];
        }

        transactions.push_back(entry);
    }

    UniValue aRules(UniValue::VARR);
    UniValue vbavailable(UniValue::VOBJ);

    pblock->nVersion = UtilMkBlockTmplVersionBits(pblock->nVersion, setClientRules, pindexPrev, &aRules, &vbavailable);

    UniValue aux(UniValue::VOBJ);
    // COINBASE_FLAGS were assigned in CreateNewBlock() in the steps above.  Now we can use it here.
    {
        LOCK(cs_coinbaseFlags);
        aux.pushKV("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end()));
    }

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    UniValue aMutable(UniValue::VARR);
    aMutable.push_back("time");
    aMutable.push_back("transactions");
    aMutable.push_back("prevblock");

    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", aCaps);
    result.pushKV("version", pblock->nVersion);
    result.pushKV("rules", aRules);
    result.pushKV("vbavailable", vbavailable);
    result.pushKV("vbrequired", int(0));

    if (nMaxVersionPreVB >= 2)
    {
        // If VB is supported by the client, nMaxVersionPreVB is -1, so we won't get here
        // Because BIP 34 changed how the generation transaction is serialised, we can only use version/force back to v2
        // blocks
        // This is safe to do [otherwise-]unconditionally only because we are throwing an exception above if a non-force
        // deployment gets activated
        // Note that this can probably also be removed entirely after the first BIP9/BIP135 non-force deployment
        // (ie, segwit) gets activated
        aMutable.push_back("version/force");
    }

    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("transactions", transactions);
    result.pushKV("coinbaseaux", aux);
    result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0]->vout[0].nValue);
    result.pushKV("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast() + 1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");

    // Deprecated after may 2020 but leave it in in case miners are using it in their code.
    result.pushKV("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS_PER_MB);
    if (may2020Enabled)
    {
        result.pushKV("sigchecklimit", maxSigChecks.Value());
        result.pushKV("sigchecktotal", sigcheckTotal);
    }

    result.pushKV("sizelimit", (int64_t)maxGeneratedBlock);
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    // BU get the height directly from the block because pindexPrev could change if another block has come in.
    result.pushKV("height", (int64_t)(pblock->GetHeight()));

    return result;
}

/*
Inputs:
params
coinbaseSize -Set the size of coinbase if >=0

Outputs:
returns JSON if pblockOut is nullptr
pblockOut -A copy of the block if not nullptr
*/

bool forceTemplateRecalc GUARDED_BY(cs_main) = false;
// force block template recalculation
void SignalBlockTemplateChange()
{
    LOCK(cs_main);

    forceTemplateRecalc = true;
}
UniValue mkblocktemplate(const UniValue &params,
    int64_t coinbaseSize,
    CBlock *pblockOut,
    const CScript &coinbaseScriptIn)
{
    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    std::set<std::string> setClientRules;
    int64_t nMaxVersionPreVB = -1;
    CScript coinbaseScript(coinbaseScriptIn); // non-const copy (we may modify this below)

    if (params.size() > 0)
    {
        const UniValue &oparam = params[0].get_obj();
        const UniValue &modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal")
        {
            const UniValue &dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            {
                uint256 hash = block.GetHash();
                CBlockIndex *pindex = LookupBlockIndex(hash);
                READLOCK(cs_mapBlockIndex);
                if (pindex)
                {
                    if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                        return "duplicate";
                    if (pindex->nStatus & BLOCK_FAILED_MASK)
                        return "duplicate-invalid";
                    return "duplicate-inconclusive";
                }
            }

            CBlockIndex *const pindexPrev = chainActive.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, Params(), block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }

        const UniValue &aClientRules = find_value(oparam, "rules");
        if (aClientRules.isArray())
        {
            for (unsigned int i = 0; i < aClientRules.size(); ++i)
            {
                const UniValue &v = aClientRules[i];
                setClientRules.insert(v.get_str());
            }
        }
        else
        {
            // NOTE: It is important that this NOT be read if versionbits is supported
            const UniValue &uvMaxVersion = find_value(oparam, "maxversion");
            if (uvMaxVersion.isNum())
            {
                nMaxVersionPreVB = uvMaxVersion.get_int64();
            }
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if (!unsafeGetBlockTemplate.Value())
    {
        if (vNodes.empty())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Bitcoin is not connected!");

        if (IsInitialBlockDownload())
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Bitcoin is downloading blocks...");
    }

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        boost::system_time checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        }
        else
        {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = boost::get_system_time() + boost::posix_time::minutes(1);

            boost::unique_lock<boost::mutex> lock(csBestBlock);
            while (chainActive.Tip()->GetBlockHash() == hashWatchedChain && IsRPCRunning())
            {
                if (!cvBlockChange.timed_wait(lock, checktxtime))
                {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += boost::posix_time::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop
        // miners?
    }

    const Consensus::Params &consensusParams = Params().GetConsensus();

    // Update block
    static CBlockIndex *pindexPrev = nullptr;
    static int64_t nStart = 0;
    static std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    static CScript prevCoinbaseScript;
    static int64_t prevCoinbaseSize = -1;
    // We cache the previous block templates returned, but we invalidate the
    // cache below (generate a new block) if any of:
    // 1. Global forceTemplateRecalc is true.
    // 2. Cached block points to a different chaintip.
    // 3. Is testnet and 30 seconds have elapsed (so we pick up the testnet
    //    minimum difficulty -> 1.0 after 20 mins).
    // 4. Mempool has changed and 5 seconds has elapsed.
    // 5. Passed-in coinbaseSize differs from cached.
    // 6. Passed-in coinbaseScript differs from cached.
    if (pindexPrev != chainActive.Tip() || forceTemplateRecalc || // 1 & 2 above
        (consensusParams.fPowAllowMinDifficultyBlocks && std::abs(GetTime() - nStart) > 30) || // 3 above
        // 4 above
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && std::abs(GetTime() - nStart) > 5) ||
        prevCoinbaseSize != coinbaseSize || prevCoinbaseScript != coinbaseScript) // 5 & 6 above
    {
        forceTemplateRecalc = false;
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Saved passed-in values for coinbase (also used to determine if we need to create new block)
        prevCoinbaseScript = coinbaseScript;
        prevCoinbaseSize = coinbaseSize;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex *pindexPrevNew = chainActive.Tip();
        nStart = GetTime();

        // If client code didn't specify a coinbase address for the mining reward, grab one from the wallet.
        if (coinbaseScript.empty())
        {
            // Note that we don't cache the exact script from this to the prevCoinbaseScript -- it's sufficient
            // to cache the fact that client code didn't specify a coinbase address (by caching the empty script).
            boost::shared_ptr<CReserveScript> tmpScriptPtr;
            GetMainSignals().ScriptForMining(tmpScriptPtr);

            // throw an error if shared_ptr is not valid -- this means no wallet support was compiled-in
            if (!tmpScriptPtr)
                throw JSONRPCError(RPC_INTERNAL_ERROR,
                    "Wallet support is not compiled-in, please specify an address for the coinbase tx");

            // If the keypool is exhausted, the shared_ptr is valid but no actual script is generated; catch this.
            if (tmpScriptPtr->reserveScript.empty())
                throw JSONRPCError(
                    RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

            // Everything checks out, proceed with the wallet-generated address. Note that we don't tell the wallet to
            // "KeepKey" this address -- which means future calls will return the same address from the wallet for
            // future mining candidates, which is fine and good (since these are, after all, mining *candidates*).
            // This also means that the bitcoin-miner program will continue to mine to the same key for all blocks,
            // which is fine. If client code wants something more sophisticated, it can always specify coinbaseScript.
            coinbaseScript = tmpScriptPtr->reserveScript;
        }

        // Create new block
        pblocktemplate = BlockAssembler(Params()).CreateNewBlock(coinbaseScript, coinbaseSize);
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }
    else
    {
        LOG(RPC, "skipped block template construction tx: %d, last: %d  now:%d start:%d",
            mempool.GetTransactionsUpdated(), nTransactionsUpdatedLast, GetTime(), nStart);
    }
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience

    // Update nTime
    UpdateTime(pblock, consensusParams, pindexPrev);
    pblock->nNonce = 0;

    if (pblockOut != nullptr)
    {
        // Make a block.
        pblock->nVersion = UtilMkBlockTmplVersionBits(pblock->nVersion, setClientRules, pindexPrev, nullptr, nullptr);
        *pblockOut = *pblock;
        return UniValue();
    }
    else
    {
        // Or create JSON:
        return MkFullMiningCandidateJson(
            setClientRules, pindexPrev, coinbaseSize, pblocktemplate, nMaxVersionPreVB, nTransactionsUpdatedLast);
    }
}

UniValue getblocktemplate(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getblocktemplate ( \"jsonrequestobject\" )\n"
            "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default "
            "'template' request or a 'proposal'.\n"
            "It returns data needed to construct a block to work on.\n"
            "For full specification, see BIPs 22 and 9:\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki#getblocktemplate_changes\n"

            "\nArguments:\n"
            "1. \"jsonrequestobject\"       (string, optional) A json object in the following spec\n"
            "     {\n"
            "       \"mode\":\"template\"    (string, optional) This must be set to \"template\" or omitted\n"
            "       \"capabilities\":[       (array, optional) A list of strings\n"
            "           \"support\"           (string) client side supported feature, 'longpoll', 'coinbasetxn', "
            "'coinbasevalue', 'proposal', 'serverlist', 'workid'\n"
            "           ,...\n"
            "         ]\n"
            "     }\n"
            "\n"

            "\nResult:\n"
            "{\n"
            "  \"version\" : n,                    (numeric) The block version\n"
            "  \"rules\" : [ \"rulename\", ... ],    (array of strings) specific block rules that are to be enforced\n"
            "  \"vbavailable\" : {                 (json object) set of pending, supported versionbit (BIP 9) softfork "
            "deployments\n"
            "      \"rulename\" : bitnumber        (numeric) identifies the bit number as indicating acceptance and "
            "readiness for the named softfork rule\n"
            "      ,...\n"
            "  },\n"
            "  \"vbrequired\" : n,                 (numeric) bit mask of versionbits the server requires set in "
            "submissions\n"
            "  \"previousblockhash\" : \"xxxx\",    (string) The hash of current highest block\n"
            "  \"transactions\" : [                (array) contents of non-coinbase transactions that should be "
            "included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",          (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"hash\" : \"xxxx\",          (string) hash/id encoded in little-endian hexadecimal\n"
            "         \"depends\" : [              (array) array of numbers \n"
            "             n                        (numeric) transactions before this one (by 1-based index in "
            "'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                   (numeric) difference in value between transaction inputs and "
            "outputs (in Satoshis); for coinbase transactions, this is a negative Number of the total collected block "
            "fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT "
            "assume there isn't one\n"
            "         \"sigops\" : n,               (numeric) total number of SigOps, as counted for purposes of block "
            "limits; if key is not present, sigop count is unknown and clients MUST NOT assume there aren't any\n"
            "         \"required\" : true|false     (boolean) if provided and true, this transaction must be in the "
            "final block\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"coinbaseaux\" : {                  (json object) data that should be included in the coinbase's "
            "scriptSig content\n"
            "      \"flags\" : \"flags\"            (string) \n"
            "  },\n"
            "  \"coinbasevalue\" : n,               (numeric) maximum allowable input to coinbase transaction, "
            "including the generation award and transaction fees (in Satoshis)\n"
            "  \"coinbasetxn\" : { ... },           (json object) information for coinbase transaction\n"
            "  \"target\" : \"xxxx\",               (string) The hash target\n"
            "  \"mintime\" : xxx,                   (numeric) The minimum timestamp appropriate for next block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mutable\" : [                      (array of string) list of ways the block template may be changed \n"
            "     \"value\"                         (string) A way the block template may be changed, e.g. 'time', "
            "'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",   (string) A range of valid nonces\n"
            "  \"sigoplimit\" : n,                 (numeric) limit of sigops in blocks\n"
            "  \"sizelimit\" : n,                  (numeric) limit of block size\n"
            "  \"curtime\" : ttt,                  (numeric) current timestamp in seconds since epoch (Jan 1 1970 "
            "GMT)\n"
            "  \"bits\" : \"xxx\",                 (string) compressed target of next block\n"
            "  \"height\" : n                      (numeric) The height of the next block\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getblocktemplate", "") + HelpExampleRpc("getblocktemplate", ""));

    return mkblocktemplate(params);
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state(){};

protected:
    virtual void BlockChecked(const CBlock &block, const CValidationState &stateIn)
    {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

UniValue SubmitBlock(CBlock &block)
{
    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        CBlockIndex *pindex = LookupBlockIndex(hash);
        READLOCK(cs_mapBlockIndex);
        if (pindex)
        {
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            // Otherwise, we might only have the header - process the block before returning
            fBlockPresent = true;
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    LOG(RPC, "Received block %s via RPC.\n", block.GetHash().ToString());
    RegisterValidationInterface(&sc);

    // In we are mining our own block or not running in parallel for any reason
    // we must terminate any block validation threads that are currently running,
    // Unless they have more work than our own block or are processing a chain
    // that has more work than our block.
    PV->StopAllValidationThreads(block.GetBlockHeader().nBits);

    bool fAccepted = ProcessNewBlock(state, Params(), nullptr, &block, true, nullptr, false);
    UnregisterValidationInterface(&sc);
    if (fBlockPresent)
    {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (fAccepted)
    {
        if (!sc.found)
            return "inconclusive";
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

UniValue submitblock(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error("submitblock \"hexdata\" ( \"jsonparametersobject\" )\n"
                            "\nAttempts to submit new block to network.\n"
                            "The 'jsonparametersobject' parameter is currently ignored.\n"
                            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

                            "\nArguments\n"
                            "1. \"hexdata\"    (string, required) the hex-encoded block data to submit\n"
                            "2. \"jsonparametersobject\"     (string, optional) object of optional parameters\n"
                            "    {\n"
                            "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST "
                            "be included with submissions\n"
                            "    }\n"
                            "\nResult:\n"
                            "\nExamples:\n" +
                            HelpExampleCli("submitblock", "\"mydata\"") + HelpExampleRpc("submitblock", "\"mydata\""));

    CBlock block;
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    return SubmitBlock(block);
}

UniValue estimatefee(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("estimatefee nblocks\n"
                            "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
                            "confirmation within nblocks blocks.\n"
                            "\nArguments:\n"
                            "1. nblocks     (numeric)\n"
                            "\nResult:\n"
                            "n              (numeric) estimated fee-per-kilobyte\n"
                            "\n"
                            "A negative value is returned if not enough transactions and blocks\n"
                            "have been observed to make an estimate.\n"
                            "\nExample:\n" +
                            HelpExampleCli("estimatefee", "6"));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    if (feeRate == CFeeRate(0))
        return -1.0;

    return ValueFromAmount(feeRate.GetFeePerK());
}

UniValue estimatesmartfee(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("estimatesmartfee nblocks\n"
                            "\nWARNING: This interface is unstable and may disappear or change!\n"
                            "\nThis rpc call now does the same thing as estimatefee, It has not been removed for\n"
                            "compatibility reasons\n"
                            "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
                            "confirmation within nblocks blocks.\n"
                            "\nArguments:\n"
                            "1. nblocks     (numeric)\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"feerate\" : x.x,     (numeric) estimate fee-per-kilobyte (in BCH)\n"
                            "  \"blocks\" : 1         (numeric) hardcoded to 1 for backwards compatibility reasons\n"
                            "}\n"
                            "\n"
                            "A negative value is returned if not enough transactions and blocks\n"
                            "have been observed to make an estimate.\n"
                            "\nExample:\n" +
                            HelpExampleCli("estimatesmartfee", "6"));

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    UniValue result(UniValue::VOBJ);
    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    result.pushKV("feerate", feeRate == CFeeRate(0) ? -1.0 : ValueFromAmount(feeRate.GetFeePerK()));
    result.pushKV("blocks", 1);
    return result;
}


static const CRPCCommand commands[] = {
    //  category              name                      actor (function)         okSafeMode
    //  --------------------- ------------------------  -----------------------  ----------
    {"mining", "getnetworkhashps", &getnetworkhashps, true}, {"mining", "getmininginfo", &getmininginfo, true},
    {"mining", "prioritisetransaction", &prioritisetransaction, true},
    {"mining", "getblocktemplate", &getblocktemplate, true}, {"mining", "submitblock", &submitblock, true},

    {"generating", "generate", &generate, true}, {"generating", "generatetoaddress", &generatetoaddress, true},

    {"util", "estimatefee", &estimatefee, true}, {"util", "estimatesmartfee", &estimatesmartfee, true},
};

void RegisterMiningRPCCommands(CRPCTable &table)
{
    for (auto cmd : commands)
        table.appendCommand(cmd);
}
