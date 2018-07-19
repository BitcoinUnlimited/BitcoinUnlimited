#include <boost/test/unit_test.hpp>
#include <cassert>
#include <iostream>

#include "hash.h"
#include "iblt.h"
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

BOOST_AUTO_TEST_CASE(iblt_handles_small_quantities)
{
	bool allPassed = true;
	for (size_t nItems=1;nItems < 100;nItems++)
	{
		CIblt t(nItems);

		for (size_t i=0;i < nItems;i++)
			t.insert(i, IBLT_NULL_VALUE);

		std::set<std::pair<uint64_t, std::vector<uint8_t> > > entries;
		allPassed = allPassed && t.listEntries(entries, entries);
	}

	BOOST_CHECK(allPassed);
}

BOOST_AUTO_TEST_CASE(iblt_erases_properly)
{
    CIblt t(20);
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

BOOST_AUTO_TEST_CASE(iblt_handles_overload)
{
    CIblt t(20);

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

BOOST_AUTO_TEST_CASE(iblt_lists_entries_properly)
{
    std::set<std::pair<uint64_t, std::vector<uint8_t> > > expected;
    CIblt t(20);
    for (int i = 0; i < 20; i++)
    {
        t.insert(i, PseudoRandomValue(i * 2));
        expected.insert(std::make_pair(i, PseudoRandomValue(i * 2)));
    }
    std::set<std::pair<uint64_t, std::vector<uint8_t> > > entries;
    bool fAllFound = t.listEntries(entries, entries);
    BOOST_CHECK(fAllFound && entries == expected);
}

BOOST_AUTO_TEST_CASE(iblt_performs_subtraction_properly)
{
    CIblt t1(11);
    CIblt t2(11);

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


    CIblt emptyIBLT(11);
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

BOOST_AUTO_TEST_SUITE_END()
