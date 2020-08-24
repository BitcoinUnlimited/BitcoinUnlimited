// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "unlimited.h"
#include "versionbits.h" // bip135 added

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"

// Next protocol upgrade will be activated once MTP >= Nov 15 12:00:00 UTC 2020
const uint64_t NOV2020_ACTIVATION_TIME = 1605441600;
uint64_t nMiningForkTime = NOV2020_ACTIVATION_TIME;

CBlock CreateGenesisBlock(CScript prefix,
    const std::string &comment,
    const CScript &genesisOutputScript,
    uint32_t nTime,
    uint32_t nNonce,
    uint32_t nBits,
    int32_t nVersion,
    const CAmount &genesisReward)
{
    const unsigned char *pComment = (const unsigned char *)comment.c_str();
    std::vector<unsigned char> vComment(pComment, pComment + comment.length());

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = prefix << vComment;
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505,
 * nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase
 * 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime,
    uint32_t nNonce,
    uint32_t nBits,
    int32_t nVersion,
    const CAmount &genesisReward)
{
    const char *pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
    const CScript genesisOutputScript = CScript()
                                        << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb6"
                                                    "49f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f")
                                        << OP_CHECKSIG;
    return CreateGenesisBlock(CScript() << 486604799 << LegacyCScriptNum(4), pszTimestamp, genesisOutputScript, nTime,
        nNonce, nBits, nVersion, genesisReward);
}

