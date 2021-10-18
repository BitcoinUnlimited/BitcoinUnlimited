// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/script.h"
#include "scriptnum10.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <limits.h>
#include <stdint.h>

BOOST_FIXTURE_TEST_SUITE(scriptnum_tests, BasicTestingSetup)

static constexpr int64_t int64_t_min = std::numeric_limits<int64_t>::min();
static constexpr int64_t int64_t_max = std::numeric_limits<int64_t>::max();
static constexpr int64_t int64_t_min_8_bytes = int64_t_min + 1;

/** A selection of numbers that do not trigger int64_t overflow
 *  when added/subtracted. */
static const int64_t values[] = {0, 1, -2, 127, 128, -255, 256, (1LL << 15) - 1, -(1LL << 16), (1LL << 24) - 1,
    (1LL << 31), 1 - (1LL << 32), 1LL << 40, int64_t_min_8_bytes, int64_t_min, int64_t_max};

static const int64_t offsets[] = {1, 0x79, 0x80, 0x81, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x10000};

static bool verify(const CScriptNum10 &bignum, const CScriptNum &scriptnum)
{
    return bignum.getvch() == scriptnum.getvch() && bignum.getint() == scriptnum.getint32();
}

static void CheckCreateVchOldRules(int64_t x)
{
    const size_t maxIntegerSize = CScriptNum::MAXIMUM_ELEMENT_SIZE_32_BIT;

    CScriptNum10 bigx(x);
    const CScriptNum scriptx = CScriptNum::fromIntUnchecked(x);
    BOOST_CHECK(verify(bigx, scriptx));

    CScriptNum10 bigb(bigx.getvch(), false);
    CScriptNum scriptb(scriptx.getvch(), false, maxIntegerSize);
    BOOST_CHECK(verify(bigb, scriptb));

    CScriptNum10 bigx3(scriptb.getvch(), false);
    CScriptNum scriptx3(bigb.getvch(), false, maxIntegerSize);
    BOOST_CHECK(verify(bigx3, scriptx3));
}

static void CheckCreateVchNewRules(int64_t x)
{
    const size_t maxIntegerSize = CScriptNum::MAXIMUM_ELEMENT_SIZE_64_BIT;

    auto res = CScriptNum::fromInt(x);
    if (!res)
    {
        BOOST_CHECK(x == int64_t_min);
        return;
    }
    const CScriptNum scriptx = *res;

    CScriptNum10 bigx(x);
    BOOST_CHECK(verify(bigx, scriptx));

    CScriptNum10 bigb(bigx.getvch(), false, maxIntegerSize);
    CScriptNum scriptb(scriptx.getvch(), false, maxIntegerSize);
    BOOST_CHECK(verify(bigb, scriptb));

    CScriptNum10 bigx3(scriptb.getvch(), false, maxIntegerSize);
    CScriptNum scriptx3(bigb.getvch(), false, maxIntegerSize);
    BOOST_CHECK(verify(bigx3, scriptx3));
}

static void CheckCreateIntOldRules(int64_t x)
{
    const CScriptNum scriptx = CScriptNum::fromIntUnchecked(x);
    CScriptNum10 const bigx(x);
    BOOST_CHECK(verify(bigx, scriptx));
    BOOST_CHECK(verify(CScriptNum10(bigx.getint()), CScriptNum::fromIntUnchecked(scriptx.getint32())));
    BOOST_CHECK(verify(CScriptNum10(scriptx.getint32()), CScriptNum::fromIntUnchecked(bigx.getint())));
    BOOST_CHECK(verify(CScriptNum10(CScriptNum10(scriptx.getint32()).getint()),
        CScriptNum::fromIntUnchecked(CScriptNum::fromIntUnchecked(bigx.getint()).getint32())));
}

static void CheckCreateIntNewRules(int64_t x)
{
    auto res = CScriptNum::fromInt(x);
    if (!res)
    {
        BOOST_CHECK(x == int64_t_min);
        return;
    }
    const CScriptNum scriptx = *res;

    const CScriptNum10 bigx(x);
    BOOST_CHECK(verify(bigx, scriptx));
    BOOST_CHECK(verify(CScriptNum10(bigx.getint()), CScriptNum::fromIntUnchecked(scriptx.getint32())));
    BOOST_CHECK(verify(CScriptNum10(scriptx.getint32()), CScriptNum::fromIntUnchecked(bigx.getint())));
    BOOST_CHECK(verify(CScriptNum10(CScriptNum10(scriptx.getint32()).getint()),
        CScriptNum::fromIntUnchecked(CScriptNum::fromIntUnchecked(bigx.getint()).getint32())));
}


