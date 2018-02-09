// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_bitcoin.h"
#include "utilstrencodings.h"
#include "tokengroups.h"
#include "miner.h"
#include "main.h"
#include "parallel.h"
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(tokengroup_tests, BasicTestingSetup)

// create a group pay to public key hash script
CScript gp2pkh(const CTokenGroupID& group, const CKeyID &dest)
{
    CScript script = CScript() << group.bytes() << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160
                               << ToByteVector(dest) << OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

// create a group pay to public key hash script
CScript p2pkh(const CKeyID &dest)
{
    CScript script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(dest) << OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

CScript p2sh(const CScriptID &dest)
{
    CScript script;

    script.clear();
    script << OP_HASH160 << ToByteVector(dest) << OP_EQUAL;
    return script;
}

// if no group is specified, this produces a p2sh otherwise gp2sh
CScript gp2sh(const CTokenGroupID &group, const CScriptID &dest)
{
    CScript script;

    script.clear();
    if (group.isUserGroup())
    {
        script << group.bytes() << OP_GROUP << OP_DROP << OP_HASH160 << ToByteVector(dest) << OP_EQUAL;
    }
    else
    {
        script << OP_HASH160 << ToByteVector(dest) << OP_EQUAL;
    }
    return script;
}

std::string HexStr(const CMutableTransaction &tx)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}




class QuickAddress
{
public:
    QuickAddress()
    {
        secret.MakeNewKey(true);
        pubkey = secret.GetPubKey();
        addr = pubkey.GetID();
        eAddr = pubkey.GetHash();
        grp = CTokenGroupID(addr);
    }
    QuickAddress(const CKey& k)
    {
        secret = k;
        pubkey = secret.GetPubKey();
        addr = pubkey.GetID();
        eAddr = pubkey.GetHash();
        grp = CTokenGroupID(addr);
    }
    QuickAddress(unsigned char key)  // make a very simple key for testing only
    {
        secret.MakeNewKey(true);
        unsigned char *c = (unsigned char*) secret.begin();
        *c = key;
        c++;
        for (int i=1;i<32;i++,c++)
        {
            *c = 0;
        }
        pubkey = secret.GetPubKey();
        addr = pubkey.GetID();
        eAddr = pubkey.GetHash();
        grp = CTokenGroupID(addr);
    }

    CKey secret;
    CPubKey pubkey;
    CKeyID addr;  // 160 bit normal address
    uint256 eAddr; // 256 bit extended address
    CTokenGroupID grp;
};

COutPoint AddUtxo(const CScript &script, uint64_t amount, CCoinsViewCache &coins)
{
    // This creates an unbalanced transaction but it doesn't matter because AddCoins doesn't validate the tx
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = script;
    tx.vout[0].nValue = amount;
    tx.vin[0].scriptSig = CScript();
    tx.nLockTime = 0;

    int height = 1; // doesn't matter for our purposes
    AddCoins(coins, tx, height);
    return COutPoint(tx.GetHash(), 0);
}

CTransaction tx1x1(const COutPoint &utxo, const CScript &txo, CAmount amt, const CScript& prevOutScript=CScript())
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = utxo;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = txo;
    tx.vout[0].nValue = amt;
    tx.vin[0].scriptSig = CScript(); // CheckTokenGroups does not validate sig so anything in here
    tx.nLockTime = 0;

    return tx;
}


CTransaction tx1x1(const COutPoint &utxo, const CScript &txo, CAmount amt, const CKey& key, const CScript& prevOutScript, bool p2pkh=true)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = utxo;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = txo;
    tx.vout[0].nValue = amt;
    tx.vin[0].scriptSig = CScript();
    tx.nLockTime = 0;

    unsigned int sighashType = SIGHASH_ALL | SIGHASH_FORKID;
    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(prevOutScript, tx, 0, sighashType, amt, 0);
    if (!key.Sign(hash, vchSig))
    {
        assert(0);
    }
    vchSig.push_back((unsigned char)sighashType);
    tx.vin[0].scriptSig << vchSig;
    if (p2pkh)
    {
        tx.vin[0].scriptSig << ToByteVector(key.GetPubKey());
    }

    return tx;
}