bool CChainParams::RequireStandard() const
{
    // the acceptnonstdtxn flag can only be used to narrow the behavior.
    // A blockchain whose default is to allow nonstandard txns can be configured to disallow them.
    return fRequireStandard || !GetBoolArg("-acceptnonstdtxn", true);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210000;
        // 00000000000000ce80a7e057163a4db1d5ad7b20fb6f598c9597b9665c8fb0d4 - April 1, 2012
        consensus.BIP16Height = 173805;
        consensus.BIP34Height = 227931;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = 388381; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 363725; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
        consensus.BIP68Height = 419328; // BIP68, 112, 113 has activated
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        // The half life for the ASERT DAA. For every (nASERTHalfLife) seconds behind schedule the blockchain gets,
        // difficulty is cut in half. Doubled if blocks are ahead of schedule.
        // Two days
        consensus.nASERTHalfLife = 2 * 24 * 60 * 60;
        // testing bit
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601LL; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999LL; // December 31, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].windowsize = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1916; // 95% of 2016

        // Aug, 1 2017 hard fork
        consensus.uahfHeight = 478559;
        // Nov, 13 2017 hard fork
        consensus.daaHeight = 504031;
        // May, 15 2018 hard fork
        consensus.may2018Height = 530359;
        // Nov, 15 2018 hard fork
        consensus.nov2018Height = 556766;
        // Noc, 15 2019 hard fork
        consensus.nov2019Height = 609135;
        // May, 15 2020 hard fork
        consensus.may2020Height = 635258;
        // Nov 15, 2020 12:00:00 UTC protocol upgrade¶
        consensus.nov2020ActivationTime = NOV2020_ACTIVATION_TIME;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xd9;
        pchCashMessageStart[0] = 0xe3;
        pchCashMessageStart[1] = 0xe1;
        pchCashMessageStart[2] = 0xf3;
        pchCashMessageStart[3] = 0xe8;
        nDefaultPort = DEFAULT_MAINNET_PORT;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1231006505, 2083236893, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock ==
               uint256S("0x000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"));
        assert(
            genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        // List of Bitcoin Cash compatible seeders
        vSeeds.push_back(CDNSSeedData("bitcoinunlimited.info", "btccash-seeder.bitcoinunlimited.info", true));
        vSeeds.push_back(CDNSSeedData("bitcoinabc.org", "seed.bitcoinabc.org", true));
        vSeeds.push_back(CDNSSeedData("bitcoinforks.org", "seed-bch.bitcoinforks.org", true));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 5);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 128);
        base58Prefixes[EXT_PUBLIC_KEY] =
            boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] =
            boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();
        cashaddrPrefix = "bitcoincash";

        // BITCOINUNLIMITED START
        vFixedSeeds = std::vector<SeedSpec6>();
        // BITCOINUNLIMITED END

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        // clang-format off
        checkpointData = CCheckpointData();
        MapCheckpoints &checkpoints = checkpointData.mapCheckpoints;
        checkpoints[ 11111] = uint256S("0x0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d");
        checkpoints[ 33333] = uint256S("0x000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6");
        checkpoints[ 74000] = uint256S("0x0000000000573993a3c9e41ce34471c079dcf5f52a0e824a81e7f953b8661a20");
        checkpoints[105000] = uint256S("0x00000000000291ce28027faea320c8d2b054b2e0fe44a773f3eefb151d6bdc97");
        checkpoints[134444] = uint256S("0x00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe");
        checkpoints[168000] = uint256S("0x000000000000099e61ea72015e79632f216fe6cb33d7899acb35b75c8303b763");
        checkpoints[193000] = uint256S("0x000000000000059f452a5f7340de6682a977387c17010ff6e6c3bd83ca8b1317");
        checkpoints[210000] = uint256S("0x000000000000048b95347e83192f69cf0366076336c639f9b7228e9ba171342e");
        checkpoints[216116] = uint256S("0x00000000000001b4f4b433e81ee46494af945cf96014816a4e2370f11b23df4e");
        checkpoints[225430] = uint256S("0x00000000000001c108384350f74090433e7fcf79a606b8e797f065b130575932");
        checkpoints[250000] = uint256S("0x000000000000003887df1f29024b06fc2200b55f8af8f35453d7be294df2d214");
        checkpoints[279000] = uint256S("0x0000000000000001ae8c72a0b0c301f67e3afca10e819efa9041e458e9bd7e40");
        checkpoints[295000] = uint256S("0x00000000000000004d9b4ef50f0f9d686fd69db2e03af35a100370c64632a983");
        // August 1st 2017 CASH fork (UAHF)
        checkpoints[478559] = uint256S("0x000000000000000000651ef99cb9fcbe0dadde1d424bd9f15ff20136191a5eec");
        // November 13th 2017 new DAA fork
        checkpoints[504031] = uint256S("0x0000000000000000011ebf65b60d0a3de80b8175be709d653b4c1a1beeb6ab9c");
        // May 15th 2018 re-enable op_codes and 32 MB max block size
        checkpoints[530359] = uint256S("0x0000000000000000011ada8bd08f46074f44a8f155396f43e38acf9501c49103");
        // Nov 15th 2018 activate LTOR, DSV op_code
        checkpoints[556767] = uint256S("0x0000000000000000004626ff6e3b936941d341c5932ece4357eeccac44e6d56c");
        // May 15th 2019 activate Schnorr, segwit recovery
        checkpoints[582680] = uint256S("0x000000000000000001b4b8e36aec7d4f9671a47872cb9a74dc16ca398c7dcc18");
        // Nov 15th 2019 activate Schnorr Multisig, minimal data
        checkpoints[609136] = uint256S("0x000000000000000000b48bb207faac5ac655c313e41ac909322eaa694f5bc5b1");
        // May 15th 2020 activate op_reverse, SigChecks
        checkpoints[635259] = uint256S("0x00000000000000000033dfef1fc2d6a5d5520b078c55193a9bf498c5b27530f7");

        // clang-format on
        // * UNIX timestamp of last checkpoint block
        checkpointData.nTimeLastCheckpoint = 1573825449;
        // * total number of transactions between genesis and last checkpoint
        checkpointData.nTransactionsLastCheckpoint = 281198294;
        // * estimated number of transactions per day after checkpoint (~3.5 TPS)
        checkpointData.fTransactionsPerDay = 280000.0;
    }
};

static CMainParams mainParams;

