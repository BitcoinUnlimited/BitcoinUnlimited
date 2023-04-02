// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/heapoptional.h>

#include <random.h>
#include "test/test_bitcoin.h"
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(heapoptional_tests, BasicTestingSetup)

static std::vector<uint8_t> RandomData() {
    static FastRandomContext rand(true /* determinstic */);
    uint256 r = rand.rand256();
    return std::vector<uint8_t>(r.begin(), r.end());
}

BOOST_AUTO_TEST_CASE(heapoptional_test) {
    // Test basic operation
    HeapOptional<std::vector<uint8_t>> p = {}; /* check that  '= {}' uses default c'tor */
    HeapOptional<std::vector<uint8_t>> p2;
    // default constructed value should have nothing in it
    BOOST_CHECK(!p);
    BOOST_CHECK(p.get() == nullptr);
    BOOST_CHECK(p == p2); // nulls compare equal
    BOOST_CHECK(!(p != p2)); // nulls are never not equal (test operator!=)
    BOOST_CHECK(!(p < p2)); // nulls are not less than

    // assign a real value to p but not to p2
    const std::vector<uint8_t> data1 = RandomData();
    BOOST_CHECK(!p);
    p = data1;
    BOOST_CHECK(bool(p));
    // Test comparison ops ==, !=, and <
    BOOST_CHECK(*p == data1);
    BOOST_CHECK(p == data1);
    BOOST_CHECK(!(p < data1)); // operator< should return false
    BOOST_CHECK(!(p != data1));
    BOOST_CHECK(p.get() != &data1);
    BOOST_CHECK(p2 < data1); // nullptr p2 is always less than data1
    BOOST_CHECK(p2 != data1); // nullptr p2 is always not equal to data1
    BOOST_CHECK(!(p2 == data1)); // nullptr p2 is always not equal to data1 (test opeerator==)
    // decrement the last byte(s) of *p
    BOOST_CHECK(!p->empty());
    for (auto rit = p->rbegin(); rit != p->rend(); ++rit)
        if ((*rit)-- != 0) break;
    // p should now compare less
    BOOST_CHECK(p < data1);
    BOOST_CHECK(p != data1);
    BOOST_CHECK(!(p == data1)); // operator==
    BOOST_CHECK(data1 > *p);

    // assign p2 from p
    BOOST_CHECK(!p2);
    p2 = p;
    BOOST_CHECK(bool(p2));
    BOOST_CHECK(p.get() != p2.get());
    BOOST_CHECK(p == p2);
    BOOST_CHECK(!(p != p2));
    BOOST_CHECK(!(p < p2));

    // assign data1 to p2
    p2 = data1;
    BOOST_CHECK(bool(p2));
    BOOST_CHECK(p.get() != p2.get());
    BOOST_CHECK(!(p == p2));
    BOOST_CHECK(p != p2);
    BOOST_CHECK(p < p2);

    // check reset and emplace
    p.reset();
    const void *oldp2_ptr = p2.get();
    p2.emplace(data1.size(), 0x0); // assign all 0's to p2 using the emplace() method
    BOOST_CHECK(p2.get() != oldp2_ptr); // emplacing should have created a new object in a different heap location (and deleted the old)
    BOOST_CHECK(!p);
    BOOST_CHECK(!p.get());
    BOOST_CHECK(p2);
    BOOST_CHECK(p2.get());
    BOOST_CHECK(p != p2);
    BOOST_CHECK(p < p2); // p is null, should always be less than p2
    BOOST_CHECK(!(p == p2)); // operator== where p is nullptr
    BOOST_CHECK((p2 == std::vector<uint8_t>(data1.size(), 0x0)));
    BOOST_CHECK((p2 != std::vector<uint8_t>(data1.size(), 0x1)));
    BOOST_CHECK((p2 < std::vector<uint8_t>(data1.size(), 0x1)));

    p2.reset();
    BOOST_CHECK(!p2);
    BOOST_CHECK(p == p2);
    BOOST_CHECK(p.get() == nullptr && p2.get() == nullptr);

    // test construction in-place
    BOOST_CHECK(HeapOptional<std::vector<uint8_t>>(100, 0x80) == HeapOptional<std::vector<uint8_t>>(100, 0x80));
    BOOST_CHECK(HeapOptional<std::vector<uint8_t>>(100, 0x80) != HeapOptional<std::vector<uint8_t>>(100, 0x81));
    BOOST_CHECK(HeapOptional<std::vector<uint8_t>>(100, 0x80) < HeapOptional<std::vector<uint8_t>>(100, 0x81));
}

BOOST_AUTO_TEST_SUITE_END()