CTransaction tx1x1(const CTransaction &prevtx, int prevout, const CScript &txo, CAmount amt, const CKey& key, bool p2pkh=true)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(prevtx.GetHash(),prevout);
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = txo;
    tx.vout[0].nValue = amt;
    tx.vin[0].scriptSig = CScript();
    tx.nLockTime = 0;

    unsigned int sighashType = SIGHASH_ALL | SIGHASH_FORKID;
    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(prevtx.vout[prevout].scriptPubKey, tx, 0, sighashType, prevtx.vout[prevout].nValue, 0);
    if (!key.Sign(hash, vchSig))
    {
        assert(0);
    }
    vchSig.push_back((unsigned char)sighashType);
    tx.vin[0].scriptSig << vchSig;
    if (p2pkh)
    {
        tx.vin[0].scriptSig << ToByteVector(key.GetPubKey());
    }

    return tx;
}

CTransaction tx1x1_p2sh_of_p2pkh(const CTransaction &prevtx, int prevout, const CScript &txo, CAmount amt, const CKey& key, const CScript& redeemScript)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(prevtx.GetHash(),prevout);
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = txo;
    tx.vout[0].nValue = amt;
    tx.vin[0].scriptSig = CScript();
    tx.nLockTime = 0;

    unsigned int sighashType = SIGHASH_ALL | SIGHASH_FORKID;
    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(redeemScript, tx, 0, sighashType, prevtx.vout[prevout].nValue, 0);
    if (!key.Sign(hash, vchSig))
    {
        assert(0);
    }
    vchSig.push_back((unsigned char)sighashType);
    tx.vin[0].scriptSig << vchSig;
    tx.vin[0].scriptSig << ToByteVector(key.GetPubKey());
    tx.vin[0].scriptSig << ToByteVector(redeemScript);

    return tx;
}


CTransaction tx1x2(const CTransaction &prevtx, int prevout, const CScript &txo0, CAmount amt0, const CScript &txo1, CAmount amt1, const CKey& key, bool p2pkh=true)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(prevtx.GetHash(),prevout);
    tx.vin[0].scriptSig = CScript();

    tx.vout.resize(2);
    tx.vout[0].scriptPubKey = txo0;
    tx.vout[0].nValue = amt0;
    tx.vout[1].scriptPubKey = txo1;
    tx.vout[1].nValue = amt1;

    tx.nLockTime = 0;

    unsigned int sighashType = SIGHASH_ALL | SIGHASH_FORKID;
    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(prevtx.vout[prevout].scriptPubKey, tx, 0, sighashType, prevtx.vout[prevout].nValue, 0);
    if (!key.Sign(hash, vchSig))
    {
        assert(0);
    }
    vchSig.push_back((unsigned char)sighashType);
    tx.vin[0].scriptSig << vchSig;
    if (p2pkh)
    {
        tx.vin[0].scriptSig << ToByteVector(key.GetPubKey());
    }

    return tx;
}



CTransaction tx2x1(const COutPoint &utxo1,const COutPoint &utxo2, const CScript &txo, CAmount amt)
{
    CMutableTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout = utxo1;
    tx.vin[1].prevout = utxo2;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = txo;
    tx.vout[0].nValue = amt;
    tx.vin[0].scriptSig = CScript(); // CheckTokenGroups does not validate sig so anything in here
    tx.nLockTime = 0;
    return tx;
}

CTransaction tx2x2(const COutPoint &utxo1,const COutPoint &utxo2, const CScript &txo1, CAmount amt1,const CScript &txo2, CAmount amt2)
{
    CMutableTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout = utxo1;
    tx.vin[1].prevout = utxo2;
    tx.vout.resize(2);
    tx.vout[0].scriptPubKey = txo1;
    tx.vout[0].nValue = amt1;
    tx.vin[0].scriptSig = CScript(); // CheckTokenGroups does not validate sig so anything in here
    tx.vout[1].scriptPubKey = txo2;
    tx.vout[1].nValue = amt2;
    tx.vin[1].scriptSig = CScript(); // CheckTokenGroups does not validate sig so anything in here

    tx.nLockTime = 0;
    return tx;
}

