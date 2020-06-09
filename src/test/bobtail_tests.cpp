#include "bobtail/dag.h"
#include "bobtail/subblock.h"
#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bobtail_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(test_dag_temporal_sort)
{
    CBobtailDagSet forest;
    CSubBlock subblock1;
    CSubBlock subblock2;
    forest.Insert(subblock1);
    forest.Insert(subblock2);
    forest.TemporalSort();

    BOOST_CHECK(forest.IsTemporallySorted());
}

BOOST_AUTO_TEST_SUITE_END()