static void CheckAddOldRules(int64_t a, int64_t b)
{
    if (a == int64_t_min || b == int64_t_min)
    {
        return;
    }

    CScriptNum10 const biga(a);
    CScriptNum10 const bigb(b);
    const CScriptNum scripta = CScriptNum::fromIntUnchecked(a);
    const CScriptNum scriptb = CScriptNum::fromIntUnchecked(b);

    // int64_t overflow is undefined.
    bool overflowing = (b > 0 && a > int64_t_max - b) || (b < 0 && a < int64_t_min_8_bytes - b);

    if (!overflowing)
    {
        auto res = scripta.safeAdd(scriptb);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(biga + bigb, *res));
        res = scripta.safeAdd(b);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(biga + bigb, *res));
        res = scriptb.safeAdd(scripta);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(biga + bigb, *res));
        res = scriptb.safeAdd(a);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(biga + bigb, *res));
    }
    else
    {
        BOOST_CHECK(!scripta.safeAdd(scriptb));
        BOOST_CHECK(!scripta.safeAdd(b));
        BOOST_CHECK(!scriptb.safeAdd(a));
    }
}

static void CheckAddNewRules(int64_t a, int64_t b)
{
    auto res = CScriptNum::fromInt(a);
    if (!res)
    {
        BOOST_CHECK(a == int64_t_min);
        return;
    }
    const CScriptNum scripta = *res;

    res = CScriptNum::fromInt(b);
    if (!res)
    {
        BOOST_CHECK(b == int64_t_min);
        return;
    }
    const CScriptNum scriptb = *res;

    bool overflowing = (b > 0 && a > int64_t_max - b) || (b < 0 && a < int64_t_min_8_bytes - b);

    res = scripta.safeAdd(scriptb);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || a + b == res->getint64());

    res = scripta.safeAdd(b);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || a + b == res->getint64());

    res = scriptb.safeAdd(scripta);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || b + a == res->getint64());

    res = scriptb.safeAdd(a);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || b + a == res->getint64());
}

static void CheckSubtractOldRules(int64_t a, int64_t b)
{
    if (a == int64_t_min || b == int64_t_min)
    {
        return;
    }

    CScriptNum10 const biga(a);
    CScriptNum10 const bigb(b);
    const CScriptNum scripta = CScriptNum::fromIntUnchecked(a);
    const CScriptNum scriptb = CScriptNum::fromIntUnchecked(b);

    // int64_t overflow is undefined.
    bool overflowing = (b > 0 && a < int64_t_min_8_bytes + b) || (b < 0 && a > int64_t_max + b);

    if (!overflowing)
    {
        auto res = scripta.safeSub(scriptb);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(biga - bigb, *res));
        res = scripta.safeSub(b);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(biga - bigb, *res));
    }
    else
    {
        BOOST_CHECK(!scripta.safeSub(scriptb));
        BOOST_CHECK(!scripta.safeSub(b));
    }

    overflowing = (a > 0 && b < int64_t_min_8_bytes + a) || (a < 0 && b > int64_t_max + a);

    if (!overflowing)
    {
        auto res = scriptb.safeSub(scripta);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(bigb - biga, *res));
        res = scriptb.safeSub(a);
        BOOST_CHECK(res);
        BOOST_CHECK(verify(bigb - biga, *res));
    }
    else
    {
        BOOST_CHECK(!scriptb.safeSub(scripta));
        BOOST_CHECK(!scriptb.safeSub(a));
    }
}

