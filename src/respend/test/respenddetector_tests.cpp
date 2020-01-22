// Copyright (c) 2018 The Bitcoin developers
// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "respend/respenddetector.h"
#include "primitives/transaction.h"
#include "respend/respendaction.h"
#include "test/test_bitcoin.h"
#include "test/testutil.h"
#include "txmempool.h"

#include <boost/test/unit_test.hpp>

using namespace respend;

namespace
{
class DummyRespendAction : public RespendAction
{
public:
    DummyRespendAction()
        : addOutpointCalls(0), respentBefore(false), isEquivalent(false), triggered(false), returnInteresting(false),
          valid(false)
    {
    }

    bool AddOutpointConflict(const COutPoint &out,
        const CTxMemPool::txiter mempoolEntry,
        const CTransactionRef respendTx,
        bool fRespentBefore,
        bool fIsEquivalent) override
    {
        addOutpointCalls++;
        this->respentBefore = fRespentBefore;
        this->isEquivalent = fIsEquivalent;
        return false;
    }

    bool IsInteresting() const override { return returnInteresting; }
    void SetValid(bool v) override { valid = v; }
    void Trigger() override { triggered = true; }
    int addOutpointCalls;
    bool respentBefore;
    bool isEquivalent;
    bool triggered;
    bool returnInteresting;
    bool valid;
};

class RespendFixture : public BasicTestingSetup
{
public:
    RespendFixture() : mempool(CFeeRate(0)), dummyaction(new DummyRespendAction) {}
    CTxMemPool mempool;
    std::shared_ptr<DummyRespendAction> dummyaction;
};

} // ns anon

BOOST_FIXTURE_TEST_SUITE(respenddetector_tests, RespendFixture);

BOOST_AUTO_TEST_CASE(not_a_respend)
{
    CMutableTransaction tx1 = CreateRandomTx();
    CMutableTransaction tx2 = CreateRandomTx();

    // Nothing in mempool, can't be a respend.
    {
        RespendDetector detector(mempool, MakeTransactionRef(tx1), {dummyaction});
        BOOST_CHECK(!detector.IsRespend());
        BOOST_CHECK_EQUAL(0, dummyaction->addOutpointCalls);
    }

    TestMemPoolEntryHelper entry;
    mempool.addUnchecked(tx1.GetHash(), entry.FromTx(tx1));

    // tx2 is not a respend of tx1
    RespendDetector detector(mempool, MakeTransactionRef(tx2), {dummyaction});
    BOOST_CHECK(!detector.IsRespend());
    BOOST_CHECK_EQUAL(0, dummyaction->addOutpointCalls);
}

BOOST_AUTO_TEST_CASE(only_script_differs)
{
    CMutableTransaction tx1 = CreateRandomTx();
    CMutableTransaction tx2 = tx1;
    tx2.vin[0].scriptSig << OP_DROP << OP_1;

    TestMemPoolEntryHelper entry;
    mempool.addUnchecked(tx1.GetHash(), entry.FromTx(tx1));
    RespendDetector detector(mempool, MakeTransactionRef(tx2), {dummyaction});
    BOOST_CHECK(detector.IsRespend());
    // when only the script differs, the isEquivalent flag should be set
    BOOST_CHECK(dummyaction->isEquivalent);
    BOOST_CHECK(!dummyaction->respentBefore);
}

BOOST_AUTO_TEST_CASE(seen_before)
{
    CMutableTransaction tx1 = CreateRandomTx();
    CMutableTransaction tx2 = tx1;
    tx2.vout[0].scriptPubKey = CreateRandomTx().vout[0].scriptPubKey;

    TestMemPoolEntryHelper entry;
    mempool.addUnchecked(tx1.GetHash(), entry.FromTx(tx1));

    {
        RespendDetector detector(mempool, MakeTransactionRef(tx2), {dummyaction});
        BOOST_CHECK(detector.IsRespend());
        BOOST_CHECK(!dummyaction->isEquivalent);
        BOOST_CHECK(!dummyaction->respentBefore);

        // only valid txs are added to the seen before filter
        detector.SetValid(true);
    }

    // tx3 differs from tx2, but spends the same input
    CMutableTransaction tx3 = tx1;
    tx3.vout[0].scriptPubKey = CreateRandomTx().vout[0].scriptPubKey;
    {
        RespendDetector detector(mempool, MakeTransactionRef(tx3), {dummyaction});
        BOOST_CHECK(detector.IsRespend());
        BOOST_CHECK(!dummyaction->isEquivalent);
        BOOST_CHECK(dummyaction->respentBefore);
    }
}

BOOST_AUTO_TEST_CASE(triggers_actions)
{
    // Actions should trigger when RespendDetector goes out of scope.
    {
        RespendDetector detector(mempool, MakeTransactionRef(CTransaction{}), {dummyaction});
        BOOST_CHECK(!dummyaction->triggered);
    }
    BOOST_CHECK(dummyaction->triggered);
}

BOOST_AUTO_TEST_CASE(is_interesting)
{
    // Respend is interesting when at least one action finds it interesting.
    auto action1 = new DummyRespendAction;
    auto action2 = new DummyRespendAction;
    RespendDetector detector(
        mempool, MakeTransactionRef(CTransaction{}), {RespendActionPtr(action1), RespendActionPtr(action2)});

    action1->returnInteresting = false;
    action2->returnInteresting = false;
    BOOST_CHECK(!detector.IsInteresting());

    action2->returnInteresting = true;
    BOOST_CHECK(detector.IsInteresting());
}

BOOST_AUTO_TEST_CASE(set_valid)
{
    auto action1 = new DummyRespendAction;
    auto action2 = new DummyRespendAction;
    RespendDetector detector(
        mempool, MakeTransactionRef(CTransaction{}), {RespendActionPtr(action1), RespendActionPtr(action2)});

    detector.SetValid(true);
    BOOST_CHECK(action1->valid);
    BOOST_CHECK(action2->valid);

    detector.SetValid(false);
    BOOST_CHECK(!action1->valid);
    BOOST_CHECK(!action2->valid);
}

BOOST_AUTO_TEST_SUITE_END();
