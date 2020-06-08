#include "bobtail/bobtail.h"
#include "bobtail/dag.h"
#include "bobtail/subblock.h"
#include "test/test_bitcoin.h"
#include <boost/math/distributions/gamma.hpp>
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

BOOST_AUTO_TEST_CASE(arith_uint256_sanity)
{
    unsigned int nBits = 545259519;
    arith_uint256 a;
    a.SetCompact(nBits);
    arith_uint256 b;
    b.SetCompact(nBits);
    b /= 1000;
    arith_uint256 c;
    a.SetCompact(nBits);
    c = ~c;
    c *= 1000;
    c = ~c;

    BOOST_CHECK(a > b);
    BOOST_CHECK(a > c);
}

BOOST_AUTO_TEST_CASE(gamma_sanity_check)
{
    // The median of the exponential distribution with mean 1 should be ln(2)
    boost::math::gamma_distribution<> expon(1,1);
    BOOST_CHECK(quantile(expon, 0.5) == std::log(2));

    // The quantile of the density of a gamma at its mean should be equal to k*scale_parameter 
    uint8_t k = 3;
    arith_uint256 scale = arith_uint256(1e6);
    boost::math::gamma_distribution<> bobtail_gamma(k, scale.getdouble());
    BOOST_CHECK(quantile(bobtail_gamma, cdf(bobtail_gamma, mean(bobtail_gamma))) == k*scale.getdouble());
}

BOOST_AUTO_TEST_CASE(test_kos_threshold)
{
    uint8_t k = 3;
    arith_uint256 target(1e6);

    double thresh = GetKOSThreshold(target, k);
    // Threshold should be larger than mean
    BOOST_CHECK(thresh > target.getdouble()*k);
}

BOOST_AUTO_TEST_SUITE_END()