static void CheckSubtractNewRules(int64_t a, int64_t b)
{
    auto res = CScriptNum::fromInt(a);
    if (!res)
    {
        BOOST_CHECK(a == int64_t_min);
        return;
    }
    auto const scripta = *res;

    res = CScriptNum::fromInt(b);
    if (!res)
    {
        BOOST_CHECK(b == int64_t_min);
        return;
    }
    auto const scriptb = *res;

    bool overflowing = (b > 0 && a < int64_t_min_8_bytes + b) || (b < 0 && a > int64_t_max + b);

    res = scripta.safeSub(scriptb);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || a - b == res->getint64());

    res = scripta.safeSub(b);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || a - b == res->getint64());

    overflowing = (a > 0 && b < int64_t_min_8_bytes + a) || (a < 0 && b > int64_t_max + a);

    res = scriptb.safeSub(scripta);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || b - a == res->getint64());

    res = scriptb.safeSub(a);
    BOOST_CHECK(bool(res) != overflowing);
    BOOST_CHECK(!res || b - a == res->getint64());
}

static void CheckMultiply(int64_t a, int64_t b)
{
    auto res = CScriptNum::fromInt(a);
    if (!res)
    {
        BOOST_CHECK(a == int64_t_min);
        return;
    }
    const CScriptNum scripta = *res;

    res = CScriptNum::fromInt(b);
    if (!res)
    {
        BOOST_CHECK(b == int64_t_min);
        return;
    }
    const CScriptNum scriptb = *res;

    res = scripta.safeMul(scriptb);
    BOOST_CHECK(!res || a * b == res->getint64());
    res = scripta.safeMul(b);
    BOOST_CHECK(!res || a * b == res->getint64());
    res = scriptb.safeMul(scripta);
    BOOST_CHECK(!res || b * a == res->getint64());
    res = scriptb.safeMul(a);
    BOOST_CHECK(!res || b * a == res->getint64());
}

static void CheckDivideOldRules(int64_t a, int64_t b)
{
    CScriptNum10 const biga(a);
    CScriptNum10 const bigb(b);
    const CScriptNum scripta = CScriptNum::fromIntUnchecked(a);
    const CScriptNum scriptb = CScriptNum::fromIntUnchecked(b);

    // int64_t overflow is undefined.
    bool overflowing = a == int64_t_min && b == -1;

    if (b != 0)
    {
        if (!overflowing)
        {
            auto res = scripta / scriptb;
            BOOST_CHECK(verify(CScriptNum10(a / b), res));
            res = scripta / b;
            BOOST_CHECK(verify(CScriptNum10(a / b), res));
        }
        else
        {
            BOOST_CHECK(scripta / scriptb == scripta);
            BOOST_CHECK(verify(biga, scripta / b));
        }
    }

    overflowing = b == int64_t_min && a == -1;

    if (a != 0)
    {
        if (!overflowing)
        {
            auto res = scriptb / scripta;
            BOOST_CHECK(verify(CScriptNum10(b / a), res));
            res = scriptb / a;
            BOOST_CHECK(verify(CScriptNum10(b / a), res));
        }
        else
        {
            BOOST_CHECK(scriptb / scripta == scripta);
            BOOST_CHECK(verify(bigb, scriptb / a));
        }
    }
}

static void CheckDivideNewRules(int64_t a, int64_t b)
{
    auto res = CScriptNum::fromInt(a);
    if (!res)
    {
        BOOST_CHECK(a == int64_t_min);
        return;
    }
    const CScriptNum scripta = *res;

    res = CScriptNum::fromInt(b);
    if (!res)
    {
        BOOST_CHECK(b == int64_t_min);
        return;
    }
    const CScriptNum scriptb = *res;

    if (b != 0)
    {
        // Prevent divide by 0
        auto val = scripta / scriptb;
        BOOST_CHECK(a / b == val.getint64());
        val = scripta / b;
        BOOST_CHECK(a / b == val.getint64());
    }
    if (a != 0)
    {
        // Prevent divide by 0
        auto val = scriptb / scripta;
        BOOST_CHECK(b / a == val.getint64());
        val = scriptb / a;
        BOOST_CHECK(b / a == val.getint64());
    }
}

static void CheckNegateOldRules(int64_t x)
{
    const CScriptNum10 bigx(x);
    const CScriptNum scriptx = CScriptNum::fromIntUnchecked(x);

    // -INT64_MIN is undefined
    if (x != int64_t_min)
    {
        BOOST_CHECK(verify(-bigx, -scriptx));
    }
}

