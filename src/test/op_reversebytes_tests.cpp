// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/script.h>

#include <test/lcg.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

typedef std::vector<uint8_t> valtype;
typedef std::vector<valtype> stacktype;

BOOST_FIXTURE_TEST_SUITE(op_reversebytes_tests, BasicTestingSetup)

static void CheckErrorWithFlags(const uint32_t flags,
    const stacktype &original_stack,
    const CScript &script,
    const ScriptError expected)
{
    BaseSignatureChecker sigchecker;
    ScriptError err = SCRIPT_ERR_OK;
    stacktype stack{original_stack};
    bool r = EvalScript(stack, script, flags, MAX_OPS_PER_SCRIPT, sigchecker, &err);
    BOOST_CHECK(!r);
    BOOST_CHECK(err == expected);
}

static void CheckPassWithFlags(const uint32_t flags,
    const stacktype &original_stack,
    const CScript &script,
    const stacktype &expected)
{
    BaseSignatureChecker sigchecker;
    ScriptError err = SCRIPT_ERR_OK;
    stacktype stack{original_stack};
    bool r = EvalScript(stack, script, flags, MAX_OPS_PER_SCRIPT, sigchecker, &err);
    BOOST_CHECK(r);
    BOOST_CHECK(err == SCRIPT_ERR_OK);
    BOOST_CHECK(stack == expected);
}

/**
 * Verifies that the given error occurs with OP_REVERSEBYTES enabled
 * and that BAD_OPCODE occurs if disabled.
 */
static void CheckErrorIfEnabled(const uint32_t flags,
    const stacktype &original_stack,
    const CScript &script,
    const ScriptError expected)
{
    CheckErrorWithFlags(flags | SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack, script, expected);
    CheckErrorWithFlags(flags & ~SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack, script, SCRIPT_ERR_BAD_OPCODE);
}

/**
 * Verifies that the given stack results with OP_REVERSEBYTES enabled
 * and that BAD_OPCODE occurs if disabled.
 */
static void CheckPassIfEnabled(const uint32_t flags,
    const stacktype &original_stack,
    const CScript &script,
    const stacktype &expected)
{
    CheckPassWithFlags(flags | SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack, script, expected);
    CheckErrorWithFlags(flags & ~SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack, script, SCRIPT_ERR_BAD_OPCODE);
}

/**
 * Verifies the different combinations of a given test case.
 * Checks if
 * - <item> OP_REVERSEBYTES results in <reversed_item>,
 * - <reversed_item> OP_REVERSEBYTES results in <item>,
 * - <item> {OP_REVERSEBYTES} x 2 results in <item> and
 * - <reversed_item> {OP_REVERSEBYTES} x 2 results in <reversed_item>.
 */
static void CheckPassForCombinations(const uint32_t flags, const valtype &item, const valtype &reversed_item)
{
    CheckPassIfEnabled(flags, {item}, CScript() << OP_REVERSEBYTES, {reversed_item});
    CheckPassIfEnabled(flags, {reversed_item}, CScript() << OP_REVERSEBYTES, {item});
    CheckPassIfEnabled(flags, {item}, CScript() << OP_REVERSEBYTES << OP_REVERSEBYTES, {item});
    CheckPassIfEnabled(flags, {reversed_item}, CScript() << OP_REVERSEBYTES << OP_REVERSEBYTES, {reversed_item});
}

// Test a few simple manual cases with random flags (proxy for exhaustive
// testing).
BOOST_AUTO_TEST_CASE(op_reversebytes_manual_random_flags)
{
    MMIXLinearCongruentialGenerator lcg;
    for (size_t i = 0; i < 4096; i++)
    {
        uint32_t flags = lcg.next();
        CheckPassForCombinations(flags, {}, {});
        CheckPassForCombinations(flags, {99}, {99});
        CheckPassForCombinations(flags, {0xde, 0xad}, {0xad, 0xde});
        CheckPassForCombinations(flags, {0xde, 0xad, 0xa1}, {0xa1, 0xad, 0xde});
        CheckPassForCombinations(flags, {0xde, 0xad, 0xbe, 0xef}, {0xef, 0xbe, 0xad, 0xde});
        CheckPassForCombinations(flags, {0x12, 0x34, 0x56}, {0x56, 0x34, 0x12});
    }
}


BOOST_AUTO_TEST_CASE(op_reversebytes_iota)
{
    MMIXLinearCongruentialGenerator lcg;
    for (uint32_t datasize : {0, 1, 2, 10, 16, 32, 50, 128, 300, 400, 512, 519, 520})
    {
        valtype iota_data;
        iota_data.reserve(datasize);
        for (size_t item = 0; item < datasize; ++item)
        {
            iota_data.emplace_back(item % 256);
        }
        valtype iota_data_reversed = {iota_data.rbegin(), iota_data.rend()};
        for (size_t i = 0; i < 4096; i++)
        {
            uint32_t flags = lcg.next();
            CheckPassForCombinations(flags, iota_data, iota_data_reversed);
        }
    }
}

BOOST_AUTO_TEST_CASE(op_reversebytes_random_and_palindrome)
{
    MMIXLinearCongruentialGenerator lcg;

    // Prepare a couple of interesting script flags.
    std::vector<uint32_t> flaglist({
        SCRIPT_VERIFY_NONE, STANDARD_SCRIPT_VERIFY_FLAGS, MANDATORY_SCRIPT_VERIFY_FLAGS,
    });
    for (uint32_t flagindex = 0; flagindex < 32; ++flagindex)
    {
        uint32_t flags = 1 << flagindex;
        flaglist.push_back(flags);
    }

    // Test every possible stack item size.
    for (uint32_t datasize = 0; datasize < MAX_SCRIPT_ELEMENT_SIZE; ++datasize)
    {
        // Generate random data.
        valtype random_data;
        random_data.reserve(datasize);
        for (size_t item = 0; item < datasize; ++item)
        {
            random_data.emplace_back(lcg.next() % 256);
        }
        valtype random_data_reversed = {random_data.rbegin(), random_data.rend()};

        // Make a palindrome of the form 0..n..0.
        valtype palindrome;
        palindrome.reserve(datasize);
        for (size_t item = 0; item < datasize; ++item)
        {
            palindrome.emplace_back((item < (datasize + 1) / 2 ? item : datasize - item - 1) % 256);
        }

        for (const uint32_t flags : flaglist)
        {
            // Verify random data passes.
            CheckPassForCombinations(flags, random_data, random_data_reversed);
            // Verify palindrome check passes.
            CheckPassIfEnabled(flags, {palindrome}, CScript() << OP_REVERSEBYTES, {palindrome});

            // Test empty stack results in INVALID_STACK_OPERATION.
            CheckErrorIfEnabled(flags, {}, CScript() << OP_REVERSEBYTES, SCRIPT_ERR_INVALID_STACK_OPERATION);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