BOOST_AUTO_TEST_CASE(tokengroup_basicfunctions)
{
    CKey secret;
    CPubKey pubkey;
    CKeyID addr;
    uint256 eAddr;
    secret.MakeNewKey(true);
    pubkey = secret.GetPubKey();
    addr = pubkey.GetID();
    eAddr = pubkey.GetHash();

    { // check incorrect group length
        std::vector<unsigned char> fakeGrp(21);
        CScript script = CScript() << fakeGrp << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(BitcoinGroup, CTokenGroupID(addr)));
    }
    { // check incorrect group length
        std::vector<unsigned char> fakeGrp(19);
        CScript script = CScript() << fakeGrp << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(BitcoinGroup, CTokenGroupID(addr)));
    }
    { // check incorrect group length
        std::vector<unsigned char> fakeGrp(1);
        CScript script = CScript() << fakeGrp << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(BitcoinGroup, CTokenGroupID(addr)));
    }
    { // check incorrect group length
        std::vector<unsigned char> fakeGrp(33);
        CScript script = CScript() << fakeGrp << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(BitcoinGroup, CTokenGroupID(addr)));
    }

    { // check correct group length
        std::vector<unsigned char> fakeGrp(20);
        CScript script = CScript() << fakeGrp << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(CTokenGroupID(fakeGrp), CTokenGroupID(addr)));
    }
    { // check correct group length
        std::vector<unsigned char> fakeGrp(32);
        CScript script = CScript() << fakeGrp << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(CTokenGroupID(fakeGrp), CTokenGroupID(addr)));
    }

    { // check P2PKH
        CScript script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(BitcoinGroup, CTokenGroupID(addr)));
    }

    CKey grpSecret;
    CPubKey grpPubkey;
    CKeyID grpAddr;
    uint256 eGrpAddr;
    grpSecret.MakeNewKey(true);
    grpPubkey = secret.GetPubKey();
    grpAddr = pubkey.GetID();
    eGrpAddr = pubkey.GetHash();

    { // check GP2PKH
        CScript script = CScript() << ToByteVector(grpAddr) << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160
                                   << ToByteVector(addr) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(grpAddr, addr));
        CTxDestination resultAddr;
        bool worked = ExtractDestination(script, resultAddr);
        BOOST_CHECK(worked && (resultAddr == CTxDestination(addr)));
    }

    { // check P2SH
        CScript script = CScript() << OP_HASH160 << ToByteVector(addr) << OP_EQUAL;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        CTokenGroupPair correct = CTokenGroupPair(BitcoinGroup, addr);
        BOOST_CHECK(ret == correct);
    }

    { // check GP2SH
        // cheating here a bit because of course addr should the the hash160 of a script not a pubkey but for this test
        // its just bytes
        CScript script = CScript() << ToByteVector(grpAddr) << OP_GROUP << OP_DROP << OP_HASH160 << ToByteVector(addr) << OP_EQUAL;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        BOOST_CHECK(ret == CTokenGroupPair(grpAddr, addr));
        CTxDestination resultAddr;
        bool worked = ExtractDestination(script, resultAddr);
        BOOST_CHECK(worked && (resultAddr == CTxDestination(CScriptID(addr))));
    }

#if 0  // Skip the larger hash version of P2SH until this script format is deployed
    { // check P2SH2
        CScript script = CScript() << OP_HASH256 << ToByteVector(eAddr) << OP_EQUAL;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        // TODO BOOST_CHECK(ret == CTokenGroupPair(BitcoinGroup, eAddr));
    }

    { // check GP2SH2
        CScript script = CScript() << ToByteVector(eGrpAddr) << OP_GROUP << OP_DROP << OP_HASH256 << ToByteVector(eAddr) << OP_EQUAL;
        CTokenGroupPair ret = GetTokenGroupPair(script);
        // TODO BOOST_CHECK(ret == CTokenGroupPair(eGrpAddr, eAddr));
    }
