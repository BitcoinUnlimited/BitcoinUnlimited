// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "weakblock.h"
#include "consensus/merkle.h"
#include "test/test_bitcoin.h"
#include "test/testutil.h"
#include "test/test_random.h"
#include <boost/test/unit_test.hpp>
#include <string>
#include <algorithm>

using namespace std;

struct WeakTestSetup : public TestingSetup {
    WeakTestSetup() : TestingSetup() {
        wbEnable.Set(DEFAULT_WEAKBLOCKS_ENABLE);
        wbConsiderPOWratio.Set(DEFAULT_WEAKBLOCKS_CONSIDER_POW_RATIO);
        weakstore.expireOld(true);
    }
    ~WeakTestSetup() {
        weakstore.consistencyCheck();
    }
};
static uint256 null;

BOOST_FIXTURE_TEST_SUITE(weakblock_tests, WeakTestSetup)

// check basic state when everything's fresh and empty
BOOST_AUTO_TEST_CASE(default_tests)
{
    LOCK(cs_weakblocks);

    BOOST_CHECK_EQUAL(weakblocksEnabled(), DEFAULT_WEAKBLOCKS_ENABLE);
    BOOST_CHECK_EQUAL(weakblocksConsiderPOWRatio(), DEFAULT_WEAKBLOCKS_CONSIDER_POW_RATIO);
    BOOST_CHECK_EQUAL(weakblocksMinPOWRatio(), 600);
    wbConsiderPOWratio.Set("123");
    BOOST_CHECK_EQUAL(weakblocksConsiderPOWRatio(), 123);
    wbEnable.Set("false");
    BOOST_CHECK_EQUAL(weakblocksEnabled(), false);
    wbEnable.Set("true");
    BOOST_CHECK_EQUAL(weakblocksEnabled(), true);

    BOOST_CHECK(weakstore.Tip() == nullptr);

    BOOST_CHECK_EQUAL(weakstore.size(), 0);
    BOOST_CHECK(weakstore.empty());
    weakstore.consistencyCheck();
    weakstore.expireOld();
    weakstore.consistencyCheck();
}

// helper function to create coinbase txn with prev-weak-block-pointers
static CTransactionRef weakblockCB(uint256 weakref,
                            char size_byte = 0x22,
                            char marker1 = 'W',
                            char marker2 = 'B') {
    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vout.resize(2);
    cb.vout[0].scriptPubKey = CScript();
    cb.vout[0].nValue = 100000000;
    uint64_t pseudoHeight = 100000;

    cb.vin[0].scriptSig = CScript() << pseudoHeight << OP_0;

    cb.vout[1].nValue = 0;
    cb.vout[1].scriptPubKey = CScript() << OP_RETURN;
    cb.vout[1].scriptPubKey.push_back(size_byte); // size byte
    cb.vout[1].scriptPubKey.push_back(marker1); // marker
    cb.vout[1].scriptPubKey.push_back(marker2);
    cb.vout[1].scriptPubKey.insert(cb.vout[1].scriptPubKey.end(),
                                   weakref.begin(),
                                   weakref.end());

    return make_shared<CTransaction>(cb);
}

static CBlockRef weakextendBlock(const CBlock *underlying,
                                 size_t ntx) {
    CBlockRef res;
    assert (ntx>0);

    size_t otx =0;
    if (underlying != nullptr) {
        res = make_shared<CBlock>(*underlying);
        otx = underlying->vtx.size();
        assert(otx<=ntx);
    }
    else {
        res = make_shared<CBlock>();
    }
    res->vtx.resize(ntx);
    res->vtx[0] = weakblockCB(underlying->GetHash());

    for (size_t i=otx > 0 ? otx : 1; i<ntx; i++) {
        CMutableTransaction tx;
        RandomTransaction(tx, false);
        res->vtx[i] = make_shared<CTransaction>(tx);
    }
    res->hashMerkleRoot = BlockMerkleRoot(*res);
    return res;
}

// test weakblocksExtractCommitment
BOOST_AUTO_TEST_CASE(extract_commitment)
{
    BOOST_CHECK(null.IsNull());
    BOOST_CHECK_EQUAL(weakblocksExtractCommitment(nullptr), null);

    CBlock b0;
    BOOST_CHECK_EQUAL(weakblocksExtractCommitment(&b0),  null);
    BOOST_CHECK(b0.GetHash() != null);

    CBlockRef b1 = weakextendBlock(&b0, 100);
    BOOST_CHECK_EQUAL(weakblocksExtractCommitment(b1.get()), b0.GetHash());
}

// test empty weak block
BOOST_AUTO_TEST_CASE(construct_empty)
{
    CBlock b0;
    CWeakblock wb(&b0);
    LOCK(cs_weakblocks);
    BOOST_CHECK_EQUAL(wb.GetWeakHeight(), 0);
    BOOST_CHECK_EQUAL(wb.GetWeakHeight(), 0); // using cached value
}


