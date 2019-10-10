// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <boost/test/unit_test.hpp>
#include <cassert>
#include <iostream>

#include "hashwrapper.h"
#include "iblt.h"
#include "serialize.h"
#include "test/test_bitcoin.h"
#include "utilstrencodings.h"

const std::vector<uint8_t> IBLT_NULL_VALUE = {};

std::vector<uint8_t> PseudoRandomValue(uint32_t n)
{
    std::vector<uint8_t> result;
    for (int i = 0; i < 4; i++)
    {
        result.push_back(static_cast<uint8_t>(MurmurHash3(n + i, result) & 0xff));
    }
    return result;
}

BOOST_FIXTURE_TEST_SUITE(iblt_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(iblt_variable_checksum_gives_smaller_encoding)
{
    uint64_t version = 2;
    uint32_t salt = 1;
    size_t n = 1;

    CIblt full(n, version);
    full.insert(0, IBLT_NULL_VALUE);
    size_t full_size = ::GetSerializeSize(full, SER_NETWORK, PROTOCOL_VERSION);
    CIblt masked1(n, salt, version, 0x0000ffff);
    masked1.insert(0, IBLT_NULL_VALUE);
    size_t masked1_size = ::GetSerializeSize(masked1, SER_NETWORK, PROTOCOL_VERSION);
    CIblt masked2(n, salt, version, 0x000000ff);
    masked2.insert(0, IBLT_NULL_VALUE);
    size_t masked2_size = ::GetSerializeSize(masked2, SER_NETWORK, PROTOCOL_VERSION);

    BOOST_CHECK(full_size > masked1_size);
    BOOST_CHECK(masked1_size > masked2_size);
}

BOOST_AUTO_TEST_CASE(iblt_handles_small_quantities)
{
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        bool allPassed = true;
        for (size_t nItems=1;nItems < 100;nItems++)
        {
            CIblt t(nItems, version);

            for (size_t i=0;i < nItems;i++)
                t.insert(i, IBLT_NULL_VALUE);

            std::set<std::pair<uint64_t, std::vector<uint8_t> > > entries;
            allPassed = allPassed && t.listEntries(entries, entries);
        }

        BOOST_CHECK(allPassed);
    }
}

