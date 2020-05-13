// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Bitcoin Test Suite

#include "test_bitcoin.h"

#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "crypto/sha256.h"
#include "fs.h"
#include "key.h"
#include "main.h"
#include "miner.h"
#include "parallel.h"
#include "pubkey.h"
#include "random.h"
#include "rpc/register.h"
#include "rpc/server.h"
#include "script/sigcache.h"
#include "test/testutil.h"
#include "txadmission.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "validation/validation.h"

#include <memory>

#include <boost/program_options.hpp>
#include <boost/test/unit_test.hpp>
#include <thread>

uint256 insecure_rand_seed = GetRandHash();
FastRandomContext insecure_rand_ctx(insecure_rand_seed);

extern bool fPrintToConsole;
extern void noui_connect();

BasicTestingSetup::BasicTestingSetup(const std::string &chainName)
{
    // Do not place the data created by these unit tests on top of any existing chain,
    // by overriding datadir to use a temporary if it isn't already overridden
    if (mapArgs.count("-datadir") == 0)
        mapArgs["-datadir"] = GetTempPath().string();
    SHA256AutoDetect();
    RandomInit();
    ECC_Start();
    SetupEnvironment();
    SetupNetworking();
    InitSignatureCache();
    fPrintToDebugLog = false; // don't want to write to debug.log file
    fCheckBlockIndex = true;
    SelectParams(chainName);
    noui_connect();
}

BasicTestingSetup::~BasicTestingSetup() { ECC_Stop(); }
TestingSetup::TestingSetup(const std::string &chainName) : BasicTestingSetup(chainName)
{
    const CChainParams &chainparams = Params();
    // Ideally we'd move all the RPC tests to the functional testing framework
    // instead of unit tests, but for now we need these here.
    RegisterAllCoreRPCCommands(tableRPC);
    ClearDatadirCache();
    pathTemp =
        GetTempPath() / strprintf("test_bitcoin_%lu_%i", (unsigned long)GetTime(), (int)(InsecureRandRange(1 << 30)));
    fs::create_directories(pathTemp);
    pblocktree = new CBlockTreeDB(1 << 20, "", true);
    pcoinsdbview = new CCoinsViewDB(1 << 23, true);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    txCommitQ = new std::map<uint256, CTxCommitData>();
    bool worked = InitBlockIndex(chainparams);
    assert(worked);

    // -limitfreerelay is diabled by default but some tests rely on it so make sure to set it here.
    SetArg("-limitfreerelay", std::to_string(15));

    // Initial dbcache settings so that the automatic cache setting don't kick in and allow
    // us to accidentally use up our RAM on Travis, and also so that we are not prevented from flushing the
    // dbcache if the need arises in the unit tests (dbcache must be less than the DEFAULT_HIGH_PERF_MEM_CUTOFF
    // to allow all cache entries to be flushed).
    SoftSetArg("-dbcache", std::to_string(5));
    nCoinCacheMaxSize.store(5000000);

    // Make sure there are 3 script check threads running for each queue
    SoftSetArg("-par", std::to_string(3));
    PV.reset(new CParallelValidation());

    RegisterNodeSignals(GetNodeSignals());
}

TestingSetup::~TestingSetup()
{
    UnregisterNodeSignals(GetNodeSignals());
    threadGroup.interrupt_all();
    threadGroup.join_all();
    UnloadBlockIndex();
    delete pcoinsTip;
    delete pcoinsdbview;
    delete pblocktree;
    fs::remove_all(pathTemp);
}

struct NumericallyLessTxHashComparator
{
public:
    bool operator()(const CTransactionRef &a, const CTransactionRef &b) const { return a->GetHash() < b->GetHash(); }
};

TestChain100Setup::TestChain100Setup() : TestingSetup(CBaseChainParams::REGTEST)
{
    // Generate a 100-block chain:
    coinbaseKey.MakeNewKey(true);
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    for (int i = 0; i < COINBASE_MATURITY; i++)
    {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        coinbaseTxns.push_back(*b.vtx[0]);
    }
}