static void scenario1() {

    CBlock b0;
    CBlockRef b1 = weakextendBlock(&b0, 100);

    CWeakblockRef wb0, wb1, wb2;
    BOOST_CHECK(weakstore.byHash(b0.GetHash()) == nullptr);
    BOOST_CHECK(weakstore.byHash(b0.GetHash().GetCheapHash()) == nullptr);
    BOOST_CHECK(nullptr != (wb0 = weakstore.store(&b0)));
    BOOST_CHECK_EQUAL(weakstore.Tip()->GetHash(), b0.GetHash());
    BOOST_CHECK(weakstore.byHash(b0.GetHash().GetCheapHash()) == wb0);
    BOOST_CHECK_EQUAL(weakstore.size(), 1);
    BOOST_CHECK(!weakstore.empty());

    BOOST_CHECK(nullptr != (wb1 = weakstore.store(b1.get())));
    BOOST_CHECK_EQUAL(weakstore.Tip()->GetHash(), b1->GetHash());
    BOOST_CHECK_EQUAL(weakstore.size(), 2);

    CBlockRef b2 = weakextendBlock(b1.get(), 200);
    BOOST_CHECK(nullptr != (wb2 = weakstore.store(b2.get())));
    BOOST_CHECK_EQUAL(weakstore.Tip()->GetHash(), b2->GetHash());
    BOOST_CHECK_EQUAL(weakstore.size(), 3);

    BOOST_CHECK_EQUAL(wb0, weakstore.byHash(b0.GetHash()));
    BOOST_CHECK_EQUAL(wb1, weakstore.byHash(b1->GetHash()));
    BOOST_CHECK_EQUAL(wb2, weakstore.byHash(b2->GetHash()));

    BOOST_CHECK(wb1->extends(b0));
    BOOST_CHECK(wb1->extends(wb0));
    BOOST_CHECK(wb2->extends(b1));
    BOOST_CHECK(wb2->extends(wb1));
    BOOST_CHECK(wb2->extends(b0));

    BOOST_CHECK_EQUAL(wb0->GetWeakHeight(), 0);
    BOOST_CHECK_EQUAL(wb1->GetWeakHeight(), 1);
    BOOST_CHECK_EQUAL(wb2->GetWeakHeight(), 2);

    // now overtake with a second chain starting at wb1

    CWeakblockRef wb1_1 = weakstore.store(weakextendBlock(b1.get(), 300).get());
    BOOST_CHECK_EQUAL(weakstore.Tip()->GetHash(), b2->GetHash());
    BOOST_CHECK_EQUAL(weakstore.chainTips().size(), 2);
    BOOST_CHECK_EQUAL(weakstore.Tip()->GetWeakHeight(), 2);

    CWeakblockRef wb1_2 = weakstore.store(weakextendBlock(wb1_1.get(), 300).get());
    BOOST_CHECK_EQUAL(weakstore.Tip()->GetHash(), wb1_2->GetHash());
    BOOST_CHECK_EQUAL(weakstore.chainTips().size(), 2);
    BOOST_CHECK_EQUAL(weakstore.Tip()->GetWeakHeight(), 3);
}

static void scenario2() {

    CBlock b0_2;

    CMutableTransaction tx;
    RandomTransaction(tx, false);
    b0_2.vtx.push_back(make_shared<CTransaction>(tx));
    b0_2.hashMerkleRoot = BlockMerkleRoot(b0_2);

    CWeakblockRef wb0_2, wb1_2, wb2_2, wb3_2, wb4_2, wb5_2;

    BOOST_CHECK(nullptr != (wb0_2 = weakstore.store(&b0_2)));
    BOOST_CHECK(weakstore.Tip() != wb0_2);

    BOOST_CHECK(weakstore.Tip()->GetWeakHeight() == 3);

    wb1_2 = weakstore.store(weakextendBlock(wb0_2.get(), 1000).get());
    BOOST_CHECK(wb1_2 != nullptr);
    BOOST_CHECK(weakstore.Tip() != wb1_2);
    BOOST_CHECK(weakstore.Tip()->GetWeakHeight() == 3);

    wb2_2 = weakstore.store(weakextendBlock(wb1_2.get(), 2000).get());
    BOOST_CHECK(weakstore.Tip() != wb2_2);
    BOOST_CHECK(wb2_2 != nullptr);
    BOOST_CHECK(weakstore.Tip()->GetWeakHeight() == 3);

    wb3_2 = weakstore.store(weakextendBlock(wb2_2.get(), 3000).get());
    BOOST_CHECK(wb3_2 != nullptr);
    BOOST_CHECK(weakstore.Tip()->GetWeakHeight() == 3);

    wb4_2 = weakstore.store(weakextendBlock(wb3_2.get(), 4000).get());
    BOOST_CHECK(wb4_2 != nullptr);
    BOOST_CHECK_EQUAL(weakstore.Tip(), wb4_2);
    BOOST_CHECK(weakstore.Tip()->GetWeakHeight() == 4);

    wb5_2 = weakstore.store(weakextendBlock(wb4_2.get(), 5000).get());
    BOOST_CHECK(wb5_2 != nullptr);
    BOOST_CHECK_EQUAL(weakstore.Tip(), wb5_2);
    BOOST_CHECK(weakstore.Tip()->GetWeakHeight() == 5);
    BOOST_CHECK_EQUAL(weakstore.Tip(), wb5_2);
}