BOOST_AUTO_TEST_CASE(iblt_arbitrary_salt)
{
    uint32_t salt = 17;
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        size_t nItems = 2;
        CIblt t(nItems, salt, version);

        t.insert(0, ParseHex("00000000"));
        t.insert(1, ParseHex("00000001"));
        bool gotResult;
        std::vector<uint8_t> result;

        gotResult = t.get(0, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000000"));

        gotResult = t.get(1, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000001"));
    }
}

BOOST_AUTO_TEST_CASE(iblt_salted_reset)
{
    size_t nHash = 1;
    uint32_t salt = 17;
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        bool gotResult;
        std::vector<uint8_t> result;
        CIblt t(nHash, salt, version);

        t.insert(0, ParseHex("00000000"));
        gotResult = t.get(0, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000000"));

        t.reset();
        t.resize(20);
        t.insert(1, ParseHex("00000001"));
        t.insert(11, ParseHex("00000011"));

        gotResult = t.get(1, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000001"));
    }
}

BOOST_AUTO_TEST_CASE(iblt_reset)
{
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        CIblt t(version);
        t.insert(0, ParseHex("00000000"));
        bool gotResult;
        std::vector<uint8_t> result;
        gotResult = t.get(21, result);
        BOOST_CHECK(!gotResult);  // anything could have been inserted into a zero length IBLT

        t.reset();
        t.resize(20);
        t.insert(0, ParseHex("00000000"));
        t.insert(1, ParseHex("00000001"));
        t.insert(11, ParseHex("00000011"));

        gotResult = t.get(0, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000000"));

        t.reset();

        gotResult = t.get(0, result);
        BOOST_CHECK(gotResult && (result.size()==0));

        t.resize(40);

        t.insert(0, ParseHex("00000000"));
        t.insert(1, ParseHex("00000001"));
        t.insert(11, ParseHex("00000011"));

        gotResult = t.get(0, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000000"));
    }
}

BOOST_AUTO_TEST_CASE(iblt_erases_properly)
{
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        CIblt t(20, version);
        t.insert(0, ParseHex("00000000"));
        t.insert(1, ParseHex("00000001"));
        t.insert(11, ParseHex("00000011"));

        bool gotResult;
        std::vector<uint8_t> result;
        gotResult = t.get(0, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000000"));
        gotResult = t.get(11, result);
        BOOST_CHECK(gotResult && result == ParseHex("00000011"));

        t.erase(0, ParseHex("00000000"));
        t.erase(1, ParseHex("00000001"));
        gotResult = t.get(1, result);
        BOOST_CHECK(gotResult && result.empty());
        t.erase(11, ParseHex("00000011"));
        gotResult = t.get(11, result);
        BOOST_CHECK(gotResult && result.empty());

        t.insert(0, ParseHex("00000000"));
        t.insert(1, ParseHex("00000001"));
        t.insert(11, ParseHex("00000011"));

        for (int i = 100; i < 115; i++)
        {
            t.insert(i, ParseHex("aabbccdd"));
        }

        gotResult = t.get(101, result);
        BOOST_CHECK(gotResult && result == ParseHex("aabbccdd"));
        gotResult = t.get(200, result);
        BOOST_CHECK(gotResult && result.empty());
    }
}

BOOST_AUTO_TEST_CASE(iblt_handles_overload)
{
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        CIblt t(20, version);

        // 1,000 values in an IBLT that has room for 20,
        // all lookups should fail.
        for (int i = 0; i < 1000; i++)
        {
            t.insert(i, PseudoRandomValue(i));
        }
        bool gotResult;
        std::vector<uint8_t> result;
        for (int i = 0; i < 1000; i += 97)
        {
            gotResult = t.get(i, result);
            BOOST_CHECK(!gotResult && result.empty());
        }

        // erase all but 20:
        for (int i = 20; i < 1000; i++)
        {
            t.erase(i, PseudoRandomValue(i));
        }
        for (int i = 0; i < 20; i++)
        {
            gotResult = t.get(i, result);
            BOOST_CHECK(gotResult && result == PseudoRandomValue(i));
        }
    }
}

BOOST_AUTO_TEST_CASE(iblt_lists_entries_properly)
{
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > expected;
        CIblt t(20, version);
        for (int i = 0; i < 20; i++)
        {
            t.insert(i, PseudoRandomValue(i * 2));
            expected.insert(std::make_pair(i, PseudoRandomValue(i * 2)));
        }
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > entries;
        bool fAllFound = t.listEntries(entries, entries);
        BOOST_CHECK(fAllFound && entries == expected);
    }
}

BOOST_AUTO_TEST_CASE(iblt_performs_subtraction_properly)
{
    uint64_t versions[2] = {1, 2}; 
    for (uint64_t version : versions)
    {
        CIblt t1(11, version);
        CIblt t2(11, version);

        for (int i = 0; i < 195; i++)
        {
            t1.insert(i, PseudoRandomValue(i));
        }
        for (int i = 5; i < 200; i++)
        {
            t2.insert(i, PseudoRandomValue(i));
        }

        CIblt diff = t1 - t2;

        // Should end up with 10 differences, 5 positive and 5 negative:
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > expectedPositive;
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > expectedNegative;
        for (int i = 0; i < 5; i++)
        {
            expectedPositive.insert(std::make_pair(i, PseudoRandomValue(i)));
            expectedNegative.insert(std::make_pair(195 + i, PseudoRandomValue(195 + i)));
        }
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > positive;
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > negative;
        bool allDecoded = diff.listEntries(positive, negative);
        BOOST_CHECK(allDecoded);
        BOOST_CHECK(positive == expectedPositive);
        BOOST_CHECK(negative == expectedNegative);

        positive.clear();
        negative.clear();
        allDecoded = (t2 - t1).listEntries(positive, negative);
        BOOST_CHECK(allDecoded);
        BOOST_CHECK(positive == expectedNegative); // Opposite subtraction, opposite results
        BOOST_CHECK(negative == expectedPositive);


        CIblt emptyIBLT(11, version);
        std::set<std::pair<uint64_t, std::vector<uint8_t> > > emptySet;

        // Test edge cases for empty IBLT:
        allDecoded = emptyIBLT.listEntries(emptySet, emptySet);
        BOOST_CHECK(allDecoded);
        BOOST_CHECK(emptySet.empty());

        positive.clear();
        negative.clear();
        allDecoded = (diff - emptyIBLT).listEntries(positive, negative);
        BOOST_CHECK(allDecoded);
        BOOST_CHECK(positive == expectedPositive);
        BOOST_CHECK(negative == expectedNegative);

        positive.clear();
        negative.clear();
        allDecoded = (emptyIBLT - diff).listEntries(positive, negative);
        BOOST_CHECK(allDecoded);
        BOOST_CHECK(positive == expectedNegative); // Opposite subtraction, opposite results
        BOOST_CHECK(negative == expectedPositive);
    }
}

BOOST_AUTO_TEST_SUITE_END()