#endif

    // Now test transaction balances
    {
        QuickAddress grp1;
        QuickAddress grp2;
        QuickAddress u1;
        QuickAddress u2;

        // Create a utxo set that I can run tests against
        CCoinsView coinsDummy;
        CCoinsViewCache coins(&coinsDummy);
        CValidationState state;
        COutPoint gutxo = AddUtxo(gp2pkh(grp1.addr, u1.addr),100, coins);
        COutPoint gutxo_burnable = AddUtxo(gp2pkh(grp1.addr, grp1.addr),100, coins);
        COutPoint putxo_mintable = AddUtxo(p2pkh(grp1.addr),100, coins);
        COutPoint putxo = AddUtxo(p2pkh(u1.addr),100, coins);

        // my p2sh will just be a p2pkh inside
        CScript p2shBaseScript = p2pkh(u1.addr);
        CScriptID sid = CScriptID(p2shBaseScript);

        COutPoint gp2sh_meltable = AddUtxo(gp2sh(sid,sid), 100, coins);
        COutPoint p2sh_mintable = AddUtxo(p2sh(sid), 100, coins);
        {
        // check p2sh melt
        CTransaction t = tx1x1(gp2sh_meltable, p2pkh(u1.addr), 100);
        bool ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // check p2sh move to another group (should fail)
        t = tx1x1(gp2sh_meltable, gp2pkh(grp1.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);

        // check p2sh to p2pkh within group controlled by p2sh address
        t = tx1x1(gp2sh_meltable, gp2pkh(sid, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // check p2sh mint
        t = tx1x1(p2sh_mintable, gp2sh(sid, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);
    }
        

        // check same group 1 input 1 output
        CTransaction t = tx1x1(gutxo, gp2pkh(grp1.addr, u1.addr), 100);
        bool ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // check same group 1 input 1 output, wrong value
        t = tx1x1(gutxo, gp2pkh(grp1.addr, u1.addr), 10);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);
        t = tx1x1(gutxo, gp2pkh(grp1.addr, u1.addr), 1000);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);

        // check different groups 1 input 1 output
        t = tx1x1(gutxo, gp2pkh(grp2.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);

        // check mint, incorrect input group address
        t = tx1x1(putxo, gp2pkh(grp2.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);
        t = tx1x1(putxo_mintable, gp2pkh(grp2.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);
        // check mint, correct input group address
        t = tx1x1(putxo_mintable, gp2pkh(grp1.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // check burn but incorrect address
        t = tx1x1(gutxo, p2pkh(u2.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);

        // check burn correct address
        t = tx1x1(gutxo_burnable, p2pkh(u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // check burnable utxo but not burning
        t = tx1x1(gutxo_burnable, gp2pkh(grp1.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // check mintable utxo but not minting
        t = tx1x1(putxo_mintable, p2pkh(u2.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);
        
        

        // Test multiple input/output transactions

        // send 100 coins and burn 100 coins into output
        t = tx2x1(putxo, gutxo_burnable, p2pkh(u2.addr), 200);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // this burns 100 coins into the fee so it works
        t = tx2x1(putxo, gutxo_burnable, p2pkh(u2.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // send 100 coins and burn 100 coins into output, but incorrect amount
        t = tx2x1(putxo, gutxo_burnable, p2pkh(u2.addr), 300);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);

        // partial melt
        t = tx2x2(putxo, gutxo_burnable, p2pkh(u2.addr), 150, gp2pkh(grp1.addr, u1.addr), 50);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // atomic swap tokens
        COutPoint gutxo2 = AddUtxo(gp2pkh(grp2.addr, u2.addr),100, coins);

        t = tx2x2(gutxo, gutxo2, gp2pkh(grp1.addr, u2.addr), 100, gp2pkh(grp2.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // wrong amounts
        t = tx2x2(gutxo, gutxo2, gp2pkh(grp1.addr, u2.addr), 101, gp2pkh(grp2.addr, u1.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);
        t = tx2x2(gutxo, gutxo2, gp2pkh(grp1.addr, u2.addr), 100, gp2pkh(grp2.addr, u1.addr), 99);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);

        // group transaction with 50 sat fee
        t = tx2x2(putxo, gutxo, p2pkh(u1.addr), 50, gp2pkh(grp1.addr, u2.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(ok);

        // group transaction bch imbalance
        t = tx2x2(putxo, gutxo, p2pkh(u1.addr), 101, gp2pkh(grp1.addr, u2.addr), 100);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);

        // group transaction with group imbalance
        t = tx2x2(putxo, gutxo, p2pkh(u1.addr), 50, gp2pkh(grp1.addr, u2.addr), 101);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);
        // group transaction with group imbalance
        t = tx2x2(putxo, gutxo, p2pkh(u1.addr), 50, gp2pkh(grp1.addr, u2.addr), 99);
        ok = CheckTokenGroups(t, state, coins);
        BOOST_CHECK(!ok);
    }
}

static bool tryBlock(const std::vector<CMutableTransaction> &txns,
                             const CScript &scriptPubKey, CBlock& result, CValidationState &state)
{
    const CChainParams &chainparams = Params();
    CBlockTemplate *pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey);
    CBlock &block = pblocktemplate->block;

    // Replace mempool-selected txns with just coinbase plus passed-in txns:
    block.vtx.resize(1);
    BOOST_FOREACH (const CMutableTransaction &tx, txns)
        block.vtx.push_back(tx);
    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    unsigned int extraNonce = 0;
    IncrementExtraNonce(&block, extraNonce);

    while (!CheckProofOfWork(block.GetHash(), block.nBits, chainparams.GetConsensus()))
        ++block.nNonce;

    bool ret;
    ret = ProcessNewBlock(state, chainparams, NULL, &block, true, NULL, false);
    result = block;
    delete pblocktemplate;
    return ret;
}

static bool tryMempool(const CTransaction& tx, CValidationState &state)
{
    LOCK(cs_main);
    bool inputsMissing = false;
    return AcceptToMemoryPool(mempool, state, tx, false, &inputsMissing, true, false);
}

BOOST_FIXTURE_TEST_CASE(tokengroup_blockchain, TestChain100Setup)
{
    //fPrintToConsole = true;
    //LogToggleCategory(Logging::TOKGRP, true);
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    std::vector<CMutableTransaction> txns;

    QuickAddress p2pkGrp(coinbaseKey);
    QuickAddress grp0(4);
    QuickAddress grp1(1);
    QuickAddress a1(2);
    QuickAddress a2(3);

    CValidationState state;
    CBlock blk1;
    CBlock tipblk;
    CBlock badblk;  // If I'm expecting a failure, I stick the block in badblk so that I still have the chain tip

    // just regress making a block
    bool ret = tryBlock(txns,p2pkh(grp1.addr),blk1,state);
    BOOST_CHECK(ret);
    if (!ret) return; // no subsequent test will work

    txns.push_back(CMutableTransaction());  // Make space for 1 tx in the vector

    {
    // Should fail: bad group size
    uint256 hash = blk1.vtx[0].GetHash();
    std::vector<unsigned char> fakeGrp(21);
    CScript script = CScript() << fakeGrp << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160
                               << ToByteVector(a1.addr) << OP_EQUALVERIFY << OP_CHECKSIG;

    txns[0] = tx1x1(COutPoint(hash,0), script, blk1.vtx[0].vout[0].nValue);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);
    }

    // Should fail: premature coinbase spend into a group mint
    uint256 hash = blk1.vtx[0].GetHash();
    txns[0] = tx1x1(COutPoint(hash,0), gp2pkh(grp1.grp, a1.addr), blk1.vtx[0].vout[0].nValue);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // Since the TestChain100Setup creates p2pk outputs this won't work
    txns[0] = tx1x1(COutPoint(coinbaseTxns[0].GetHash(),0), gp2pkh(p2pkGrp.grp, a1.addr), coinbaseTxns[0].vout[0].nValue,
                    coinbaseKey, coinbaseTxns[0].vout[0].scriptPubKey, false);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // so spend to a p2pkh address so we can tokenify it
    txns[0] = tx1x1(COutPoint(coinbaseTxns[0].GetHash(),0), p2pkh(grp0.addr), coinbaseTxns[0].vout[0].nValue,
                    coinbaseKey, coinbaseTxns[0].vout[0].scriptPubKey, false);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // coinbase output in the first block as our group so minting should work
    txns[0] = tx1x1(COutPoint(tipblk.vtx[1].GetHash(),0), gp2pkh(grp0.grp, a1.addr), tipblk.vtx[1].vout[0].nValue,
                    grp0.secret, tipblk.vtx[1].vout[0].scriptPubKey, true);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // Useful printouts:
    // Note state response is broken due to state = CValidationState(); in main.cpp:ActivateBestChainStep
    //printf("state: %d:%s, %s\n", state.GetRejectCode(), state.GetRejectReason().c_str(), state.GetDebugMessage().c_str())
    //printf("%s\n", CTransaction(txns[0]).ToString().c_str());
    //printf("%s\n", HexStr(txns[0]).c_str());
    //printf("state: %d:%s, %s\n", state.GetRejectCode(), state.GetRejectReason().c_str(), state.GetDebugMessage().c_str());

    // Should fail: pay from group to non-groups
    txns[0] = tx1x1(COutPoint(tipblk.vtx[1].GetHash(),0), p2pkh(a2.addr), tipblk.vtx[1].vout[0].nValue,
                    a1.secret, tipblk.vtx[1].vout[0].scriptPubKey);
    ret = tryMempool(txns[0], state);
    BOOST_CHECK(!ret);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // now try the same but to the correct group
    txns[0] = tx1x1(COutPoint(tipblk.vtx[1].GetHash(),0), gp2pkh(grp0.grp, a2.addr), tipblk.vtx[1].vout[0].nValue,
                    a1.secret, tipblk.vtx[1].vout[0].scriptPubKey);
    ret = tryMempool(txns[0], state);
    BOOST_CHECK(ret);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // To make sure blocks get accepted or rejected without the block's tx in the mempool, I
    // won't use the mempool for the rest of this test.

    // Should fail: see if an unbalanced group but balanced btc tx is accepted
    txns[0] = tx1x2(tipblk.vtx[1],0,
                    gp2pkh(grp0.grp, a1.addr), tipblk.vtx[1].vout[0].nValue-100000,
                    p2pkh(a2.addr), 100000,
                    a2.secret);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // same tx as above but spend both to the group output should work because balanced
    txns[0] = tx1x2(tipblk.vtx[1],0,
                    gp2pkh(grp0.grp, grp0.addr), tipblk.vtx[1].vout[0].nValue-1,
                    gp2pkh(grp0.grp, a2.addr), 1,
                    a2.secret);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // Should fail: try to melt the 2nd output (should fail because not grp addr)
    txns[0] = tx1x1(tipblk.vtx[1],1,
                    p2pkh(a1.addr), tipblk.vtx[1].vout[1].nValue,
                    a2.secret);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // try to melt the 1st output (should succeed)
    txns[0] = tx1x1(tipblk.vtx[1],0,
                    p2pkh(a1.addr), tipblk.vtx[1].vout[0].nValue,
                    grp0.secret);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // P2SH
    CScript p2shBaseScript1 = p2pkh(a1.addr);
    CScriptID sid1 = CScriptID(p2shBaseScript1);
    CScript p2shBaseScript2 = p2pkh(a2.addr);
    CScriptID sid2 = CScriptID(p2shBaseScript2);

    // Spend to a p2sh address so we can tokenify it
    txns[0] = tx1x1(COutPoint(coinbaseTxns[1].GetHash(),0), p2sh(sid1), coinbaseTxns[1].vout[0].nValue,
                    coinbaseKey, coinbaseTxns[1].vout[0].scriptPubKey, false);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // Mint to a different p2sh destination
    txns[0] = tx1x1_p2sh_of_p2pkh(tipblk.vtx[1], 0, gp2sh(sid1, sid2), tipblk.vtx[1].vout[0].nValue, a1.secret,p2shBaseScript1);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // FAIL: Spend that gp2sh to a p2pkh, still under the group controlled by a p2sh address
    txns[0] = tx1x1_p2sh_of_p2pkh(tipblk.vtx[1], 0, p2pkh(a1.addr), tipblk.vtx[1].vout[0].nValue, a2.secret,p2shBaseScript2);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // FAIL: Spend that gp2sh to a p2sh
    txns[0] = tx1x1_p2sh_of_p2pkh(tipblk.vtx[1], 0, p2sh(sid1), tipblk.vtx[1].vout[0].nValue, a2.secret,p2shBaseScript2);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // Spend that gp2sh to a gp2pkh, still under the group controlled by a p2sh address
    txns[0] = tx1x1_p2sh_of_p2pkh(tipblk.vtx[1], 0, gp2pkh(sid1, a1.addr), tipblk.vtx[1].vout[0].nValue, a2.secret,p2shBaseScript2);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // FAIL: Spend back into the controlling non-grouped p2sh
    txns[0] = tx1x1(tipblk.vtx[1], 0, p2sh(sid1), tipblk.vtx[1].vout[0].nValue, a1.secret);
    ret = tryBlock(txns,p2pkh(a2.addr), badblk, state);
    BOOST_CHECK(!ret);

    // Spend back into the controlling p2sh
    txns[0] = tx1x1(tipblk.vtx[1], 0, gp2sh(sid1, sid1), tipblk.vtx[1].vout[0].nValue, a1.secret);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);

    // melt into bch
    txns[0] = tx1x1_p2sh_of_p2pkh(tipblk.vtx[1], 0, p2pkh(a2.addr), tipblk.vtx[1].vout[0].nValue, a1.secret, p2shBaseScript1);
    ret = tryBlock(txns,p2pkh(a2.addr), tipblk, state);
    BOOST_CHECK(ret);
    
}

BOOST_AUTO_TEST_SUITE_END()