static void CheckNegateNewRules(int64_t x)
{
    auto res = CScriptNum::fromInt(x);
    if (!res)
    {
        BOOST_CHECK(x == int64_t_min);
        return;
    }
    const CScriptNum scriptx = *res;
    CScriptNum10 const bigx(x);
    BOOST_CHECK(verify(-bigx, -scriptx));
    BOOST_CHECK(verify(-(-bigx), -(-scriptx)));
}

static void CheckCompare(const int64_t &num1, const int64_t &num2)
{
    const CScriptNum10 bignum1(num1);
    const CScriptNum10 bignum2(num2);
    const CScriptNum scriptnum1 = CScriptNum::fromIntUnchecked(num1);
    const CScriptNum scriptnum2 = CScriptNum::fromIntUnchecked(num2);

    BOOST_CHECK((bignum1 == bignum1) == (scriptnum1 == scriptnum1));
    BOOST_CHECK((bignum1 != bignum1) == (scriptnum1 != scriptnum1));
    BOOST_CHECK((bignum1 < bignum1) == (scriptnum1 < scriptnum1));
    BOOST_CHECK((bignum1 > bignum1) == (scriptnum1 > scriptnum1));
    BOOST_CHECK((bignum1 >= bignum1) == (scriptnum1 >= scriptnum1));
    BOOST_CHECK((bignum1 <= bignum1) == (scriptnum1 <= scriptnum1));

    BOOST_CHECK((bignum1 == bignum1) == (scriptnum1 == num1));
    BOOST_CHECK((bignum1 != bignum1) == (scriptnum1 != num1));
    BOOST_CHECK((bignum1 < bignum1) == (scriptnum1 < num1));
    BOOST_CHECK((bignum1 > bignum1) == (scriptnum1 > num1));
    BOOST_CHECK((bignum1 >= bignum1) == (scriptnum1 >= num1));
    BOOST_CHECK((bignum1 <= bignum1) == (scriptnum1 <= num1));

    BOOST_CHECK((bignum1 == bignum2) == (scriptnum1 == scriptnum2));
    BOOST_CHECK((bignum1 != bignum2) == (scriptnum1 != scriptnum2));
    BOOST_CHECK((bignum1 < bignum2) == (scriptnum1 < scriptnum2));
    BOOST_CHECK((bignum1 > bignum2) == (scriptnum1 > scriptnum2));
    BOOST_CHECK((bignum1 >= bignum2) == (scriptnum1 >= scriptnum2));
    BOOST_CHECK((bignum1 <= bignum2) == (scriptnum1 <= scriptnum2));

    BOOST_CHECK((bignum1 == bignum2) == (scriptnum1 == num2));
    BOOST_CHECK((bignum1 != bignum2) == (scriptnum1 != num2));
    BOOST_CHECK((bignum1 < bignum2) == (scriptnum1 < num2));
    BOOST_CHECK((bignum1 > bignum2) == (scriptnum1 > num2));
    BOOST_CHECK((bignum1 >= bignum2) == (scriptnum1 >= num2));
    BOOST_CHECK((bignum1 <= bignum2) == (scriptnum1 <= num2));
}

static void RunCreateOldRules(CScriptNum const &scriptx)
{
    size_t const maxIntegerSize = CScriptNum::MAXIMUM_ELEMENT_SIZE_32_BIT;
    int64_t const x = scriptx.getint64();
    CheckCreateIntOldRules(x);
    if (scriptx.getvch().size() <= maxIntegerSize)
    {
        CheckCreateVchOldRules(x);
    }
    else
    {
        BOOST_CHECK_THROW(CheckCreateVchOldRules(x), scriptnum10_error);
    }
}

static void RunCreateOldRulesSet(int64_t v, int64_t o)
{
    const CScriptNum value = CScriptNum::fromIntUnchecked(v);
    const CScriptNum offset = CScriptNum::fromIntUnchecked(o);
    RunCreateOldRules(value);
    auto res = value.safeAdd(offset);
    if (res)
    {
        RunCreateOldRules(*res);
    }
    res = value.safeSub(offset);
    if (res)
    {
        RunCreateOldRules(*res);
    }
}

