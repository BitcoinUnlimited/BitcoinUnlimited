// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"

#include "clientversion.h"
#include "consensus/consensus.h"
#include "primitives/transaction.h"
#include "reverse_iterator.h"
#include "sync.h"
#include "test/test_bitcoin.h"
#include "unlimited.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "xversionmessage.h"

#include <stdint.h>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(util_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(util_criticalsection)
{
    CCriticalSection test_cs;

    do
    {
        LOCK(test_cs);
        break;

        BOOST_ERROR("break was swallowed!");
    } while (0);

    do
    {
        TRY_LOCK(test_cs, lockTest);
        if (lockTest)
            break;

        BOOST_ERROR("break was swallowed!");
    } while (0);
}


static volatile int critVal = 0;
static volatile int readVal = 0;
static volatile bool threadExited = false;
static volatile bool threadStarted = false;
void ThreadSharedCritTest(CSharedCriticalSection *cs)
{
    threadStarted = true;
    READLOCK(*cs);
    readVal = critVal;

    threadExited = true;
}

BOOST_AUTO_TEST_CASE(util_sharedcriticalsection)
{
    CSharedCriticalSection test_cs;

    do
    {
        READLOCK(test_cs);
        break;

        BOOST_ERROR("break was swallowed!");
    } while (0);

    do
    {
        WRITELOCK(test_cs);
        break;

        BOOST_ERROR("break was swallowed!");
    } while (0);

    { // If the read lock does not allow simultaneous locking, this code will hang in the join_all
        boost::thread_group thrds;
        READLOCK(test_cs);
        thrds.create_thread(boost::bind(ThreadSharedCritTest, &test_cs));
        thrds.join_all();
    }

    { // Ensure that the exclusive lock works
        threadStarted = false;
        threadExited = false;
        readVal = 0;
        critVal = 1;
        boost::thread_group thrds;
        {
            WRITELOCK(test_cs);
            thrds.create_thread(boost::bind(ThreadSharedCritTest, &test_cs));
            MilliSleep(250); // give thread a chance to run.
            BOOST_CHECK(threadStarted == true);
            BOOST_CHECK(threadExited == false);
            critVal = 2;
        }
        // Now the write lock is released so the thread should read the value.
        thrds.join_all();
        BOOST_CHECK(threadExited == true);
        BOOST_CHECK(readVal == 2);
    }
}


void ThreadCorralTest(CThreadCorral *c, int region, int *pReadVal, int setVal)
{
    CORRAL(*c, region);
    *pReadVal = critVal;
    if (setVal != 0)
        critVal = setVal;
}


BOOST_AUTO_TEST_CASE(util_threadcorral)
{
    CThreadCorral corral;

    { // ensure that regions lock out other regions, but not the current region.
        boost::thread_group thrds;
        int readVals[3] = {0, 0, 0};
        {
            CORRAL(corral, 1);
            critVal = 1;
            thrds.create_thread(boost::bind(ThreadCorralTest, &corral, 0, &readVals[0], 4));
            thrds.create_thread(boost::bind(ThreadCorralTest, &corral, 1, &readVals[1], 0));
            MilliSleep(500); // Thread 1 should run now because there is no higher region waiting.
            thrds.create_thread(boost::bind(ThreadCorralTest, &corral, 2, &readVals[2], 3));
            MilliSleep(500); // give threads a chance to run (if they are going to).
            critVal = 2;
        }
        MilliSleep(1000); // give threads a chance to run (if they are going to).
        BOOST_CHECK(readVals[1] == 1); // since region 1 was active, thread 1 should have run right away
        BOOST_CHECK(readVals[2] == 2); // After release, region 2 should have run since its higher priority
        BOOST_CHECK(readVals[0] == 3); // Finally, region 0 should have run (and gotten the value set by region 2)
    }
}


static const unsigned char ParseHex_expected[65] = {0x04, 0x67, 0x8a, 0xfd, 0xb0, 0xfe, 0x55, 0x48, 0x27, 0x19, 0x67,
    0xf1, 0xa6, 0x71, 0x30, 0xb7, 0x10, 0x5c, 0xd6, 0xa8, 0x28, 0xe0, 0x39, 0x09, 0xa6, 0x79, 0x62, 0xe0, 0xea, 0x1f,
    0x61, 0xde, 0xb6, 0x49, 0xf6, 0xbc, 0x3f, 0x4c, 0xef, 0x38, 0xc4, 0xf3, 0x55, 0x04, 0xe5, 0x1e, 0xc1, 0x12, 0xde,
    0x5c, 0x38, 0x4d, 0xf7, 0xba, 0x0b, 0x8d, 0x57, 0x8a, 0x4c, 0x70, 0x2b, 0x6b, 0xf1, 0x1d, 0x5f};

BOOST_AUTO_TEST_CASE(util_DbgAssert)
{
#ifndef DEBUG_ASSERTION
    int i = 0;
    bool savedVal = fPrintToConsole;
    fPrintToConsole = true;
    DbgAssert(1, i = 1);
    BOOST_CHECK(i == 0);

    // prevent debug output for this failing (on purpose) DbgAssert
    bool fPrintToConsole_bak = fPrintToConsole;
    bool fPrintToDebugLog_bak = fPrintToDebugLog;
    fPrintToConsole = fPrintToDebugLog = false;

    DbgAssert(0, i = 1);

    fPrintToDebugLog = fPrintToDebugLog_bak;
    fPrintToConsole = fPrintToConsole_bak;

    BOOST_CHECK(i == 1);
    fPrintToConsole = savedVal;
#endif
}

