// Copyright (c) 2018 The Bitcoin developers
// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "respend/respendrelayer.h"
#include "DoubleSpendProofStorage.h"
#include "key.h"
#include "keystore.h"
#include "net.h"
#include "random.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "test/test_bitcoin.h"
#include "txadmission.h"

#include <boost/test/unit_test.hpp>

using namespace respend;

BOOST_AUTO_TEST_SUITE(respendrelayer_tests);


// Helper: create two dummy transactions, each with
// two outputs.  The first has 11 and 50 CENT outputs
// paid to a TX_PUBKEY, the second 21 and 22 CENT outputs
// paid to a TX_PUBKEYHASH.
//
static std::vector<CMutableTransaction> SetupDummyInputs(CBasicKeyStore &keystoreRet, CCoinsViewCache &coinsRet)
{
    std::vector<CMutableTransaction> dummyTransactions;
    dummyTransactions.resize(2);

    // Add some keys to the keystore:
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(i % 2);
        keystoreRet.AddKey(key[i]);
    }

    // Create some dummy input transactions
    int nHeight = 1000; // any height will do
    dummyTransactions[0].vout.resize(2);
    dummyTransactions[0].vout[0].nValue = 50 * CENT;
    dummyTransactions[0].vout[0].scriptPubKey << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    dummyTransactions[0].vout[1].nValue = 50 * CENT;
    dummyTransactions[0].vout[1].scriptPubKey << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
    AddCoins(coinsRet, dummyTransactions[0], nHeight);

    return dummyTransactions;
}

static void ClearInventory(CNode *pnode)
{
    LOCK(pnode->cs_inventory);
    pnode->vInventoryToSend.clear();
}

BOOST_AUTO_TEST_CASE(not_interesting)
{
    RespendRelayer r;
    BOOST_CHECK(!r.IsInteresting());
    uint256 dummy;
    bool lookAtMore;

    lookAtMore =
        r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(CTransaction{}), true /* seen before */, false);
    BOOST_CHECK(lookAtMore);
    BOOST_CHECK(!r.IsInteresting());

    lookAtMore =
        r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(CTransaction{}), false, true /* is equivalent */);
    BOOST_CHECK(lookAtMore);
    BOOST_CHECK(!r.IsInteresting());
}

BOOST_AUTO_TEST_CASE(is_interesting)
{
    RespendRelayer r;
    uint256 dummy;
    bool lookAtMore;

    lookAtMore = r.AddOutpointConflict(COutPoint{}, dummy, MakeTransactionRef(CTransaction{}), false, false);
    BOOST_CHECK(!lookAtMore);
    BOOST_CHECK(r.IsInteresting());
}