static void RunCreateNewRules(CScriptNum const &scriptx)
{
    size_t const maxIntegerSize = CScriptNum::MAXIMUM_ELEMENT_SIZE_64_BIT;
    int64_t const x = scriptx.getint64();
    CheckCreateIntNewRules(x);
    if (scriptx.getvch().size() <= maxIntegerSize)
    {
        CheckCreateVchNewRules(x);
    }
    else
    {
        BOOST_CHECK_THROW(CheckCreateVchNewRules(x), scriptnum10_error);
    }
}

static void RunCreateNewRulesSet(int64_t v, int64_t o)
{
    auto res = CScriptNum::fromInt(v);
    if (!res)
    {
        BOOST_CHECK(v == int64_t_min);
        return;
    }
    const CScriptNum value = *res;
    res = CScriptNum::fromInt(o);
    if (!res)
    {
        BOOST_CHECK(o == int64_t_min);
        return;
    }
    const CScriptNum offset = *res;
    RunCreateNewRules(value);
    res = value.safeAdd(offset);
    if (res)
    {
        RunCreateNewRules(*res);
    }
    res = value.safeSub(offset);
    if (res)
    {
        RunCreateNewRules(*res);
    }
}

static void RunOperators(int64_t a, int64_t b)
{
    CheckAddOldRules(a, b);
    CheckAddNewRules(a, b);
    CheckSubtractOldRules(a, b);
    CheckSubtractNewRules(a, b);
    CheckMultiply(a, b);
    CheckDivideOldRules(a, b);
    CheckDivideNewRules(a, b);
    CheckNegateOldRules(a);
    CheckNegateNewRules(a);
    CheckCompare(a, b);
}

BOOST_AUTO_TEST_CASE(creation)
{
    for (auto value : values)
    {
        for (auto offset : offsets)
        {
            RunCreateOldRulesSet(value, offset);
            RunCreateNewRulesSet(value, offset);
        }
    }
}

// Prevent potential UB
int64_t negate(int64_t x) { return x != int64_t_min ? -x : int64_t_min; };

BOOST_AUTO_TEST_CASE(operators)
{
    for (auto a : values)
    {
        RunOperators(a, a);
        RunOperators(a, negate(a));
        for (auto b : values)
        {
            RunOperators(a, b);
            RunOperators(a, negate(b));
        }
    }
}

static void CheckMinimalyEncode(std::vector<uint8_t> data, const std::vector<uint8_t> &expected)
{
    bool alreadyEncoded = CScriptNum::IsMinimallyEncoded(data, data.size());
    bool hasEncoded = CScriptNum::MinimallyEncode(data);
    BOOST_CHECK_EQUAL(hasEncoded, !alreadyEncoded);
    BOOST_CHECK(data == expected);
}

BOOST_AUTO_TEST_CASE(minimize_encoding_test)
{
    CheckMinimalyEncode({}, {});

    // Check that positive and negative zeros encode to nothing.
    std::vector<uint8_t> zero, negZero;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE; i++)
    {
        zero.push_back(0x00);
        CheckMinimalyEncode(zero, {});

        negZero.push_back(0x80);
        CheckMinimalyEncode(negZero, {});

        // prepare for next round.
        negZero[negZero.size() - 1] = 0x00;
    }

    // Keep one leading zero when sign bit is used.
    std::vector<uint8_t> n{0x80, 0x00}, negn{0x80, 0x80};
    std::vector<uint8_t> npadded = n, negnpadded = negn;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE; i++)
    {
        CheckMinimalyEncode(npadded, n);
        npadded.push_back(0x00);

        CheckMinimalyEncode(negnpadded, negn);
        negnpadded[negnpadded.size() - 1] = 0x00;
        negnpadded.push_back(0x80);
    }

    // Mege leading byte when sign bit isn't used.
    std::vector<uint8_t> k{0x7f}, negk{0xff};
    std::vector<uint8_t> kpadded = k, negkpadded = negk;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE; i++)
    {
        CheckMinimalyEncode(kpadded, k);
        kpadded.push_back(0x00);

        CheckMinimalyEncode(negkpadded, negk);
        negkpadded[negkpadded.size() - 1] &= 0x7f;
        negkpadded.push_back(0x80);
    }
}

BOOST_AUTO_TEST_SUITE_END()