BOOST_AUTO_TEST_CASE(util_ParseHex)
{
    std::vector<unsigned char> result;
    std::vector<unsigned char> expected(ParseHex_expected, ParseHex_expected + sizeof(ParseHex_expected));
    // Basic test vector
    result = ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                      "de5c384df7ba0b8d578a4c702b6bf11d5f");
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());

    // Spaces between bytes must be supported
    result = ParseHex("12 34 56 78");
    BOOST_CHECK(result.size() == 4 && result[0] == 0x12 && result[1] == 0x34 && result[2] == 0x56 && result[3] == 0x78);

    // Stop parsing at invalid value
    result = ParseHex("1234 invalid 1234");
    BOOST_CHECK(result.size() == 2 && result[0] == 0x12 && result[1] == 0x34);
}

BOOST_AUTO_TEST_CASE(util_HexStr)
{
    BOOST_CHECK_EQUAL(HexStr(ParseHex_expected, ParseHex_expected + sizeof(ParseHex_expected)),
        "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b"
        "8d578a4c702b6bf11d5f");

    BOOST_CHECK_EQUAL(HexStr(ParseHex_expected, ParseHex_expected + 5, true), "04 67 8a fd b0");

    BOOST_CHECK_EQUAL(HexStr(ParseHex_expected, ParseHex_expected, true), "");

    std::vector<unsigned char> ParseHex_vec(ParseHex_expected, ParseHex_expected + 5);

    BOOST_CHECK_EQUAL(HexStr(ParseHex_vec, true), "04 67 8a fd b0");
}


BOOST_AUTO_TEST_CASE(util_DateTimeStrFormat)
{
    BOOST_CHECK_EQUAL(DateTimeStrFormat("%Y-%m-%d %H:%M:%S", 0), "1970-01-01 00:00:00");
    BOOST_CHECK_EQUAL(DateTimeStrFormat("%Y-%m-%d %H:%M:%S", 0x7FFFFFFF), "2038-01-19 03:14:07");
    BOOST_CHECK_EQUAL(DateTimeStrFormat("%Y-%m-%d %H:%M:%S", 1317425777), "2011-09-30 23:36:17");
    BOOST_CHECK_EQUAL(DateTimeStrFormat("%Y-%m-%d %H:%M", 1317425777), "2011-09-30 23:36");
    BOOST_CHECK_EQUAL(DateTimeStrFormat("%a, %d %b %Y %H:%M:%S +0000", 1317425777), "Fri, 30 Sep 2011 23:36:17 +0000");
}

BOOST_AUTO_TEST_CASE(util_ParseParameters)
{
    const char *argv_test[] = {
        "-ignored", "-reindex", "-txindex", "-connect=argument", "-connect=multiple", "f", "-d=e"};

    ParseParameters(0, (char **)argv_test, AllowedArgs::Bitcoind());
    BOOST_CHECK(mapArgs.empty() && mapMultiArgs.empty());

    ParseParameters(1, (char **)argv_test, AllowedArgs::Bitcoind());
    BOOST_CHECK(mapArgs.empty() && mapMultiArgs.empty());

    ParseParameters(5, (char **)argv_test, AllowedArgs::Bitcoind());
    // expectation: -ignored is ignored (program name argument),
    // -reindex, -txindex and -connect end up in map, -d ignored because it is after
    // a non-option argument (non-GNU option parsing)
    BOOST_CHECK(mapArgs.size() == 3 && mapMultiArgs.size() == 3);
    BOOST_CHECK(mapArgs.count("-reindex") && mapArgs.count("-txindex") && mapArgs.count("-connect") &&
                !mapArgs.count("f") && !mapArgs.count("-d"));
    BOOST_CHECK(mapMultiArgs.count("-reindex") && mapMultiArgs.count("-txindex") && mapMultiArgs.count("-connect") &&
                !mapMultiArgs.count("f") && !mapMultiArgs.count("-d"));

    BOOST_CHECK(mapArgs["-reindex"] == "" && mapArgs["-connect"] == "multiple");
    BOOST_CHECK(mapMultiArgs["-connect"].size() == 2);
}

BOOST_AUTO_TEST_CASE(util_GetArg)
{
    mapArgs.clear();
    mapArgs["strtest1"] = "string...";
    // strtest2 undefined on purpose
    mapArgs["inttest1"] = "12345";
    mapArgs["inttest2"] = "81985529216486895";
    // inttest3 undefined on purpose
    mapArgs["booltest1"] = "";
    // booltest2 undefined on purpose
    mapArgs["booltest3"] = "0";
    mapArgs["booltest4"] = "1";

    BOOST_CHECK_EQUAL(GetArg("strtest1", "default"), "string...");
    BOOST_CHECK_EQUAL(GetArg("strtest2", "default"), "default");
    BOOST_CHECK_EQUAL(GetArg("inttest1", -1), 12345);
    BOOST_CHECK_EQUAL(GetArg("inttest2", -1), 81985529216486895LL);
    BOOST_CHECK_EQUAL(GetArg("inttest3", -1), -1);
    BOOST_CHECK_EQUAL(GetBoolArg("booltest1", false), true);
    BOOST_CHECK_EQUAL(GetBoolArg("booltest2", false), false);
    BOOST_CHECK_EQUAL(GetBoolArg("booltest3", false), false);
    BOOST_CHECK_EQUAL(GetBoolArg("booltest4", false), true);
}