class CUnlParams : public CChainParams
{
public:
    CUnlParams()
    {
        strNetworkID = "nol";

        std::vector<unsigned char> rawScript(ParseHex("76a914a123a6fdc265e1bbcf1123458891bd7af1a1b5d988ac"));
        CScript outputScript(rawScript.begin(), rawScript.end());

        genesis = CreateGenesisBlock(CScript() << 0, "Big blocks FTW (for the world)", outputScript, 1496544271,
            2301659837, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Height = 0;
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = consensus.hashGenesisBlock;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.BIP68Height = 0;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60 / 10; // two weeks
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;

        assert(
            consensus.hashGenesisBlock == uint256S("0000000057e31bd2066c939a63b7b8623bd0f10d8c001304bdfc1a7902ae6d35"));

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xfb;
        pchMessageStart[1] = 0xce;
        pchMessageStart[2] = 0xc4;
        pchMessageStart[3] = 0xe9;
        nDefaultPort = DEFAULT_NOLNET_PORT;
        nPruneAfterHeight = 100000;

        // Aug, 1 2017 hard fork
        consensus.uahfHeight = 0;
        // Nov, 13 hard fork
        consensus.daaHeight = consensus.DifficultyAdjustmentInterval();
        // May, 15 2018 hard fork
        consensus.may2018Height = 0;
        // Nov, 15 2018 hard fork
        consensus.nov2018Height = 0;
        // May, 15 2019 hard fork
        consensus.may2019Height = 0;
        // May 15, 2020 12:00:00 UTC protocol upgrade¶
        consensus.nov2019Height = 0;
        // May, 15 2020 hard fork
        consensus.may2020Height = 0;
        // Nov, 15 2019 12:00:00 UTC fork activation time
        consensus.nov2020ActivationTime = NOV2020_ACTIVATION_TIME;

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("bitcoinunlimited.info", "nolnet-seed.bitcoinunlimited.info", true));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 25); // P2PKH addresses begin with B
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 68); // P2SH  addresses begin with U
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 35); // WIF   format begins with 2B or 2C
        base58Prefixes[EXT_PUBLIC_KEY] =
            boost::assign::list_of(0x42)(0x69)(0x67)(0x20).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] =
            boost::assign::list_of(0x42)(0x6c)(0x6b)(0x73).convert_to_container<std::vector<unsigned char> >();
        cashaddrPrefix = "bchnol";

        vFixedSeeds = std::vector<SeedSpec6>();

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of(0, uint256S("0000000057e31bd2066c939a63b7b8623bd0f10d8c001304bdfc1a7902ae6d35")),
            0, 0, 0};
    }
};
CUnlParams unlParams;


