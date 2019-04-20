#include "test/test_bitcoin.h"
#include "test/test_random.h"
#include "arith_uint256.h"
#include "deltablocks.h"
#include "consensus/merkle.h"
#include <boost/test/unit_test.hpp>
#include <map>
#include <iostream>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(deltablock_tests, BasicTestingSetup)

void tcase(std::string x) {
    arith_uint256 a(x);
    arith_uint256 b;
    b.SetCompact(weakPOWfromPOW(a.GetCompact()));
    double ratio = a.getdouble()/ b.getdouble();
    BOOST_CHECK(ratio < 1.0/900);
    BOOST_CHECK(ratio > 1.0/1100);
}

BOOST_AUTO_TEST_CASE(weakpow)
{
    CDeltaBlock::resetAll();
    tcase("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    tcase("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    tcase("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    tcase("0000000000000000000000000fffffffffffffffffffffffffffffffffffffff");
    tcase("00000000000000000000000000000000000000ffffffffffffffffffffffffff");
    tcase("00000000000000000000000000000000000000000000000000ffffffffffffff");
    tcase("000000000000000000000000000000000000000000000000000000000000ffff");

}

static uint256 hash1(uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
static uint256 hash2(uint256S("00000000000000000000000000000000000000ffffffffffffffffffffffffff"));

BOOST_AUTO_TEST_CASE(construct1_and_some_static_fns)
{
    CDeltaBlock::resetAll();
    CBlockHeader dummyheader;
    CMutableTransaction dummycb;

    std::vector<uint256> hashes;
    hashes.emplace_back(hash1);
    hashes.emplace_back(hash2);
    CDeltaBlock::addAncestorOPRETURNs(dummycb, hashes);

    CDeltaBlockRef dbr(new CDeltaBlock(dummyheader, MakeTransactionRef(dummycb)));

    std::vector<uint256> ah = dbr->ancestorHashes();
    BOOST_CHECK_EQUAL(ah.size(), 2);
    BOOST_CHECK_EQUAL(ah[0], hashes[0]);
    BOOST_CHECK_EQUAL(ah[1], hashes[1]);

    BOOST_CHECK_EQUAL(dbr->deltaSet().size(), 0);
    BOOST_CHECK(dbr->weakPOW() < 0);
    BOOST_CHECK(dbr->compatible(*dbr));

    std::vector<ConstCDeltaBlockRef> blocks;
    BOOST_CHECK(dbr->compatible(blocks));
    blocks.emplace_back(dbr);
    BOOST_CHECK(dbr->compatible(blocks));

    BOOST_CHECK_EQUAL(CDeltaBlock::tips(hash1).size(), 0);
    BOOST_CHECK_EQUAL(CDeltaBlock::tips(dbr->hashPrevBlock).size(), 0);
    BOOST_CHECK_EQUAL(dbr->numTransactions(), 1); // only CB
    BOOST_CHECK_EQUAL(CDeltaBlock::knownInReceiveOrder().size(), 0);

    CMutableTransaction dummytx;
    dbr->add(MakeTransactionRef(dummytx));
    BOOST_CHECK_EQUAL(dbr->deltaSet().size(), 1);

    dbr->setAllTransactionsKnown();
    BOOST_CHECK_EQUAL(dbr->weakPOW(), 1);

    BOOST_CHECK(!dbr->isStrong());
}

/* Random transaction generation helpers for testing delta blocks
 * merging and compatibility checks. Note that the resulting ins and
 * outs and transactions are not random in any general way, just
 * enough to be used for deltablocks testing. */

COutPoint rndOutpoint() {
    COutPoint result;
    result.hash = InsecureRand256();
    result.n = insecure_rand();
    return result;
}

/*! If doublespend != nullptr, take an input from that transaction and (double) spend it. */
CTransactionRef rndTransaction(const CTransactionRef &doublespend = nullptr) {
    size_t n_input = insecure_rand() % 4 + 1;
    size_t n_output = insecure_rand() % 4 + 1;
    CMutableTransaction result;
    if (doublespend != nullptr)
        result.vin.emplace_back(doublespend->vin[insecure_rand() % doublespend->vin.size()]);

    while (result.vin.size() < n_input) {
        CTxIn inp;
        inp.prevout = rndOutpoint();
        result.vin.emplace_back(inp);
    }
    while (result.vout.size() < n_output)
        result.vout.emplace_back(CTxOut());
    return CTransactionRef(new CTransaction(result));
}

void addSomeTx(CDeltaBlockRef &dbr, size_t n) {
    for (size_t i = 0; i < n; i++)
        dbr->add(rndTransaction());
}

void finalize(CDeltaBlockRef& dbr) {
    dbr->setAllTransactionsKnown();
    dbr->hashMerkleRoot =  BlockMerkleRoot(*dbr);
}

BOOST_AUTO_TEST_CASE(deltatree)
{
    CDeltaBlock::resetAll();
    CBlockHeader headertemplate;
    headertemplate.hashPrevBlock = hash1;
    CDeltaBlock::newStrong(hash1);
    CMutableTransaction cbtmpl;
    cbtmpl.vin.resize(1);
    cbtmpl.vin[0].prevout.SetNull();
    CDeltaBlockRef dbr(new CDeltaBlock(headertemplate, CTransactionRef(new CTransaction(cbtmpl))));
    addSomeTx(dbr, 100);
    finalize(dbr);
    BOOST_CHECK(dbr->coinbase()->IsCoinBase());
    BOOST_CHECK(dbr->allTransactionsKnown());
    BOOST_CHECK_EQUAL(dbr->numTransactions(), 101);
    BOOST_CHECK_EQUAL(dbr->deltaSet().size(), 100);
    BOOST_CHECK(dbr->compatible(*dbr));

    CDeltaBlock::tryRegister(dbr);

    BOOST_CHECK_EQUAL(dbr, CDeltaBlock::byHash(dbr->GetHash()));
    BOOST_CHECK_EQUAL(dbr, CDeltaBlock::latestForStrong(hash1));

    static std::map<uint256, std::vector<ConstCDeltaBlockRef> > kiro = CDeltaBlock::knownInReceiveOrder();
    BOOST_CHECK_EQUAL(kiro.size(), 1);
    for (auto p : kiro) {
        BOOST_CHECK_EQUAL(p.first, hash1);
        BOOST_CHECK_EQUAL(p.second.size(), 1);
        BOOST_CHECK_EQUAL(p.second[0], dbr);
    }

    BOOST_CHECK_EQUAL(dbr->weakPOW(), 1);

    // hash2 not known
    BOOST_CHECK_EQUAL(CDeltaBlock::bestTemplate(hash2)->ancestors().size(), 0);

    // known but no descendants
    CDeltaBlock::newStrong(hash2);
    BOOST_CHECK_EQUAL(CDeltaBlock::bestTemplate(hash2)->ancestors().size(), 0);

    CDeltaBlockRef b2 = CDeltaBlock::bestTemplate(hash1);
    addSomeTx(b2, 30);
    finalize(b2);
    BOOST_CHECK(b2->coinbase()->IsCoinBase());
    BOOST_CHECK_EQUAL(b2->numTransactions(), 131);
    BOOST_CHECK_EQUAL(b2->deltaSet().size(), 30);
    BOOST_CHECK(b2->compatible(*dbr));
    BOOST_CHECK(dbr->compatible(*b2));
    BOOST_CHECK_EQUAL(b2->ancestors().size(), 1);

    CDeltaBlockRef b3 = CDeltaBlock::bestTemplate(hash1);
    addSomeTx(b3, 40);
    // add some compatible txn from b2
    b3->add(b2->deltaSet()[10]);
    b3->add(b2->deltaSet()[15]);
    b3->add(b2->deltaSet()[20]);

    finalize(b3);
    BOOST_CHECK(b3->coinbase()->IsCoinBase());
    BOOST_CHECK_EQUAL(b3->numTransactions(), 144);
    BOOST_CHECK_EQUAL(b3->deltaSet().size(), 43);
    BOOST_CHECK(b2->compatible(*dbr));
    BOOST_CHECK(dbr->compatible(*b2));
    BOOST_CHECK(b3->compatible(*b2));
    BOOST_CHECK(b2->compatible(*b3));
    BOOST_CHECK_EQUAL(b3->ancestors().size(), 1);

    // this should simulate concurrently generated blocks
    CDeltaBlock::tryRegister(b2);
    CDeltaBlock::tryRegister(b3);

    // b1 <- b2 <-- b4
    //    <- b3 <-/
    CDeltaBlockRef b4 = CDeltaBlock::bestTemplate(hash1);
    addSomeTx(b4, 50);
    finalize(b4);
    for (auto anc : b4->ancestors()) {
        BOOST_TEST_MESSAGE("ancestor b4: " << anc->GetHash() << " "
                           << anc->numTransactions() << " "
                           << anc->deltaSet().size());
    }
    BOOST_CHECK_EQUAL(b4->ancestors().size(), 2); // b2, b3
    BOOST_CHECK(b4->coinbase()->IsCoinBase());
    BOOST_CHECK_EQUAL(b4->numTransactions(), 101+30+40+50);
    BOOST_CHECK_EQUAL(b4->deltaSet().size(), 50);
    BOOST_CHECK_EQUAL(b4->weakPOW(), 4);

    // b1 <- b2 <-- b5 (incompatible to b4)
    //    <- b3 <-/
    CDeltaBlockRef b5 = CDeltaBlock::bestTemplate(hash1);
    addSomeTx(b5, 60);

    // make incompatible to b4
    b5->add(rndTransaction(b4->deltaSet()[25]));
    finalize(b5);
    BOOST_CHECK_EQUAL(b5->ancestors().size(), 2); // b2, b3
    BOOST_CHECK_EQUAL(b5->numTransactions(), 101+30+40+61);
    BOOST_CHECK_EQUAL(b5->deltaSet().size(), 61);
    BOOST_CHECK(!b4->compatible(*b5));
    BOOST_CHECK(!b5->compatible(*b4));
    BOOST_CHECK_EQUAL(b5->weakPOW(), 4); // b4 missing

    CDeltaBlock::tryRegister(b5);
    // and extend b5 (as b5 is not known yet)
    // b1 <- b2 <-- b4, b5 <-- b6
    //    <- b3 <-/
    CDeltaBlockRef b6 = CDeltaBlock::bestTemplate(hash1);
    addSomeTx(b6, 70);
    finalize(b6);
    BOOST_CHECK_EQUAL(b6->ancestors().size(), 1); // b5
    BOOST_CHECK_EQUAL(b6->ancestors()[0], b5);
    BOOST_CHECK_EQUAL(b6->numTransactions(), 101+30+40+61+70);
    BOOST_CHECK_EQUAL(b6->deltaSet().size(), 70);
    BOOST_CHECK(!b4->compatible(*b6));
    BOOST_CHECK(b5->compatible(*b6));
    BOOST_CHECK_EQUAL(b6->weakPOW(), 5); // b4 missing
    CDeltaBlock::tryRegister(b6);
    {
        auto tips = CDeltaBlock::tips(hash1);
        BOOST_CHECK_EQUAL(tips.size(), 1);
        BOOST_CHECK_EQUAL(tips[0], b6);
    }
    {
        CDeltaBlock::tryRegister(b4);
        auto tips = CDeltaBlock::tips(hash1);
        BOOST_CHECK_EQUAL(tips.size(), 2);
        BOOST_CHECK_EQUAL(tips[0], b6); // came first and more POW
        BOOST_CHECK_EQUAL(tips[1], b4);
    }

    // now, lets put two blocks onto B4 to make it longer than B6
    std::vector<ConstCDeltaBlockRef> tips_override;
    tips_override.emplace_back(b4);
    CDeltaBlockRef b7 = CDeltaBlock::bestTemplate(hash1, &tips_override);
    addSomeTx(b7, 8);
    finalize(b7);

    BOOST_CHECK_EQUAL(b7->ancestors().size(), 1); // b4
    BOOST_CHECK_EQUAL(b7->ancestors()[0], b4);
    BOOST_CHECK_EQUAL(b7->numTransactions(), 101+30+40+50+8);
    BOOST_CHECK_EQUAL(b7->deltaSet().size(), 8);
    BOOST_CHECK(!b7->compatible(*b6));
    BOOST_CHECK(b7->compatible(*b4));
    BOOST_CHECK_EQUAL(b7->weakPOW(), 5); // b5,b6 missing

    {
        CDeltaBlock::tryRegister(b7);
        auto tips = CDeltaBlock::tips(hash1);
        BOOST_CHECK_EQUAL(tips.size(), 2);
        BOOST_CHECK_EQUAL(tips[0], b6); // came first
        BOOST_CHECK_EQUAL(tips[1], b7);
    }

    tips_override.clear();
    tips_override.emplace_back(b7);
    CDeltaBlockRef b8 = CDeltaBlock::bestTemplate(hash1, &tips_override);
    addSomeTx(b8, 9);
    finalize(b8);

    BOOST_CHECK_EQUAL(b8->ancestors().size(), 1); // b7
    BOOST_CHECK_EQUAL(b8->ancestors()[0], b7);
    BOOST_CHECK_EQUAL(b8->numTransactions(), 101+30+40+50+8+9);
    BOOST_CHECK_EQUAL(b8->deltaSet().size(), 9);
    BOOST_CHECK(!b7->compatible(*b6));
    BOOST_CHECK(b7->compatible(*b4));
    BOOST_CHECK_EQUAL(b8->weakPOW(), 6); // b5,b6 missing

    {
        CDeltaBlock::tryRegister(b8);
        auto tips = CDeltaBlock::tips(hash1);
        BOOST_CHECK_EQUAL(tips.size(), 2);
        BOOST_CHECK_EQUAL(tips[0], b6); // still came first
        BOOST_CHECK_EQUAL(tips[1], b8);
    }
}

BOOST_AUTO_TEST_SUITE_END()