BOOST_AUTO_TEST_CASE(util_FormatMoney)
{
    BOOST_CHECK_EQUAL(FormatMoney(0), "0.00");
    BOOST_CHECK_EQUAL(FormatMoney((COIN / 10000) * 123456789), "12345.6789");
    BOOST_CHECK_EQUAL(FormatMoney(-COIN), "-1.00");

    BOOST_CHECK_EQUAL(FormatMoney(COIN * 100000000), "100000000.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN * 10000000), "10000000.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN * 1000000), "1000000.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN * 100000), "100000.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN * 10000), "10000.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN * 1000), "1000.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN * 100), "100.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN * 10), "10.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN), "1.00");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 10), "0.10");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 100), "0.01");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 1000), "0.001");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 10000), "0.0001");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 100000), "0.00001");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 1000000), "0.000001");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 10000000), "0.0000001");
    BOOST_CHECK_EQUAL(FormatMoney(COIN / 100000000), "0.00000001");
}

BOOST_AUTO_TEST_CASE(util_ParseMoney)
{
    CAmount ret = 0;
    BOOST_CHECK(ParseMoney("0.0", ret));
    BOOST_CHECK_EQUAL(ret, 0);

    BOOST_CHECK(ParseMoney("12345.6789", ret));
    BOOST_CHECK_EQUAL(ret, (COIN / 10000) * 123456789);

    BOOST_CHECK(ParseMoney("100000000.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 100000000);
    BOOST_CHECK(ParseMoney("10000000.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 10000000);
    BOOST_CHECK(ParseMoney("1000000.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 1000000);
    BOOST_CHECK(ParseMoney("100000.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 100000);
    BOOST_CHECK(ParseMoney("10000.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 10000);
    BOOST_CHECK(ParseMoney("1000.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 1000);
    BOOST_CHECK(ParseMoney("100.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 100);
    BOOST_CHECK(ParseMoney("10.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN * 10);
    BOOST_CHECK(ParseMoney("1.00", ret));
    BOOST_CHECK_EQUAL(ret, COIN);
    BOOST_CHECK(ParseMoney("1", ret));
    BOOST_CHECK_EQUAL(ret, COIN);
    BOOST_CHECK(ParseMoney("0.1", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 10);
    BOOST_CHECK(ParseMoney("0.01", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 100);
    BOOST_CHECK(ParseMoney("0.001", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 1000);
    BOOST_CHECK(ParseMoney("0.0001", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 10000);
    BOOST_CHECK(ParseMoney("0.00001", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 100000);
    BOOST_CHECK(ParseMoney("0.000001", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 1000000);
    BOOST_CHECK(ParseMoney("0.0000001", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 10000000);
    BOOST_CHECK(ParseMoney("0.00000001", ret));
    BOOST_CHECK_EQUAL(ret, COIN / 100000000);

    // Attempted 63 bit overflow should fail
    BOOST_CHECK(!ParseMoney("92233720368.54775808", ret));

    // Parsing negative amounts must fail
    BOOST_CHECK(!ParseMoney("-1", ret));
}

BOOST_AUTO_TEST_CASE(util_IsHex)
{
    BOOST_CHECK(IsHex("00"));
    BOOST_CHECK(IsHex("00112233445566778899aabbccddeeffAABBCCDDEEFF"));
    BOOST_CHECK(IsHex("ff"));
    BOOST_CHECK(IsHex("FF"));

    BOOST_CHECK(!IsHex(""));
    BOOST_CHECK(!IsHex("0"));
    BOOST_CHECK(!IsHex("a"));
    BOOST_CHECK(!IsHex("eleven"));
    BOOST_CHECK(!IsHex("00xx00"));
    BOOST_CHECK(!IsHex("0x0000"));
}

BOOST_AUTO_TEST_CASE(util_seed_insecure_rand)
{
    int i;
    int count = 0;

    SeedInsecureRand(true);

    for (int mod = 2; mod < 11; mod++)
    {
        int mask = 1;
        // Really rough binomal confidence approximation.
        int err = 30 * 10000. / mod * sqrt((1. / mod * (1 - 1. / mod)) / 10000.);
        // mask is 2^ceil(log2(mod))-1
        while (mask < mod - 1)
            mask = (mask << 1) + 1;

        count = 0;
        // How often does it get a zero from the uniform range [0,mod)?
        for (i = 0; i < 10000; i++)
        {
            uint32_t rval;
            do
            {
                rval = InsecureRand32() & mask;
            } while (rval >= (uint32_t)mod);
            count += rval == 0;
        }
        BOOST_CHECK(count <= 10000 / mod + err);
        BOOST_CHECK(count >= 10000 / mod - err);
    }
}

BOOST_AUTO_TEST_CASE(util_TimingResistantEqual)
{
    BOOST_CHECK(TimingResistantEqual(std::string(""), std::string("")));
    BOOST_CHECK(!TimingResistantEqual(std::string("abc"), std::string("")));
    BOOST_CHECK(!TimingResistantEqual(std::string(""), std::string("abc")));
    BOOST_CHECK(!TimingResistantEqual(std::string("a"), std::string("aa")));
    BOOST_CHECK(!TimingResistantEqual(std::string("aa"), std::string("a")));
    BOOST_CHECK(TimingResistantEqual(std::string("abc"), std::string("abc")));
    BOOST_CHECK(!TimingResistantEqual(std::string("abc"), std::string("aba")));
}

/* Test strprintf formatting directives.
 * Put a string before and after to ensure sanity of element sizes on stack. */
#define B "check_prefix"
#define E "check_postfix"
BOOST_AUTO_TEST_CASE(strprintf_numbers)
{
    int64_t s64t = -9223372036854775807LL; /* signed 64 bit test value */
    uint64_t u64t = 18446744073709551615ULL; /* unsigned 64 bit test value */
    BOOST_CHECK(strprintf("%s %d %s", B, s64t, E) == B " -9223372036854775807 " E);
    BOOST_CHECK(strprintf("%s %u %s", B, u64t, E) == B " 18446744073709551615 " E);
    BOOST_CHECK(strprintf("%s %x %s", B, u64t, E) == B " ffffffffffffffff " E);

    size_t st = 12345678; /* unsigned size_t test value */
    ssize_t sst = -12345678; /* signed size_t test value */
    BOOST_CHECK(strprintf("%s %d %s", B, sst, E) == B " -12345678 " E);
    BOOST_CHECK(strprintf("%s %u %s", B, st, E) == B " 12345678 " E);
    BOOST_CHECK(strprintf("%s %x %s", B, st, E) == B " bc614e " E);

    ptrdiff_t pt = 87654321; /* positive ptrdiff_t test value */
    ptrdiff_t spt = -87654321; /* negative ptrdiff_t test value */
    BOOST_CHECK(strprintf("%s %d %s", B, spt, E) == B " -87654321 " E);
    BOOST_CHECK(strprintf("%s %u %s", B, pt, E) == B " 87654321 " E);
    BOOST_CHECK(strprintf("%s %x %s", B, pt, E) == B " 5397fb1 " E);
}
#undef B
#undef E

/* Check for mingw/wine issue #3494
 * Remove this test before time.ctime(0xffffffff) == 'Sun Feb  7 07:28:15 2106'
 */
BOOST_AUTO_TEST_CASE(gettime) { BOOST_CHECK((GetTime() & ~0xFFFFFFFFLL) == 0); }
BOOST_AUTO_TEST_CASE(test_ParseInt32)
{
    int32_t n;
    // Valid values
    BOOST_CHECK(ParseInt32("1234", nullptr));
    BOOST_CHECK(ParseInt32("0", &n) && n == 0);
    BOOST_CHECK(ParseInt32("1234", &n) && n == 1234);
    BOOST_CHECK(ParseInt32("01234", &n) && n == 1234); // no octal
    BOOST_CHECK(ParseInt32("2147483647", &n) && n == 2147483647);
    BOOST_CHECK(ParseInt32("-2147483648", &n) && n == -2147483648);
    BOOST_CHECK(ParseInt32("-1234", &n) && n == -1234);
    // Invalid values
    BOOST_CHECK(!ParseInt32("", &n));
    BOOST_CHECK(!ParseInt32(" 1", &n)); // no padding inside
    BOOST_CHECK(!ParseInt32("1 ", &n));
    BOOST_CHECK(!ParseInt32("1a", &n));
    BOOST_CHECK(!ParseInt32("aap", &n));
    BOOST_CHECK(!ParseInt32("0x1", &n)); // no hex
    BOOST_CHECK(!ParseInt32("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    BOOST_CHECK(!ParseInt32(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    BOOST_CHECK(!ParseInt32("-2147483649", nullptr));
    BOOST_CHECK(!ParseInt32("2147483648", nullptr));
    BOOST_CHECK(!ParseInt32("-32482348723847471234", nullptr));
    BOOST_CHECK(!ParseInt32("32482348723847471234", nullptr));
}

BOOST_AUTO_TEST_CASE(test_ParseInt64)
{
    int64_t n;
    // Valid values
    BOOST_CHECK(ParseInt64("1234", nullptr));
    BOOST_CHECK(ParseInt64("0", &n) && n == 0LL);
    BOOST_CHECK(ParseInt64("1234", &n) && n == 1234LL);
    BOOST_CHECK(ParseInt64("01234", &n) && n == 1234LL); // no octal
    BOOST_CHECK(ParseInt64("2147483647", &n) && n == 2147483647LL);
    BOOST_CHECK(ParseInt64("-2147483648", &n) && n == -2147483648LL);
    BOOST_CHECK(ParseInt64("9223372036854775807", &n) && n == (int64_t)9223372036854775807);
    BOOST_CHECK(ParseInt64("-9223372036854775808", &n) && n == (int64_t)-9223372036854775807 - 1);
    BOOST_CHECK(ParseInt64("-1234", &n) && n == -1234LL);
    // Invalid values
    BOOST_CHECK(!ParseInt64("", &n));
    BOOST_CHECK(!ParseInt64(" 1", &n)); // no padding inside
    BOOST_CHECK(!ParseInt64("1 ", &n));
    BOOST_CHECK(!ParseInt64("1a", &n));
    BOOST_CHECK(!ParseInt64("aap", &n));
    BOOST_CHECK(!ParseInt64("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    BOOST_CHECK(!ParseInt64(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    BOOST_CHECK(!ParseInt64("-9223372036854775809", nullptr));
    BOOST_CHECK(!ParseInt64("9223372036854775808", nullptr));
    BOOST_CHECK(!ParseInt64("-32482348723847471234", nullptr));
    BOOST_CHECK(!ParseInt64("32482348723847471234", nullptr));
}

BOOST_AUTO_TEST_CASE(test_ParseDouble)
{
    double n;
    // Valid values
    BOOST_CHECK(ParseDouble("1234", nullptr));
    BOOST_CHECK(ParseDouble("0", &n) && n == 0.0);
    BOOST_CHECK(ParseDouble("1234", &n) && n == 1234.0);
    BOOST_CHECK(ParseDouble("01234", &n) && n == 1234.0); // no octal
    BOOST_CHECK(ParseDouble("2147483647", &n) && n == 2147483647.0);
    BOOST_CHECK(ParseDouble("-2147483648", &n) && n == -2147483648.0);
    BOOST_CHECK(ParseDouble("-1234", &n) && n == -1234.0);
    BOOST_CHECK(ParseDouble("1e6", &n) && n == 1e6);
    BOOST_CHECK(ParseDouble("-1e6", &n) && n == -1e6);
    // Invalid values
    BOOST_CHECK(!ParseDouble("", &n));
    BOOST_CHECK(!ParseDouble(" 1", &n)); // no padding inside
    BOOST_CHECK(!ParseDouble("1 ", &n));
    BOOST_CHECK(!ParseDouble("1a", &n));
    BOOST_CHECK(!ParseDouble("aap", &n));
    BOOST_CHECK(!ParseDouble("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    BOOST_CHECK(!ParseDouble(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    BOOST_CHECK(!ParseDouble("-1e10000", nullptr));
    BOOST_CHECK(!ParseDouble("1e10000", nullptr));
}

BOOST_AUTO_TEST_CASE(test_FormatParagraph)
{
    BOOST_CHECK_EQUAL(FormatParagraph("", 79, 0), "");
    BOOST_CHECK_EQUAL(FormatParagraph("test", 79, 0), "test");
    BOOST_CHECK_EQUAL(FormatParagraph(" test", 79, 0), " test");
    BOOST_CHECK_EQUAL(FormatParagraph("test test", 79, 0), "test test");
    BOOST_CHECK_EQUAL(FormatParagraph("test test", 4, 0), "test\ntest");
    BOOST_CHECK_EQUAL(FormatParagraph("testerde test", 4, 0), "testerde\ntest");
    BOOST_CHECK_EQUAL(FormatParagraph("test test", 4, 4), "test\n    test");

    // Make sure we don't indent a fully-new line following a too-long line ending
    BOOST_CHECK_EQUAL(FormatParagraph("test test\nabc", 4, 4), "test\n    test\nabc");

    BOOST_CHECK_EQUAL(FormatParagraph("This_is_a_very_long_test_string_without_any_spaces_so_it_should_just_get_"
                                      "returned_as_is_despite_the_length until it gets here",
                          79),
        "This_is_a_very_long_test_string_without_any_spaces_so_it_should_just_get_returned_as_is_despite_the_"
        "length\nuntil it gets here");

    // Test wrap length is exact
    BOOST_CHECK_EQUAL(
        FormatParagraph(
            "a b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9 a b c de f g h i j k l m n o p", 79),
        "a b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9 a b c de\nf g h i j k l m n o p");
    BOOST_CHECK_EQUAL(
        FormatParagraph(
            "x\na b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9 a b c de f g h i j k l m n o p",
            79),
        "x\na b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9 a b c de\nf g h i j k l m n o p");
    // Indent should be included in length of lines
    BOOST_CHECK_EQUAL(
        FormatParagraph("x\na b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9 a b c de f g h i j k "
                        "l m n o p q r s t u v w x y z 0 1 2 3 4 5 6 7 8 9 a b c d e fg h i j k",
            79, 4),
        "x\na b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9 a b c de\n    f g h i j k l m n o p "
        "q r s t u v w x y z 0 1 2 3 4 5 6 7 8 9 a b c d e fg\n    h i j k");

    BOOST_CHECK_EQUAL(
        FormatParagraph("This is a very long test string. This is a second sentence in the very long test string.", 79),
        "This is a very long test string. This is a second sentence in the very long\ntest string.");
    BOOST_CHECK_EQUAL(FormatParagraph("This is a very long test string.\nThis is a second sentence in the very long "
                                      "test string. This is a third sentence in the very long test string.",
                          79),
        "This is a very long test string.\nThis is a second sentence in the very long test string. This is a "
        "third\nsentence in the very long test string.");
    BOOST_CHECK_EQUAL(FormatParagraph("This is a very long test string.\n\nThis is a second sentence in the very long "
                                      "test string. This is a third sentence in the very long test string.",
                          79),
        "This is a very long test string.\n\nThis is a second sentence in the very long test string. This is a "
        "third\nsentence in the very long test string.");
    BOOST_CHECK_EQUAL(FormatParagraph("Testing that normal newlines do not get indented.\nLike here.", 79),
        "Testing that normal newlines do not get indented.\nLike here.");
}

BOOST_AUTO_TEST_CASE(test_FormatSubVersion)
{
    int temp = 0;
    int *ptemp = &temp;
    std::string arch = (sizeof(ptemp) == 4) ? "32bit" : "64bit";

    std::vector<std::string> comments;
    comments.push_back(std::string("comment1"));
    std::vector<std::string> comments2;
    comments2.push_back(std::string("comment1"));
    // Semicolon is discouraged but not forbidden by BIP-0014
    comments2.push_back(
        SanitizeString(std::string("Comment2; .,_?@-; !\"#$%&'()*+/<=>[]\\^`{|}~"), SAFE_CHARS_UA_COMMENT));
    BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99800, {}), std::string("/Test:0.9.98(" + arch + ")/"));
    BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, comments), std::string("/Test:0.9.99(comment1; " + arch + ")/"));
    BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, comments2),
        std::string("/Test:0.9.99(comment1; Comment2; .,_?@-; ; " + arch + ")/"));

    excessiveBlockSize = 1000000;
    excessiveAcceptDepth = 40;
    settingsToUserAgentString();
    const char *argv_test[] = {"bitcoind", "-uacomment=comment1", "-uacomment=Comment2", "-uacomment=Comment3"};
    ParseParameters(4, (char **)argv_test, AllowedArgs::Bitcoind());
    BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, BUComments),
        std::string("/Test:0.9.99(EB1; AD40; " + arch + "; comment1; Comment2; Comment3)/"));

    const char *argv_test2[] = {"bitcoind", "-uacomment=Commenttttttttttttttttttttttttttttttttttttttttt1",
        "-uacomment=Commenttttttttttttttttttttttttttttttttttttttttttttttttttttt2",
        "-uacomment=Commenttttttttttttttttttttttttttttttttttttttttttttttttttttt3",
        "-uacomment=Commenttttttttttttttttttttttttttttttttttttttttttttttttttttt4"};
    ParseParameters(5, (char **)argv_test2, AllowedArgs::Bitcoind());
    BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, BUComments),
        std::string("/Test:0.9.99(EB1; AD40; " + arch + "; Commenttttttttttttttttttttttttttttttttttttttttt1; "
                                                        "Commenttttttttttttttttttttttttttttttttttttttttttttttttttttt2; "
                                                        "Commenttttttttttttttttttttttttttttttttttttttttttttttttttttt3; "
                                                        "Commenttttttttttttttttttttttttttttttttttttttttttt)/"));

    std::string subver = FormatSubVersion("Test", 99900, BUComments);
    BOOST_CHECK_EQUAL(subver.size(), MAX_SUBVERSION_LENGTH);

    // Check if displayArchInSubver Tweak is working
    fDisplayArchInSubver = false;
    settingsToUserAgentString();
    const char *argv_test3[] = {"bitcoind", "-uacomment=comment1", "-uacomment=Comment2", "-uacomment=Comment3"};
    ParseParameters(4, (char **)argv_test3, AllowedArgs::Bitcoind());
    BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, BUComments),
        std::string("/Test:0.9.99(EB1; AD40; comment1; Comment2; Comment3)/"));

    // set EB/AD back to default value
    excessiveBlockSize = DEFAULT_EXCESSIVE_BLOCK_SIZE;
    excessiveAcceptDepth = DEFAULT_EXCESSIVE_ACCEPT_DEPTH;
    fDisplayArchInSubver = true;
}

BOOST_AUTO_TEST_CASE(test_ParseFixedPoint)
{
    int64_t amount = 0;
    BOOST_CHECK(ParseFixedPoint("0", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 0LL);
    BOOST_CHECK(ParseFixedPoint("1", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 100000000LL);
    BOOST_CHECK(ParseFixedPoint("0.0", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 0LL);
    BOOST_CHECK(ParseFixedPoint("-0.1", 8, &amount));
    BOOST_CHECK_EQUAL(amount, -10000000LL);
    BOOST_CHECK(ParseFixedPoint("1.1", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 110000000LL);
    BOOST_CHECK(ParseFixedPoint("1.10000000000000000", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 110000000LL);
    BOOST_CHECK(ParseFixedPoint("1.1e1", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 1100000000LL);
    BOOST_CHECK(ParseFixedPoint("1.1e-1", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 11000000LL);
    BOOST_CHECK(ParseFixedPoint("1000", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 100000000000LL);
    BOOST_CHECK(ParseFixedPoint("-1000", 8, &amount));
    BOOST_CHECK_EQUAL(amount, -100000000000LL);
    BOOST_CHECK(ParseFixedPoint("0.00000001", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 1LL);
    BOOST_CHECK(ParseFixedPoint("0.0000000100000000", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 1LL);
    BOOST_CHECK(ParseFixedPoint("-0.00000001", 8, &amount));
    BOOST_CHECK_EQUAL(amount, -1LL);
    BOOST_CHECK(ParseFixedPoint("1000000000.00000001", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 100000000000000001LL);
    BOOST_CHECK(ParseFixedPoint("9999999999.99999999", 8, &amount));
    BOOST_CHECK_EQUAL(amount, 999999999999999999LL);
    BOOST_CHECK(ParseFixedPoint("-9999999999.99999999", 8, &amount));
    BOOST_CHECK_EQUAL(amount, -999999999999999999LL);

    BOOST_CHECK(!ParseFixedPoint("", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("a-1000", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-a1000", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-1000a", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-01000", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("00.1", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint(".1", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("--0.1", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("0.000000001", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-0.000000001", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("0.00000001000000001", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-10000000000.00000000", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("10000000000.00000000", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-10000000000.00000001", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("10000000000.00000001", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-10000000000.00000009", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("10000000000.00000009", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-99999999999.99999999", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("99999909999.09999999", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("92233720368.54775807", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("92233720368.54775808", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-92233720368.54775808", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("-92233720368.54775809", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("1.1e", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("1.1e-", 8, &amount));
    BOOST_CHECK(!ParseFixedPoint("1.", 8, &amount));
}

template <int F, int T>
static void CheckConvertBits(const std::vector<uint8_t> &in, const std::vector<uint8_t> &expected)
{
    std::vector<uint8_t> outpad;
    bool ret = ConvertBits<F, T, true>(outpad, in.begin(), in.end());
    BOOST_CHECK(ret);
    BOOST_CHECK(outpad == expected);

    const bool dopad = (in.size() * F) % T;
    std::vector<uint8_t> outnopad;
    ret = ConvertBits<F, T, false>(outnopad, in.begin(), in.end());
    BOOST_CHECK(ret != dopad);

    if (dopad)
    {
        // We should have skipped the last digit.
        outnopad.push_back(expected.back());
    }

    BOOST_CHECK(outnopad == expected);

    // Check the other way around.
    std::vector<uint8_t> orignopad;
    ret = ConvertBits<T, F, false>(orignopad, expected.begin(), expected.end());
    BOOST_CHECK(ret == !((expected.size() * T) % F));
    BOOST_CHECK(orignopad == in);

    // Check with padding. We may get an extra 0 in that case.
    std::vector<uint8_t> origpad;
    ret = ConvertBits<T, F, true>(origpad, expected.begin(), expected.end());
    BOOST_CHECK(ret);

    if (dopad)
    {
        BOOST_CHECK_EQUAL(origpad.back(), 0);
        origpad.pop_back();
    }

    BOOST_CHECK(origpad == in);
}

BOOST_AUTO_TEST_CASE(test_ConvertBits)
{
    CheckConvertBits<8, 5>({}, {});
    CheckConvertBits<8, 5>({0xff}, {0x1f, 0x1c});
    CheckConvertBits<8, 5>({0xff, 0xff}, {0x1f, 0x1f, 0x1f, 0x10});
    CheckConvertBits<8, 5>({0xff, 0xff, 0xff}, {0x1f, 0x1f, 0x1f, 0x1f, 0x1e});
    CheckConvertBits<8, 5>({0xff, 0xff, 0xff, 0xff}, {0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x18});
    CheckConvertBits<8, 5>({0xff, 0xff, 0xff, 0xff, 0xff}, {0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f});
    CheckConvertBits<8, 5>({0xff, 0xff, 0xff, 0xff, 0xff}, {0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f});
    CheckConvertBits<8, 5>({0xff, 0xff, 0xff, 0xff, 0xff}, {0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f});
    CheckConvertBits<8, 5>({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
        {0x00, 0x04, 0x11, 0x14, 0x0a, 0x19, 0x1c, 0x09, 0x15, 0x0f, 0x06, 0x1e, 0x1e});
}

BOOST_AUTO_TEST_CASE(util_Logging)
{
    {
        using namespace Logging;
        BOOST_CHECK_EQUAL(8, sizeof(categoriesEnabled));
        BOOST_CHECK_EQUAL(NONE, categoriesEnabled);
        LogToggleCategory(THIN, true);
        BOOST_CHECK(LogAcceptCategory(THIN));
        LogToggleCategory(THIN, false);
        BOOST_CHECK(!LogAcceptCategory(THIN));
        LogToggleCategory(THIN, true);
        LogToggleCategory(NET, true);
        BOOST_CHECK(LogAcceptCategory(THIN | NET));
        LogToggleCategory(ALL, true);
        BOOST_CHECK_EQUAL(ALL, categoriesEnabled);
        LogToggleCategory(ALL, false);
        BOOST_CHECK_EQUAL(NONE, categoriesEnabled);
        BOOST_CHECK_EQUAL(LogGetLabel(ADDRMAN), "addrman");
        LogToggleCategory(ALL, true);
        LOG(THIN, "missing args %s %d\n");
        LOG(THIN, "wrong order args %s %d\n", 3, "hello");
        LOG(THIN, "null arg %s\n", NULL);
        LOG(THIN, "test no CR");
    }
}

BOOST_AUTO_TEST_CASE(isstringtrue)
{
    BOOST_CHECK(IsStringTrue("true"));
    BOOST_CHECK(IsStringTrue("enable"));
    BOOST_CHECK(IsStringTrue("1"));
    BOOST_CHECK(IsStringTrue("on"));
    BOOST_CHECK(!IsStringTrue("false"));
    BOOST_CHECK(!IsStringTrue("disable"));
    BOOST_CHECK(!IsStringTrue("0"));
    BOOST_CHECK(!IsStringTrue("off"));
    BOOST_CHECK_THROW(IsStringTrue("bad"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(util_wildmatch)
{
    BOOST_CHECK(wildmatch("123", "123"));
    BOOST_CHECK(wildmatch("", ""));
    BOOST_CHECK(wildmatch("?", "?"));
    BOOST_CHECK(wildmatch("?", "x"));
    BOOST_CHECK(wildmatch("*", "123"));
    BOOST_CHECK(!wildmatch("456", "123"));

    // multi-star pattern is not allowed
    BOOST_CHECK(!wildmatch("**", "123"));
    BOOST_CHECK(!wildmatch("************************************", "123"));
    BOOST_CHECK(!wildmatch("?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?", "123"));

    BOOST_CHECK(wildmatch("????", "1234"));
    BOOST_CHECK(wildmatch("????a?b?", "1234a5b6"));
    BOOST_CHECK(!wildmatch("????a?b?", "1234a5c6"));
    BOOST_CHECK(wildmatch("123*", "123456"));
    BOOST_CHECK(wildmatch("123*456", "123acdef456"));
    BOOST_CHECK(wildmatch("*123", "abcdef123"));

    // length limit check
    BOOST_CHECK(!wildmatch(std::string("*", 10000), ""));
    BOOST_CHECK(!wildmatch("*", std::string("x", 10000)));
}

BOOST_AUTO_TEST_CASE(splitbycommaandremovespaces)
{
    std::vector<std::string> inp1{"one", "two, three  ", "f o u r"};

    const std::vector<std::string> r = splitByCommasAndRemoveSpaces(inp1);

    BOOST_CHECK_EQUAL(r.size(), 4);
    BOOST_CHECK_EQUAL(r[3], "one");
    BOOST_CHECK_EQUAL(r[2], "two");
    BOOST_CHECK_EQUAL(r[1], "three");
    BOOST_CHECK_EQUAL(r[0], "four");

    const std::vector<std::string> r2 = splitByCommasAndRemoveSpaces(r);
    BOOST_CHECK_EQUAL(r.size(), 4);
    BOOST_CHECK_EQUAL(r[3], "one");
    BOOST_CHECK_EQUAL(r[2], "two");
    BOOST_CHECK_EQUAL(r[1], "three");
    BOOST_CHECK_EQUAL(r[0], "four");

    std::vector<std::string> inp2{"one", "two, two  ", "f o u r"};
    const std::vector<std::string> r3 = splitByCommasAndRemoveSpaces(inp2, true);
    BOOST_CHECK_EQUAL(r3.size(), 3);
    BOOST_CHECK_EQUAL(r3[2], "four");
    BOOST_CHECK_EQUAL(r3[1], "one");
    BOOST_CHECK_EQUAL(r3[0], "two");

    std::vector<std::string> inp3{"1", "2", "3", "-4"};
    const std::vector<std::string> r4 = splitByCommasAndRemoveSpaces(inp3, true);
    BOOST_CHECK_EQUAL(r4.size(), 4);
    BOOST_CHECK_EQUAL(r4[0], "3");
    BOOST_CHECK_EQUAL(r4[1], "2");
    BOOST_CHECK_EQUAL(r4[2], "1");
    BOOST_CHECK_EQUAL(r4[3], "-4");
}

BOOST_AUTO_TEST_CASE(enum_toString)
{
    std::map<uint64_t, std::string> map1 = {{1, "ONE"}, {2, "TWO"}, {4, "FOUR"}};
    std::map<uint64_t, std::string> map2 = {{1, "ONE"}, {2, "TWO"}, {4, "FOUR"}, {7, "ALL"}};

    BOOST_CHECK_EQUAL(toString(0, {}), "");
    BOOST_CHECK_EQUAL(toString(1, {}), "");
    BOOST_CHECK_EQUAL(toString(123, {}), "");
    BOOST_CHECK_EQUAL(toString(1, {{1, "1"}}), "1");
    BOOST_CHECK_EQUAL(toString(1, map1), "ONE");
    BOOST_CHECK_EQUAL(toString(3, map1), "ONE | TWO");
    BOOST_CHECK_EQUAL(toString(5, map1), "ONE | FOUR");
    BOOST_CHECK_EQUAL(toString(6, map2), "TWO | FOUR");
    BOOST_CHECK_EQUAL(toString(7, map1), "ONE | TWO | FOUR");
    BOOST_CHECK_EQUAL(toString(7, map2), "ALL");
}

BOOST_AUTO_TEST_CASE(reverse_iterator)
{
    std::vector<int> v = {5, 4, 3, 2, 1};
    int cnt = 1;
    for (auto x : reverse_iterate(v))
    {
        BOOST_CHECK_EQUAL(x, cnt);
        cnt++;
    }

    v = {}; // check empty vector
    for (auto x : reverse_iterate(v))
    {
        BOOST_CHECK(x == 100); // should never get here but use x to avoid warning
    }
}

BOOST_AUTO_TEST_SUITE_END()