/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams
{
public:
    CTestNetParams()
    {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Height = 514; // 00000000040b4e986385315e14bee30ad876d8b47f748025b26683116d21aa65
        consensus.BIP34Height = 21111;
        consensus.BIP34Hash = uint256S("0x0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8");
        consensus.BIP65Height = 581885; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 330776; // 000000002104c8c45e99a8853285a3b592602a3ccde2b832481da85e9e4ba182
        consensus.BIP68Height = 770112; // BIP68, 112, 113 has activated
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        // The half life for the ASERT DAA. For every (nASERTHalfLife) seconds behind schedule the blockchain gets,
        // difficulty is cut in half. Doubled if blocks are ahead of schedule.
        // One hour
        consensus.nASERTHalfLife = 60 * 60;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].windowsize = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1512; // 75% of 2016

        // Aug, 1 2017 hard fork
        consensus.uahfHeight = 1155876;
        // Nov, 13 hard fork
        consensus.daaHeight = 1188697;
        // May, 15 2018 hard fork
        consensus.may2018Height = 1233070;
        // Nov 15, 2018 hard fork
        consensus.nov2018Height = 1267996;
        // May, 15 2019 hard fork
        consensus.may2019Height = 1303884;
        // Nov, 15 2019 har fork
        consensus.nov2019Height = 1341711;
        // May, 15 2020 hard fork
        consensus.may2020Height = 1378461;
        // Nov 15, 2020 12:00:00 UTC protocol upgrade¶
        consensus.nov2020ActivationTime = NOV2020_ACTIVATION_TIME;


        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x09;
        pchMessageStart[3] = 0x07;
        pchCashMessageStart[0] = 0xf4;
        pchCashMessageStart[1] = 0xe5;
        pchCashMessageStart[2] = 0xf3;
        pchCashMessageStart[3] = 0xf4;
        nDefaultPort = DEFAULT_TESTNET_PORT;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1296688602, 414098458, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock ==
               uint256S("0x000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"));
        assert(
            genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top

        // Bitcoin ABC seeder
        vSeeds.push_back(CDNSSeedData("bitcoinabc.org", "testnet-seed.bitcoinabc.org", true));
        // bitcoinforks seeders
        vSeeds.push_back(CDNSSeedData("bitcoinforks.org", "testnet-seed-bch.bitcoinforks.org", true));
        // BU seeder
        vSeeds.push_back(CDNSSeedData("bitcoinunlimited.info", "testnet-seed.bitcoinunlimited.info", true));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        cashaddrPrefix = "bchtest";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        // clang-format off
        checkpointData = CCheckpointData();
        MapCheckpoints &checkpoints = checkpointData.mapCheckpoints;
        checkpoints[546]     = uint256S("0x000000002a936ca763904c3c35fce2f3556c559c0214345d31b1bcebf76acb70");
        // August 1st 2017 CASH fork (UAHF) activation block
        checkpoints[1155876] = uint256S("0x00000000000e38fef93ed9582a7df43815d5c2ba9fd37ef70c9a0ea4a285b8f5");
        // Nov, 13th 2017. DAA activation block.
        checkpoints[1188697] = uint256S("0x0000000000170ed0918077bde7b4d36cc4c91be69fa09211f748240dabe047fb");
        // May 15th 2018, re-enabling opcodes, max block size 32MB
        checkpoints[1233070] = uint256S("0x0000000000000253c6201a2076663cfe4722e4c75f537552cc4ce989d15f7cd5");
        // Nov 15th 2018, CHECKDATASIG, ctor
        checkpoints[1267996] = uint256S("0x00000000000001fae0095cd4bea16f1ce8ab63f3f660a03c6d8171485f484b24");
        // May 15th 2019, Schnorr + segwit recovery activation block
        checkpoints[1303885] = uint256S("0x00000000000000479138892ef0e4fa478ccc938fb94df862ef5bde7e8dee23d3");
        // Nov 15th 2019 activate Schnorr Multisig, minimal data
        checkpoints[1341712] = uint256S("0x00000000fffc44ea2e202bd905a9fbbb9491ef9e9d5a9eed4039079229afa35b");
        // May 15th 2020 activate op_reverse, SigCheck
        checkpoints[1378461] = uint256S("0x0000000099f5509b5f36b1926bcf82b21d936ebeadee811030dfbbb7fae915d7");

        // clang-format on
        // Data as of block
        checkpointData.nTimeLastCheckpoint = 1573827462;
        // * total number of transactions between genesis and last checkpoint
        checkpointData.nTransactionsLastCheckpoint = 57494631;
        // * estimated number of transactions per day after checkpoint (~1.6 TPS)
        checkpointData.fTransactionsPerDay = 140000;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams
{
public:
    CRegTestParams()
    {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.BIP34Height = 1000; // BIP34 has activated on regtest (Used in rpc activation tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.BIP68Height = 576; // BIP68, 112, 113 has activated
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        // The half life for the ASERT DAA. For every (nASERTHalfLife) seconds behind schedule the blockchain gets,
        // difficulty is cut in half. Doubled if blocks are ahead of schedule.
        // Two days
        consensus.nASERTHalfLife = 2 * 24 * 60 * 60;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999LL;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].windowsize = 144;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 108; // 75% of 144

        // Hard fork is always enabled on regtest.
        consensus.uahfHeight = 0;
        // Nov, 13 hard fork is always on on regtest.
        consensus.daaHeight = 0;
        // May, 15 2018 hard fork is always active on regtest
        consensus.may2018Height = 0;
        // Nov, 15 2018 hard fork is always active on regtest
        consensus.nov2018Height = 0;
        // May, 15 2019 hard fork
        consensus.may2019Height = 0;
        // Nov, 15 2019 hard fork is always active on regtest
        consensus.nov2019Height = 0;
        // May, 15 2020 hard fork
        consensus.may2020Height = 0;
        // Nov 15, 2020 12:00:00 UTC protocol upgrade¶
        consensus.nov2020ActivationTime = NOV2020_ACTIVATION_TIME;

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        pchCashMessageStart[0] = 0xda;
        pchCashMessageStart[1] = 0xb5;
        pchCashMessageStart[2] = 0xbf;
        pchCashMessageStart[3] = 0xfa;
        nDefaultPort = DEFAULT_REGTESTNET_PORT;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock ==
               uint256S("0x0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"));
        assert(
            genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear(); //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of(0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")),
            0, 0, 0};
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        cashaddrPrefix = "bchreg";
    }
};
static CRegTestParams regTestParams;

