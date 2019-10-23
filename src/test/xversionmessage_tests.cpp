// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "xversionmessage.h"
#include "serialize.h"
#include "streams.h"
#include "test/test_bitcoin.h"

#include <stdint.h>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(xversionmessage_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(decode1)
{
    std::vector<uint8_t> v;

    std::vector<uint8_t> xmap1 = ParseHex(" 05" // number of entries
                                          " 11   01 22"
                                          " 33   01 44"
                                          " 55   01 66"
                                          " 66   00" // empty (and not a compact integer)
                                          " 67   02 ff ff" // not a compact-size integer
        );
    // clang-format on

    v.insert(v.end(), xmap1.begin(), xmap1.end());

    CXVersionMessage xver;

    CDataStream stream(v, SER_NETWORK, PROTOCOL_VERSION);
    stream.SetVersion(0);

    stream >> xver;

    BOOST_CHECK_EQUAL(xver.xmap.size(), 5);
    BOOST_CHECK(xver.xmap[0x11] == std::vector<uint8_t>(1, 0x22));
    BOOST_CHECK(xver.xmap[0x66] == std::vector<uint8_t>());
    BOOST_CHECK(xver.xmap[0x67] == std::vector<uint8_t>(2, 0xff));
    BOOST_CHECK_EQUAL(xver.as_u64c(0x11), 0x22);
    BOOST_CHECK_EQUAL(xver.as_u64c(0x33), 0x44);
    BOOST_CHECK_EQUAL(xver.as_u64c(0x55), 0x66);
    BOOST_CHECK_EQUAL(xver.as_u64c(0x66), 0x00); // compact size decode failure
    BOOST_CHECK_EQUAL(xver.as_u64c(0x67), 0x00); // compact size decode failure
    BOOST_CHECK_EQUAL(xver.as_u64c(0x77), 0x00); // not in map

    BOOST_CHECK_EQUAL(xmap1.size(), GetSerializeSize(REF(CompactMapSerialization(xver.xmap)), SER_NETWORK, PROTOCOL_VERSION));
}

BOOST_AUTO_TEST_SUITE_END()
