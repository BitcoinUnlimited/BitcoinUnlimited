#include <boost/test/unit_test.hpp>
#include "blockorder.h"
#include "test/testutil.h"
#include "test/test_bitcoin.h"

BOOST_FIXTURE_TEST_SUITE(blockorder_tests, BasicTestingSetup)

using namespace BlockOrder;

inline void printTXRFV(const CTxRefVector &v) {
    for (int i = 0; i < v.size(); i++) {
        auto&tx = v[i];
        printf("%d %s %s\n", i, tx->GetHash().GetHex().c_str(), tx->ToString().c_str());
    }
}

/*! Check that the second part of transactions that are not being consumed in
a block (no other transaction *within the block* depending on them) are
properly sorted according to the TXIDCompare comparator function.

Returns: negative position if the condition is not fulfilled at that position
other, returns the position of the last matching transaction

*/
int checkLastAreLexical(const CTxRefVector& txrfv) {
    std::set<uint256> deps;
    TXIDCompare compare;

    // > 1 -> skip coinbase
    for (int i=txrfv.size()-1; i > 1; i--) {
        for (auto& input : txrfv[i-1]->vin)
            deps.insert(input.prevout.hash);

        if (!compare(txrfv[i-1], txrfv[i]) &&
            !deps.count(txrfv[i-1]->GetHash()))
                return -i;
    }
    return 1;
}


void checkForFraction(double f)
{
    //  Create random block with some interdependent transactions
    CBlockRef block0 = RandomBlock(1000, f);

    BOOST_CHECK(block0->vtx[0]->IsCoinBase());
    BOOST_CHECK(isTopological(block0->vtx));

    TopoCanonical tc;
    tc.prepare(block0->vtx);

    // Copy out transaction list and sort it once
    CTxRefVector ref(block0->vtx);
    CTxRefVector test(block0->vtx);

    tc.sort(ref);

    BOOST_CHECK(isTopological(ref));

    // Now do some random shuffles on test, resort, and test for identity
    for (size_t i = 0; i < 100; i++) {
        random_shuffle(test.begin()+1, test.end());
        tc.sort(test);
        BOOST_CHECK(test == ref);
        if (test != ref) {
            printf("REF:\n");
            printTXRFV(ref);
            printf("TEST:\n");
            printTXRFV(test);

            for (auto& tx : test)
                printf("%s %s\n", tx->GetHash().GetHex().c_str(), tx->ToString().c_str());
        }
        BOOST_CHECK(test[0]->IsCoinBase());
        for (size_t j=1; j < test.size(); j++)
            BOOST_CHECK(!test[j]->IsCoinBase());
        BOOST_CHECK(isTopological(test));
        int last_are_lexical = checkLastAreLexical(test);
        BOOST_CHECK(last_are_lexical);
        if (last_are_lexical < 0) {
            printf("pos: %d\n", last_are_lexical);
            printf("REF:\n"); printTXRFV(ref);
            printf("TEST:\n"); printTXRFV(test);
        }
        if (f == 0.0)
            BOOST_CHECK(last_are_lexical == 1);
    }
}

BOOST_AUTO_TEST_CASE(blockorder_topocanonical_stable_and_topological)
{
    checkForFraction(0.0);
    checkForFraction(0.1);
}

BOOST_AUTO_TEST_SUITE_END()