//
// Create a new block with just given transactions, coinbase paying to
// scriptPubKey, and try to add it to the current chain.
//
CBlock TestChain100Setup::CreateAndProcessBlock(const std::vector<CMutableTransaction> &txns,
    const CScript &scriptPubKey)
{
    const CChainParams &chainparams = Params();
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey);
    CBlock &block = pblocktemplate->block;

    // Replace mempool-selected txns with just coinbase plus passed-in txns:
    block.vtx.resize(1);
    for (const CMutableTransaction &tx : txns)
        block.vtx.push_back(MakeTransactionRef(tx));

    // enfore LTOR ordering of transactions
    std::sort(block.vtx.begin() + 1, block.vtx.end(), NumericallyLessTxHashComparator());

    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    unsigned int extraNonce = 0;
    IncrementExtraNonce(&block, extraNonce);

    while (!CheckProofOfWork(block.GetHash(), block.nBits, chainparams.GetConsensus()))
        ++block.nNonce;

    CValidationState state;
    ProcessNewBlock(state, chainparams, nullptr, &block, true, nullptr, false);

    CBlock result = block;
    return result;
}

TestChain100Setup::~TestChain100Setup() {}
CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CMutableTransaction &tx, CTxMemPool *pool)
{
    CTransaction txn(tx);
    return FromTx(txn, pool);
}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CTransaction &txn, CTxMemPool *pool)
{
    bool hasNoDependencies = pool ? pool->HasNoInputsOf(MakeTransactionRef(txn)) : hadNoDependencies;
    // Hack to assume either its completely dependent on other mempool txs or not at all
    CAmount inChainValue = hasNoDependencies ? txn.GetValueOut() : 0;

    CTxMemPoolEntry ret(MakeTransactionRef(txn), nFee, nTime, dPriority, nHeight, hasNoDependencies, inChainValue,
        spendsCoinbase, sigOpCount, lp);
    ret.sighashType = SIGHASH_ALL; // For testing, give the transaction any valid sighashtype
    return ret;
}

void Shutdown(void *parg) { exit(0); }
void StartShutdown() { exit(0); }
bool ShutdownRequested() { return false; }
using namespace boost::program_options;

CService ipaddress(uint32_t i, uint32_t port)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), port);
}

struct StartupShutdown
{
    StartupShutdown()
    {
        options_description optDef("Options");
        optDef.add_options()("testhelp", "program options information")(
            "log_level", "set boost logging (all, test_suite, message, warning, error, ...)")(
            "log_bitcoin", value<std::string>()->required(), "bitcoin logging destination (console, none)");
        variables_map opts;
        store(parse_command_line(boost::unit_test::framework::master_test_suite().argc,
                  boost::unit_test::framework::master_test_suite().argv, optDef),
            opts);

        if (opts.count("testhelp"))
        {
            std::cout << optDef << std::endl;
            exit(0);
        }

        if (opts.count("log_bitcoin"))
        {
            std::string s = opts["log_bitcoin"].as<std::string>();
            if (s == "console")
            {
                /* To enable this, add
                   -- --log_bitcoin console
                   to the end of the test_bitcoin argument list. */
                Logging::LogToggleCategory(ALL, true);
                fPrintToConsole = true;
                fPrintToDebugLog = false;
            }
            else if (s == "none")
            {
                fPrintToConsole = false;
                fPrintToDebugLog = false;
            }
        }
    }
    ~StartupShutdown() { UnlimitedCleanup(); }
};

#if BOOST_VERSION >= 106500
BOOST_TEST_GLOBAL_FIXTURE(StartupShutdown);
#else
BOOST_GLOBAL_FIXTURE(StartupShutdown);
#endif

std::ostream &operator<<(std::ostream &os, const uint256 &num)
{
    os << num.ToString();
    return os;
}