BOOST_FIXTURE_TEST_CASE(triggers_correctly, TestChain100Setup)
{
    CTxMemPool pool;
    TestMemPoolEntryHelper entry;
    CBasicKeyStore keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(pcoinsTip);
    std::vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, *pcoinsTip);

    // Create a basic signed transactions and add them to the pool. We will use these transactions
    // to create the spend and respend transactions.
    CMutableTransaction t1;
    t1.vin.resize(1);
    t1.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t1.vin[0].prevout.n = 0;
    t1.vout.resize(1);
    t1.vout[0].nValue = 50 * CENT;
    CKey key;
    key.MakeNewKey(true);
    keystore.AddKey(key);
    t1.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    CTransaction tx1(t1);
    {
        TransactionSignatureCreator tsc(&keystore, &tx1, 0, 50 * CENT, SIGHASH_ALL | SIGHASH_FORKID);
        const CScript &scriptPubKey = dummyTransactions[0].vout[0].scriptPubKey;
        CScript &scriptSigRes = t1.vin[0].scriptSig;
        bool worked = ProduceSignature(tsc, scriptPubKey, scriptSigRes);
        BOOST_CHECK(worked);
    }
    pool.addUnchecked(tx1.GetHash(), entry.FromTx(tx1));

    CMutableTransaction t2;
    t2.vin.resize(1);
    t2.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t2.vin[0].prevout.n = 1;
    t2.vout.resize(1);
    t2.vout[0].nValue = 50 * CENT;
    key.MakeNewKey(true);
    keystore.AddKey(key);
    t2.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    CTransaction tx2(t2);
    {
        TransactionSignatureCreator tsc(&keystore, &tx2, 0, 50 * CENT, SIGHASH_ALL | SIGHASH_FORKID);
        const CScript &scriptPubKey = dummyTransactions[0].vout[1].scriptPubKey;
        CScript &scriptSigRes = t2.vin[0].scriptSig;
        bool worked = ProduceSignature(tsc, scriptPubKey, scriptSigRes);
        BOOST_CHECK(worked);
    }
    pool.addUnchecked(tx2.GetHash(), entry.FromTx(tx2));


    // Create a spend of tx1 and tx2's output.
    CMutableTransaction s1;
    s1.vin.resize(2);
    s1.vin[0].prevout.hash = tx1.GetHash();
    s1.vin[0].prevout.n = 0;
    s1.vin[1].prevout.hash = tx2.GetHash();
    s1.vin[1].prevout.n = 0;
    s1.vout.resize(1);
    s1.vout[0].nValue = 100 * CENT;
    CKey key1;
    key1.MakeNewKey(true);
    keystore.AddKey(key1);
    s1.vout[0].scriptPubKey = GetScriptForDestination(key1.GetPubKey().GetID());

    CTransaction spend1(s1);
    {
        TransactionSignatureCreator tsc(&keystore, &spend1, 0, 100 * CENT, SIGHASH_ALL | SIGHASH_FORKID);
        const CScript &scriptPubKey = tx1.vout[0].scriptPubKey;
        CScript &scriptSigRes = s1.vin[0].scriptSig;
        bool worked = ProduceSignature(tsc, scriptPubKey, scriptSigRes);
        BOOST_CHECK(worked);

        const CScript &scriptPubKey2 = tx2.vout[0].scriptPubKey;
        CScript &scriptSigRes2 = s1.vin[1].scriptSig;
        worked = ProduceSignature(tsc, scriptPubKey2, scriptSigRes2);
        BOOST_CHECK(worked);
    }
    CTransaction spend1a(s1);
    pool.addUnchecked(spend1a.GetHash(), entry.FromTx(spend1a));
    CTxMemPool::txiter iter = pool.mapTx.find(spend1a.GetHash());


    // Create a respend s1's output.
    CMutableTransaction s2;
    s2.vin.resize(1);
    s2.vin[0].prevout.hash = tx1.GetHash();
    s2.vin[0].prevout.n = 0;
    s2.vout.resize(1);
    s2.vout[0].nValue = 50 * CENT;
    key.MakeNewKey(true);
    keystore.AddKey(key);
    s2.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    CTransaction spend2(s2);
    {
        TransactionSignatureCreator tsc(&keystore, &spend2, 0, 50 * CENT, SIGHASH_ALL | SIGHASH_FORKID);
        const CScript &scriptPubKey = tx1.vout[0].scriptPubKey;
        CScript &scriptSigRes = s2.vin[0].scriptSig;
        bool worked = ProduceSignature(tsc, scriptPubKey, scriptSigRes);
        BOOST_CHECK(worked);
    }
    CTransaction spend2a(s2);


    CNode node(INVALID_SOCKET, CAddress());
    node.fRelayTxes = true;
    LOCK(cs_vNodes);
    vNodes.push_back(&node);
    // Create a "not interesting" respend
    RespendRelayer r;
    ClearInventory(&node);
    r.AddOutpointConflict(COutPoint{}, iter->GetSharedTx()->GetHash(), MakeTransactionRef(spend2a), true, false);
    r.Trigger(pool);
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());
    r.SetValid(true);
    r.Trigger(pool);
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());


    // Create an interesting, but invalid respend
    ClearInventory(&node);
    r.AddOutpointConflict(COutPoint{}, iter->GetSharedTx()->GetHash(), MakeTransactionRef(spend2a), false, false);
    BOOST_CHECK(r.IsInteresting());
    r.SetValid(false);
    r.Trigger(pool);
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());
    // make valid
    r.SetValid(true);
    r.Trigger(pool);
    BOOST_CHECK_EQUAL(size_t(1), node.GetInventoryToSendSize());
    {
        // Check that a dsproof was created and then inventory message was sent.
        LOCK(node.cs_inventory);
        BOOST_CHECK(pool.doubleSpendProofStorage()->exists(node.vInventoryToSend.at(0).hash) == true);
        BOOST_CHECK(0x94a0 == node.vInventoryToSend.at(0).type);
    }

    // Create another dsproof for against the same original first tx...it should not be possible
    {
        TransactionSignatureCreator tsc(&keystore, &spend2, 0, 50 * CENT, SIGHASH_ALL | SIGHASH_FORKID);
        const CScript &scriptPubKey = tx1.vout[0].scriptPubKey;
        CScript &scriptSigRes = s2.vin[0].scriptSig;
        bool worked = ProduceSignature(tsc, scriptPubKey, scriptSigRes);
        BOOST_CHECK(worked);
    }
    CTransaction spend2b(s2);
    ClearInventory(&node);
    r.AddOutpointConflict(COutPoint{}, iter->GetSharedTx()->GetHash(), MakeTransactionRef(spend2b), false, false);
    // make valid
    r.SetValid(true);
    r.Trigger(pool);
    BOOST_CHECK_EQUAL(size_t(0), node.GetInventoryToSendSize());


    // Make sure dsproof is the same regardless of the order of txns
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp_first = DoubleSpendProof::create(iter->GetTx(), spend2b, pool);
        const auto dsp_second = DoubleSpendProof::create(spend2b, iter->GetTx(), pool);
        BOOST_CHECK_EQUAL(dsp_first.GetHash(), dsp_second.GetHash());
    }

    // The following tests will check for dsproof exceptions

    // 1) Create a dsproof where one transaction is not a bitcoin cash transaction (no fork id)
    {
        // remove the dsproof flag from the in mempool transaction
        WRITELOCK(pool.cs_txmempool);
        auto item = *iter;
        item.dsproof = -1;
        pool.mapTx.replace(iter, item);
    }
    {
        // create a tx without a fork id
        TransactionSignatureCreator tsc(&keystore, &spend2, 0, 50 * CENT, SIGHASH_ALL);
        const CScript &scriptPubKey = tx1.vout[0].scriptPubKey;
        CScript &scriptSigRes = s2.vin[0].scriptSig;
        bool worked = ProduceSignature(tsc, scriptPubKey, scriptSigRes);
        BOOST_CHECK(worked);
    }
    CTransaction spend2c(s2);
    ClearInventory(&node);
    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(iter->GetTx(), spend2c, pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "Tx2 is not a Bitcoin Cash transaction");
    }

    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(spend2c, iter->GetTx(), pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "Tx1 is not a Bitcoin Cash transaction");
    }

    // 2) Create a dsproof that where the transactions do not double spend each other
    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(spend2a, tx1, pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "Transactions do not double spend each other");
    }

    // 3) Create a dsproof from a transaction that has no inputs
    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(spend2a, dummyTransactions[0], pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "Transactions do not double spend each other");
    }

    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(dummyTransactions[0], spend2a, pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "Transactions do not double spend each other");
    }

    // 4) Create a dsproof with a missing signature
    s2.vin[0].scriptSig = CScript();
    CTransaction spend2d(s2);
    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(spend2d, spend1, pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "scriptSig has no signature");
    }

    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(spend1, spend2d, pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "scriptSig has no signature");
    }

    // 5) Should not be able to create a dsproof from two identical transactions
    try
    {
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(iter->GetTx(), iter->GetTx(), pool);
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "Can not create dsproof from identical transactions");
    }

    // 6) Create a dsproof where one transaction is a spend from a coinbase (P2PK).  It should not
    //    be possible. Create t3 which double spends t1's coinbase.
    CMutableTransaction t3;
    t3.vin.resize(1);
    t3.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t3.vin[0].prevout.n = 0;
    t3.vout.resize(1);
    t3.vout[0].nValue = 50 * CENT;
    key.MakeNewKey(true);
    keystore.AddKey(key);
    t3.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    CTransaction tx3(t3);
    {
        TransactionSignatureCreator tsc(&keystore, &tx3, 0, 50 * CENT, SIGHASH_ALL | SIGHASH_FORKID);
        const CScript &scriptPubKey = dummyTransactions[0].vout[0].scriptPubKey;
        CScript &scriptSigRes = t3.vin[0].scriptSig;
        bool worked = ProduceSignature(tsc, scriptPubKey, scriptSigRes);
        BOOST_CHECK(worked);
    }
    CTransaction spendt1(t1);
    CTransaction spendt3(t3);
    pool.addUnchecked(spendt3.GetHash(), entry.FromTx(spendt3));
    pool.addUnchecked(spendt1.GetHash(), entry.FromTx(spendt1));
    try
    {
        // both spendt3 and spendt1 are P2PK.
        READLOCK(pool.cs_txmempool);
        const auto dsp = DoubleSpendProof::create(spendt3, spendt1, pool);
        DoubleSpendProof::Validity validity;
        validity = dsp.validate(pool);
        if (validity == DoubleSpendProof::Invalid)
            throw std::runtime_error("Invalid dsproof");

        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    }
    catch (const std::runtime_error &e)
    {
        BOOST_CHECK_EQUAL(e.what(), "Can not create dsproof: Transaction was not P2PKH");
    }

    // Cleanup
    vNodes.erase(vNodes.end() - 1);
}

BOOST_AUTO_TEST_SUITE_END();