BOOST_AUTO_TEST_CASE(weak_chain1)
{
    CBlock b0;
    LOCK(cs_weakblocks);

    // mark all for expiry
    weakstore.expireOld();
    // and check that all are at height -1
    for (auto wb : weakstore.chainTips())
        BOOST_CHECK_EQUAL(wb->GetWeakHeight(), -1);

    // and throw all stuff away this time
    weakstore.expireOld();
    BOOST_CHECK_EQUAL(weakstore.chainTips().size(), 0);
    BOOST_CHECK(weakstore.empty());

    // recreate scenario1 to overtake just once more with a wholly new chain
    scenario1();

    BOOST_CHECK_EQUAL(weakstore.chainTips().size(), 2);
    scenario2();
    BOOST_CHECK_EQUAL(weakstore.chainTips().size(), 3);

    weakstore.expireOld();
    BOOST_CHECK_EQUAL(weakstore.chainTips().size(), 3);
    // 3 tips, but all marked with a chain height of -1 now
    BOOST_CHECK(weakstore.Tip() == nullptr);

    weakstore.expireOld();
    BOOST_CHECK_EQUAL(weakstore.chainTips().size(), 0);
}

BOOST_AUTO_TEST_CASE(weak_chain_order)
{
    for (size_t dag_size = 0 ; dag_size < 20; dag_size++) {
        // test that a randomly constructed weak blocks DAG will be rebuild to
        // the same result when arrival happens in random order.
        std::vector<CBlockRef> blocks;

        LOG(WB, "Checking weak chain reconstruction order for a DAG with size: %d\n", dag_size);
        weakstore.consistencyCheck();
        weakstore.expireOld();
        weakstore.consistencyCheck();
        weakstore.expireOld(true);
        BOOST_CHECK(weakstore.empty());

        // build a random DAG. This is certainly biased in all kinds
        // of ways, but hopefully all potential edge cases are all
        // still properly explored. Also, size might be smaller than
        // targeted.
        for (size_t d=0; d < dag_size; d++) {
            switch (insecure_rand()%2) {
            case 0:
            {
                // a new root
                CBlockRef block = make_shared<CBlock>();
                for (size_t i=0 ; i < 50; i++) {
                    CMutableTransaction tx;
                    RandomTransaction(tx, false);
                    block->vtx.push_back(MakeTransactionRef(tx));
                }
                block->hashMerkleRoot = BlockMerkleRoot(*block);
                blocks.push_back(block);
            }
            break;
            case 1:
            {
                if (blocks.size()) {
                    CBlockRef underlying = blocks[insecure_rand() % blocks.size()];

                    // build on top of one of the existing blocks
                    CBlockRef block = weakextendBlock(underlying.get(),
                                                      underlying->vtx.size() + insecure_rand()%1000);
                    blocks.push_back(block);
                }
            }
            break;
            default:
                break;
            }
        }
        BOOST_CHECK(blocks.size() <= dag_size);
        // randomize order for initial insertion
        random_shuffle(blocks.begin(), blocks.end());

        std::vector<CWeakblockRef> weaks;

        for (CBlockRef b : blocks) {
            CWeakblockRef wb = weakstore.store(b.get());
            BOOST_CHECK(wb != nullptr);
            weaks.push_back(wb);
        }

        std::map<uint256, int> heights0;
        for (auto wb : weaks)
            heights0[wb->GetHash()] = wb->GetWeakHeight();

        std::set<uint256> tips0;
        for (auto wb : weakstore.chainTips())
            tips0.insert(wb->GetHash());

        for (size_t i=0; i < 10; i++) {
            LOG(WB, "Checking random reconstruction #%d.\n", i);
            weakstore.consistencyCheck();
            weakstore.expireOld();
            weakstore.consistencyCheck();
            weakstore.expireOld(true);
            weaks.clear();

            std::map<uint256, int> heights;
            std::set<uint256> tips;

            random_shuffle(blocks.begin(), blocks.end());
            for (CBlockRef b : blocks) {
                CWeakblockRef wb = weakstore.store(b.get());
                BOOST_CHECK(wb != nullptr);
                weaks.push_back(wb);
            }
            weakstore.consistencyCheck(false);

            for (auto wb : weaks)
                heights[wb->GetHash()] = wb->GetWeakHeight();
            for (auto wb : weakstore.chainTips())
                tips.insert(wb->GetHash());

            BOOST_CHECK(heights == heights0);
            BOOST_CHECK(tips    ==    tips0);
            weakstore.consistencyCheck();
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