CChainParams *pCurrentParams = 0;

const CChainParams &Params()
{
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(const std::string &chain)
{
    if (chain == CBaseChainParams::MAIN)
        return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
        return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
        return regTestParams;
    else if (chain == CBaseChainParams::UNL)
        return unlParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string &network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

// bip135 begin
/**
 * Return true if a deployment is considered to be configured for the network.
 * Deployments with a zero-length name, or a windowsize or threshold equal to
 * zero are not considered to be configured, and will be reported as 'unknown'
 * if signals are detected for them.
 * Unconfigured deployments can be ignored to save processing time, e.g.
 * in ComputeBlockVersion() when computing the default block version to emit.
 */
bool IsConfiguredDeployment(const Consensus::Params &consensusParams, const int bit)
{
    DbgAssert(bit >= 0 && bit <= (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS, return false);

    const Consensus::ForkDeployment *vdeployments = consensusParams.vDeployments;
    const struct ForkDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[bit];

    if (strlen(vbinfo.name) == 0)
        return false;

    return (vdeployments[bit].windowsize != 0 && vdeployments[bit].threshold != 0);
}

/**
 * Return a string representing CSV-formatted deployments for the network.
 * Only configured deployments satisfying IsConfiguredDeployment() are included.
 */
const std::string NetworkDeploymentInfoCSV(const std::string &network)
{
    const Consensus::Params &consensusParams = Params(network).GetConsensus();
    const Consensus::ForkDeployment *vdeployments = consensusParams.vDeployments;

    std::string networkInfoStr;
    networkInfoStr = "# deployment info for network '" + network + "':\n";

    for (int bit = 0; bit < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; bit++)
    {
        const struct ForkDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[bit];
        if (IsConfiguredDeployment(consensusParams, bit))
        {
            networkInfoStr += network + ",";
            networkInfoStr += std::to_string(bit) + ",";
            networkInfoStr += std::string(vbinfo.name) + ",";
            networkInfoStr += std::to_string(vdeployments[bit].nStartTime) + ",";
            networkInfoStr += std::to_string(vdeployments[bit].nTimeout) + ",";
            networkInfoStr += std::to_string(vdeployments[bit].windowsize) + ",";
            networkInfoStr += std::to_string(vdeployments[bit].threshold) + ",";
            networkInfoStr += std::to_string(vdeployments[bit].minlockedblocks) + ",";
            networkInfoStr += std::to_string(vdeployments[bit].minlockedtime) + ",";
            networkInfoStr += (vbinfo.gbt_force ? "true" : "false");
            networkInfoStr += "\n";
        }
    }
    return networkInfoStr;
}

/**
 * Return a modifiable reference to the chain params, to be updated by the
 * CSV deployment data reading routine.
 */
CChainParams &ModifiableParams()
{
    assert(pCurrentParams);
    return *pCurrentParams;
}
// bip135 end
