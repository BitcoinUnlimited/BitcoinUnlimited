// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_bitcoin.h"

#include "policy/policy.h"
#include "script/interpreter.h"

#include <boost/test/unit_test.hpp>

#include <array>

typedef std::vector<uint8_t> valtype;
typedef std::vector<valtype> stacktype;

std::array<uint32_t, 3> flagset{0, STANDARD_SCRIPT_VERIFY_FLAGS,
                                MANDATORY_SCRIPT_VERIFY_FLAGS};

BOOST_FIXTURE_TEST_SUITE(may152018_opcodes_tests, BasicTestingSetup)

/**
 * General utility functions to check for script passing/failing.
 */
static void CheckTestResultForAllFlags(const stacktype &original_stack,
                                       const CScript &script,
                                       const stacktype &expected) {
    BaseSignatureChecker sigchecker;

    for (uint32_t flags : flagset) {
        ScriptError err = SCRIPT_ERR_OK;
        stacktype stack{original_stack};
        bool r =
            EvalScript(stack, script, flags | SCRIPT_ENABLE_MAY152018_OPCODES,
                       sigchecker, &err);
        BOOST_CHECK(r);
        BOOST_CHECK(stack == expected);

        // Make sure that if we do not pass the may152018 flag, opcodes are still
        // disabled.
        stack = original_stack;
        r = EvalScript(stack, script, flags, sigchecker, &err);
        BOOST_CHECK(!r);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_DISABLED_OPCODE);
    }
}

static void CheckError(uint32_t flags, const stacktype &original_stack,
                       const CScript &script, ScriptError expected_error) {
    BaseSignatureChecker sigchecker;
    ScriptError err = SCRIPT_ERR_OK;
    stacktype stack{original_stack};
    bool r = EvalScript(stack, script, flags | SCRIPT_ENABLE_MAY152018_OPCODES,
                        sigchecker, &err);
    BOOST_CHECK(!r);
    BOOST_CHECK_EQUAL(err, expected_error);

    // Make sure that if we do not pass the may152018 flag, opcodes are still
    // disabled.
    stack = original_stack;
    r = EvalScript(stack, script, flags, sigchecker, &err);
    BOOST_CHECK(!r);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_DISABLED_OPCODE);
}

static void CheckErrorForAllFlags(const stacktype &original_stack,
                                  const CScript &script,
                                  ScriptError expected_error) {
    for (uint32_t flags : flagset) {
        CheckError(flags, original_stack, script, expected_error);
    }
}

static void CheckOpError(const stacktype &original_stack, opcodetype op,
                         ScriptError expected_error) {
    CheckErrorForAllFlags(original_stack, CScript() << op, expected_error);
}

static void CheckAllBitwiseOpErrors(const stacktype &stack,
                                    ScriptError expected_error) {
    CheckOpError(stack, OP_AND, expected_error);
    CheckOpError(stack, OP_OR, expected_error);
    CheckOpError(stack, OP_XOR, expected_error);
}

static void CheckBinaryOp(const valtype &a, const valtype &b, opcodetype op,
                          const valtype &expected) {
    CheckTestResultForAllFlags({a, b}, CScript() << op, {expected});
}

static valtype NegativeValtype(const valtype &v) {
    valtype r(v);
    if (r.size() > 0) {
        r[r.size() - 1] ^= 0x80;
    }
    CScriptNum::MinimallyEncode(r);
    return r;
}

BOOST_AUTO_TEST_CASE(negative_valtype_test) {
    // Test zero values
    BOOST_CHECK(NegativeValtype({}) == valtype{});
    BOOST_CHECK(NegativeValtype({0x00}) == valtype{});
    BOOST_CHECK(NegativeValtype({0x80}) == valtype{});
    BOOST_CHECK(NegativeValtype({0x00, 0x00}) == valtype{});
    BOOST_CHECK(NegativeValtype({0x00, 0x80}) == valtype{});

    // Non-zero values
    BOOST_CHECK(NegativeValtype({0x01}) == valtype{0x81});
    BOOST_CHECK(NegativeValtype({0x81}) == valtype{0x01});
    BOOST_CHECK(NegativeValtype({0x02, 0x01}) == (valtype{0x02, 0x81}));
    BOOST_CHECK(NegativeValtype({0x02, 0x81}) == (valtype{0x02, 0x01}));
    BOOST_CHECK(NegativeValtype({0xff, 0x02, 0x01}) ==
                (valtype{0xff, 0x02, 0x81}));
    BOOST_CHECK(NegativeValtype({0xff, 0x02, 0x81}) ==
                (valtype{0xff, 0x02, 0x01}));
    BOOST_CHECK(NegativeValtype({0xff, 0xff, 0x02, 0x01}) ==
                (valtype{0xff, 0xff, 0x02, 0x81}));
    BOOST_CHECK(NegativeValtype({0xff, 0xff, 0x02, 0x81}) ==
                (valtype{0xff, 0xff, 0x02, 0x01}));

    // Should not be overly-minimized
    BOOST_CHECK(NegativeValtype({0xff, 0x80}) == (valtype{0xff, 0x00}));
    BOOST_CHECK(NegativeValtype({0xff, 0x00}) == (valtype{0xff, 0x80}));
}

/**
 * Bitwise Opcodes
 */
static void RunTestForAllBitwiseOpcodes(const valtype &a, const valtype &b,
                                        const valtype &expected_and,
                                        const valtype &expected_or,
                                        const valtype &expected_xor) {
    // Bitwise ops are commutative, so we check both ways.
    CheckBinaryOp(a, b, OP_AND, expected_and);
    CheckBinaryOp(b, a, OP_AND, expected_and);
    CheckBinaryOp(a, b, OP_OR, expected_or);
    CheckBinaryOp(b, a, OP_OR, expected_or);
    CheckBinaryOp(a, b, OP_XOR, expected_xor);
    CheckBinaryOp(b, a, OP_XOR, expected_xor);
}

static void RunTestForAllBitwiseOpcodesSizes(const valtype &a, const valtype &b,
                                             const valtype &expected_and,
                                             const valtype &expected_or,
                                             const valtype &expected_xor) {
    valtype wa, wb, wand, wor, wxor;
    for (size_t i = 0; i < a.size(); i++) {
        wa.push_back(a[i]);
        wb.push_back(b[i]);
        wand.push_back(expected_and[i]);
        wor.push_back(expected_or[i]);
        wxor.push_back(expected_xor[i]);

        RunTestForAllBitwiseOpcodes(wa, wb, wand, wor, wxor);
    }
}

static void TestBitwiseOpcodes(const valtype &a, const valtype &b,
                               const valtype &expected_and,
                               const valtype &expected_or) {
    valtype expected_xor(expected_and.size());
    for (size_t i = 0; i < a.size(); i++) {
        // A ^ B = (A | B) & ~(A & B)
        expected_xor[i] = expected_or[i] & ~expected_and[i];
    }

    RunTestForAllBitwiseOpcodesSizes(a, b, expected_and, expected_or,
                                     expected_xor);

    valtype nota(a.size());
    valtype notb(b.size());
    valtype nand(expected_and.size());
    valtype nor(expected_or.size());
    for (size_t i = 0; i < a.size(); i++) {
        nota[i] = ~a[i];
        notb[i] = ~b[i];
        nand[i] = ~expected_and[i];
        nor[i] = ~expected_or[i];
    }

    // ~A & ~B == ~(A | B)
    // ~A | ~B == ~(A & B)
    // ~A ^ ~B == A ^ B
    RunTestForAllBitwiseOpcodesSizes(nota, notb, nor, nand, expected_xor);
}

BOOST_AUTO_TEST_CASE(bitwise_opcodes_test) {
    // Check that empty ops works.
    RunTestForAllBitwiseOpcodes({}, {}, {}, {}, {});

    // Run all variations of zeros and ones.
    valtype allzeros(MAX_SCRIPT_ELEMENT_SIZE, 0);
    valtype allones(MAX_SCRIPT_ELEMENT_SIZE, 0xff);

    BOOST_CHECK_EQUAL(allzeros.size(), MAX_SCRIPT_ELEMENT_SIZE);
    BOOST_CHECK_EQUAL(allones.size(), MAX_SCRIPT_ELEMENT_SIZE);

    TestBitwiseOpcodes(allzeros, allzeros, allzeros, allzeros);
    TestBitwiseOpcodes(allzeros, allones, allzeros, allones);
    TestBitwiseOpcodes(allones, allones, allones, allones);

    // Let's use two random a and b.
    valtype a{
        0x34, 0x0e, 0x7e, 0x17, 0x83, 0x66, 0x1a, 0x81, 0x45, 0x8d, 0x26, 0x26,
        0xbc, 0xbd, 0x56, 0xe7, 0xf2, 0x1c, 0xec, 0xf6, 0x79, 0x8c, 0x3e, 0x58,
        0x0f, 0x86, 0xcf, 0x53, 0xbe, 0x66, 0x8f, 0xa7, 0xbe, 0xf6, 0x30, 0x12,
        0x8d, 0x01, 0x00, 0x37, 0x7f, 0x5b, 0x64, 0x50, 0x63, 0x40, 0x6a, 0x44,
        0xf5, 0x7e, 0x02, 0xc7, 0xab, 0x45, 0xcf, 0x6a, 0x98, 0x61, 0xe8, 0xb8,
        0xc4, 0x9e, 0x11, 0xe8, 0x30, 0x71, 0x07, 0x73, 0xa2, 0x4d, 0xdd, 0xa6,
        0x6c, 0xf4, 0x2a, 0x22, 0xa0, 0xac, 0xdc, 0xf4, 0xcc, 0xfb, 0x4d, 0xe3,
        0x55, 0xde, 0x44, 0x46, 0x32, 0x36, 0x93, 0xb4, 0xd9, 0xd1, 0x3b, 0x06,
        0x09, 0x6a, 0x64, 0xc3, 0x18, 0x58, 0xc4, 0x9f, 0x1b, 0x6a, 0xa3, 0xab,
        0x59, 0x37, 0xbd, 0x36, 0x97, 0x35, 0x26, 0x87, 0x63, 0x58, 0x08, 0x6e,
        0x5e, 0x46, 0xcf, 0x15, 0x33, 0xfc, 0x46, 0x45, 0x97, 0x61, 0x4b, 0xb8,
        0xec, 0xdd, 0x1b, 0x69, 0x6e, 0x8a, 0x27, 0xf9, 0xcd, 0x4b, 0x5c, 0xa4,
        0x84, 0x18, 0xd5, 0x23, 0x50, 0xc6, 0x63, 0xbe, 0xca, 0xd3, 0xd0, 0x91,
        0x39, 0x16, 0x6a, 0x6e, 0xd6, 0x09, 0x18, 0x52, 0x05, 0x6a, 0xa7, 0xf7,
        0x64, 0xa3, 0xf0, 0xba, 0x75, 0xc5, 0x9c, 0xf7, 0xbb, 0x70, 0x68, 0x65,
        0x4f, 0xdb, 0xd0, 0x36, 0x14, 0xfb, 0x1a, 0xf6, 0x6e, 0xea, 0x8d, 0xc8,
        0xa5, 0xad, 0x61, 0xc6, 0x04, 0x4c, 0xc3, 0xb9, 0x68, 0x8c, 0xa4, 0xe4,
        0x04, 0xae, 0xee, 0xca, 0xe7, 0x52, 0xa7, 0xba, 0x16, 0x91, 0x26, 0x9b,
        0xae, 0x31, 0xcd, 0x6f, 0x4e, 0x7e, 0x47, 0x60, 0x40, 0xf0, 0xbc, 0xe2,
        0x20, 0xaf, 0xc1, 0x4f, 0x26, 0x54, 0x93, 0x37, 0xfc, 0xbf, 0x50, 0xd3,
        0xf2, 0x30, 0x70, 0xfc, 0x67, 0x15, 0x82, 0xd3, 0x39, 0x27, 0xa2, 0x4f,
        0xce, 0x10, 0xed, 0x11, 0x73, 0xc4, 0x48, 0xe9, 0x65, 0xa1, 0x5e, 0xf2,
        0x0c, 0x81, 0x3b, 0x80, 0xe1, 0x9f, 0x53, 0x31, 0x49, 0x73, 0xc8, 0x0a,
        0x6e, 0xa4, 0xe1, 0xe1, 0xe2, 0xac, 0xeb, 0x0b, 0xa5, 0x4b, 0xc5, 0x47,
        0xf6, 0xf1, 0x15, 0x10, 0x31, 0xf0, 0xcb, 0x6f, 0xed, 0xd3, 0x50, 0x7d,
        0xb2, 0x86, 0x87, 0xab, 0x62, 0x5c, 0x4c, 0x4b, 0xb0, 0x0a, 0x20, 0x19,
        0xb9, 0x8c, 0x1a, 0xf5, 0xe6, 0x29, 0xa0, 0x8a, 0x55, 0x88, 0xa0, 0xf5,
        0xef, 0xe6, 0x50, 0x6d, 0x36, 0x7b, 0x75, 0xe5, 0x14, 0xc8, 0xfb, 0xc6,
        0x5b, 0xe7, 0x99, 0x37, 0x62, 0x56, 0xdb, 0x8f, 0x40, 0x43, 0x54, 0x8d,
        0x68, 0x19, 0xc2, 0xf5, 0xc0, 0x37, 0xed, 0xee, 0x0e, 0xab, 0x0b, 0x77,
        0x29, 0x27, 0xac, 0x07, 0x70, 0xfa, 0xa9, 0x69, 0x28, 0x51, 0xf5, 0x65,
        0x58, 0x7a, 0xcc, 0xc9, 0xfe, 0x3c, 0xa0, 0x0d, 0x6e, 0x87, 0x38, 0x36,
        0xb7, 0x1a, 0x41, 0x6c, 0x9a, 0x13, 0xfa, 0x86, 0x13, 0xe6, 0xc9, 0xec,
        0x9f, 0x50, 0x15, 0xc3, 0x74, 0x4c, 0x29, 0x67, 0x0a, 0xa7, 0x7e, 0x7f,
        0x3c, 0xab, 0xe9, 0x44, 0x61, 0x6e, 0x64, 0x50, 0x47, 0x1e, 0x17, 0x23,
        0x64, 0x29, 0x9c, 0x9c, 0xef, 0x5b, 0x28, 0xe3, 0x0e, 0xa5, 0x2a, 0x2f,
        0x2d, 0xc6, 0x6c, 0xd3, 0xaa, 0x03, 0x48, 0x15, 0x0c, 0x92, 0x80, 0x86,
        0x2f, 0xc2, 0xbd, 0x5e, 0x82, 0x61, 0xa1, 0x88, 0xdd, 0x5e, 0xea, 0xef,
        0x19, 0xf9, 0x84, 0x66, 0xf7, 0xbb, 0x44, 0xad, 0xf9, 0xf7, 0x2f, 0x2a,
        0xd5, 0x37, 0xef, 0x28, 0x3d, 0x1a, 0xdc, 0x6c, 0xf1, 0xcc, 0xca, 0xd5,
        0x2b, 0x58, 0x63, 0xc0, 0x34, 0x91, 0x87, 0xd9, 0x36, 0x2f, 0x90, 0xeb,
        0xf1, 0xde, 0x8b, 0x8c, 0x20, 0x51, 0x83, 0xfd, 0xf4, 0xfd, 0xe7, 0x40,
        0x68, 0xf3, 0x5a, 0x17, 0x80, 0x21, 0xf3, 0xc1, 0x90, 0x3c, 0x75, 0x23,
        0x48, 0x1c, 0x98, 0xb5};
    valtype b{
        0xd2, 0x9e, 0x99, 0xc9, 0xe7, 0x11, 0x7b, 0x0e, 0x4b, 0x8e, 0x11, 0x08,
        0xd1, 0x5c, 0xf4, 0xb8, 0x2c, 0x14, 0x3f, 0x45, 0x75, 0xe9, 0x8a, 0xeb,
        0x81, 0xf8, 0xd8, 0xa3, 0x8e, 0x4b, 0x63, 0x0e, 0x7f, 0x1e, 0xfd, 0x84,
        0x83, 0x7c, 0x26, 0x1f, 0xf0, 0xc9, 0x37, 0x1c, 0x5f, 0xf5, 0xf3, 0x3d,
        0x67, 0x2b, 0x27, 0x30, 0xdb, 0x3e, 0xe7, 0x2f, 0x7b, 0x7d, 0x1c, 0x40,
        0x06, 0x2a, 0x72, 0x5a, 0x37, 0x0c, 0xd5, 0xa8, 0xa3, 0x81, 0xd4, 0x73,
        0xef, 0x1e, 0x4e, 0x6c, 0xb9, 0x10, 0x3d, 0x04, 0x6e, 0xca, 0xe7, 0xdf,
        0x62, 0x7b, 0x64, 0x00, 0x6a, 0xb6, 0xda, 0x02, 0x96, 0x74, 0xa7, 0xc2,
        0xbb, 0x28, 0x69, 0xdf, 0xc8, 0x09, 0xff, 0x6c, 0x6f, 0x7a, 0xf8, 0x82,
        0x69, 0xf1, 0x59, 0xf8, 0x3d, 0xe0, 0x6d, 0xa5, 0x71, 0xfb, 0x39, 0x2e,
        0x17, 0x51, 0xcb, 0x94, 0x2a, 0xd0, 0x4e, 0x02, 0xaf, 0xa5, 0xd5, 0x39,
        0x56, 0xda, 0x10, 0x2e, 0xa2, 0x91, 0x0b, 0xd2, 0xca, 0xb1, 0xac, 0x6d,
        0xd2, 0xef, 0xad, 0x59, 0x54, 0xbc, 0xd3, 0x44, 0x4c, 0x6c, 0xe2, 0x5c,
        0xed, 0xab, 0xc0, 0x04, 0x6d, 0x3e, 0x92, 0xf9, 0x4a, 0xce, 0x76, 0xed,
        0x45, 0x50, 0x93, 0x29, 0x17, 0x93, 0x9c, 0xf0, 0xd8, 0x3c, 0xcd, 0xf7,
        0x52, 0x9f, 0x27, 0x57, 0x2a, 0xff, 0xe0, 0x33, 0xb6, 0xa4, 0x41, 0xa3,
        0x35, 0x0b, 0xab, 0x0c, 0x0b, 0xdd, 0x98, 0x10, 0x1d, 0x97, 0x24, 0x7a,
        0x8e, 0xcb, 0xa3, 0x7a, 0xe9, 0xa8, 0x73, 0xf4, 0x4a, 0x4c, 0x6b, 0xb7,
        0x31, 0x65, 0xca, 0x5a, 0xc4, 0xd8, 0x3c, 0xe0, 0xad, 0x30, 0x2a, 0x2e,
        0x34, 0x2e, 0x40, 0x84, 0xdd, 0x5d, 0x08, 0xed, 0x10, 0x12, 0xca, 0x3f,
        0x24, 0x2d, 0x08, 0x5b, 0x86, 0xb6, 0xf4, 0x70, 0x00, 0x5c, 0x9d, 0x30,
        0x2a, 0x81, 0xd2, 0x5c, 0xa1, 0x70, 0xcf, 0x99, 0x0f, 0xf5, 0x94, 0xef,
        0x54, 0x1d, 0xab, 0x91, 0x24, 0x59, 0x4f, 0xf6, 0xcb, 0xb8, 0x6d, 0x14,
        0x21, 0xf1, 0xfb, 0x14, 0x5c, 0x29, 0x4e, 0x6e, 0xb0, 0x4d, 0x64, 0x0c,
        0x38, 0xee, 0x19, 0x63, 0x14, 0x9b, 0x3d, 0xb4, 0x19, 0x25, 0x91, 0xe6,
        0xde, 0xf4, 0x34, 0x2b, 0x87, 0x99, 0xbd, 0xec, 0x1c, 0xd3, 0x92, 0x34,
        0xb7, 0xba, 0xef, 0x00, 0xae, 0xdc, 0xec, 0x9d, 0xd1, 0xfa, 0x83, 0x9f,
        0x95, 0x8d, 0xb0, 0xed, 0xc0, 0x67, 0xae, 0xce, 0x15, 0xdb, 0x28, 0x8b,
        0x8f, 0xcb, 0xc4, 0x9b, 0x0d, 0x46, 0x67, 0x96, 0xb0, 0x86, 0xb2, 0xdb,
        0x3c, 0x89, 0x6e, 0x57, 0xac, 0xcb, 0x34, 0x57, 0x37, 0x80, 0x00, 0x34,
        0x78, 0x71, 0xf0, 0x1a, 0x2c, 0x28, 0x87, 0x9f, 0x08, 0x21, 0x7c, 0x0e,
        0x7e, 0x29, 0xfb, 0x9a, 0x2c, 0x77, 0x48, 0x2f, 0x88, 0xe2, 0xf0, 0x6a,
        0x87, 0x15, 0x0c, 0x4c, 0xbf, 0xcb, 0xdd, 0xee, 0x75, 0xe1, 0xbc, 0x38,
        0x31, 0xdc, 0xe9, 0x61, 0x53, 0x1e, 0xc8, 0x4b, 0x80, 0x94, 0x5c, 0x03,
        0xdd, 0x4b, 0xae, 0xa8, 0x54, 0xe9, 0x8b, 0x23, 0x20, 0x21, 0xc8, 0x03,
        0x83, 0x33, 0x5f, 0x11, 0x37, 0xfc, 0xd5, 0xb3, 0x11, 0x9a, 0x06, 0x0d,
        0xbf, 0xcd, 0xc7, 0x22, 0x88, 0xb8, 0xc9, 0x3f, 0xec, 0x7c, 0x11, 0x96,
        0x6a, 0xa0, 0x57, 0xdf, 0x5b, 0xde, 0xa2, 0x09, 0x11, 0xd3, 0xfd, 0xbf,
        0x84, 0x7a, 0x9d, 0x3a, 0xba, 0x0f, 0x6d, 0x01, 0xad, 0xbc, 0xb9, 0xd8,
        0x8a, 0xe4, 0xd6, 0xa2, 0x04, 0x93, 0xe0, 0x02, 0xd2, 0x45, 0x49, 0x14,
        0x8e, 0x84, 0x9c, 0x7c, 0x57, 0x1b, 0x05, 0x27, 0xf6, 0x59, 0x83, 0xd1,
        0xf4, 0xb6, 0x2f, 0xbe, 0x6e, 0x35, 0x7e, 0x97, 0x10, 0xf5, 0x42, 0x1a,
        0xc9, 0x4d, 0xb9, 0x07, 0x71, 0x6d, 0xd1, 0x96, 0xc3, 0x88, 0xb6, 0xe6,
        0x0e, 0x8a, 0x8a, 0xd7};

    BOOST_CHECK_EQUAL(a.size(), MAX_SCRIPT_ELEMENT_SIZE);
    BOOST_CHECK_EQUAL(b.size(), MAX_SCRIPT_ELEMENT_SIZE);

    valtype aandb{
        0x10, 0x0e, 0x18, 0x01, 0x83, 0x00, 0x1a, 0x00, 0x41, 0x8c, 0x00, 0x00,
        0x90, 0x1c, 0x54, 0xa0, 0x20, 0x14, 0x2c, 0x44, 0x71, 0x88, 0x0a, 0x48,
        0x01, 0x80, 0xc8, 0x03, 0x8e, 0x42, 0x03, 0x06, 0x3e, 0x16, 0x30, 0x00,
        0x81, 0x00, 0x00, 0x17, 0x70, 0x49, 0x24, 0x10, 0x43, 0x40, 0x62, 0x04,
        0x65, 0x2a, 0x02, 0x00, 0x8b, 0x04, 0xc7, 0x2a, 0x18, 0x61, 0x08, 0x00,
        0x04, 0x0a, 0x10, 0x48, 0x30, 0x00, 0x05, 0x20, 0xa2, 0x01, 0xd4, 0x22,
        0x6c, 0x14, 0x0a, 0x20, 0xa0, 0x00, 0x1c, 0x04, 0x4c, 0xca, 0x45, 0xc3,
        0x40, 0x5a, 0x44, 0x00, 0x22, 0x36, 0x92, 0x00, 0x90, 0x50, 0x23, 0x02,
        0x09, 0x28, 0x60, 0xc3, 0x08, 0x08, 0xc4, 0x0c, 0x0b, 0x6a, 0xa0, 0x82,
        0x49, 0x31, 0x19, 0x30, 0x15, 0x20, 0x24, 0x85, 0x61, 0x58, 0x08, 0x2e,
        0x16, 0x40, 0xcb, 0x14, 0x22, 0xd0, 0x46, 0x00, 0x87, 0x21, 0x41, 0x38,
        0x44, 0xd8, 0x10, 0x28, 0x22, 0x80, 0x03, 0xd0, 0xc8, 0x01, 0x0c, 0x24,
        0x80, 0x08, 0x85, 0x01, 0x50, 0x84, 0x43, 0x04, 0x48, 0x40, 0xc0, 0x10,
        0x29, 0x02, 0x40, 0x04, 0x44, 0x08, 0x10, 0x50, 0x00, 0x4a, 0x26, 0xe5,
        0x44, 0x00, 0x90, 0x28, 0x15, 0x81, 0x9c, 0xf0, 0x98, 0x30, 0x48, 0x65,
        0x42, 0x9b, 0x00, 0x16, 0x00, 0xfb, 0x00, 0x32, 0x26, 0xa0, 0x01, 0x80,
        0x25, 0x09, 0x21, 0x04, 0x00, 0x4c, 0x80, 0x10, 0x08, 0x84, 0x24, 0x60,
        0x04, 0x8a, 0xa2, 0x4a, 0xe1, 0x00, 0x23, 0xb0, 0x02, 0x00, 0x22, 0x93,
        0x20, 0x21, 0xc8, 0x4a, 0x44, 0x58, 0x04, 0x60, 0x00, 0x30, 0x28, 0x22,
        0x20, 0x2e, 0x40, 0x04, 0x04, 0x54, 0x00, 0x25, 0x10, 0x12, 0x40, 0x13,
        0x20, 0x20, 0x00, 0x58, 0x06, 0x14, 0x80, 0x50, 0x00, 0x04, 0x80, 0x00,
        0x0a, 0x00, 0xc0, 0x10, 0x21, 0x40, 0x48, 0x89, 0x05, 0xa1, 0x14, 0xe2,
        0x04, 0x01, 0x2b, 0x80, 0x20, 0x19, 0x43, 0x30, 0x49, 0x30, 0x48, 0x00,
        0x20, 0xa0, 0xe1, 0x00, 0x40, 0x28, 0x4a, 0x0a, 0xa0, 0x49, 0x44, 0x04,
        0x30, 0xe0, 0x11, 0x00, 0x10, 0x90, 0x09, 0x24, 0x09, 0x01, 0x10, 0x64,
        0x92, 0x84, 0x04, 0x2b, 0x02, 0x18, 0x0c, 0x48, 0x10, 0x02, 0x00, 0x10,
        0xb1, 0x88, 0x0a, 0x00, 0xa6, 0x08, 0xa0, 0x88, 0x51, 0x88, 0x80, 0x95,
        0x85, 0x84, 0x10, 0x6d, 0x00, 0x63, 0x24, 0xc4, 0x14, 0xc8, 0x28, 0x82,
        0x0b, 0xc3, 0x80, 0x13, 0x00, 0x46, 0x43, 0x86, 0x00, 0x02, 0x10, 0x89,
        0x28, 0x09, 0x42, 0x55, 0x80, 0x03, 0x24, 0x46, 0x06, 0x80, 0x00, 0x34,
        0x28, 0x21, 0xa0, 0x02, 0x20, 0x28, 0x81, 0x09, 0x08, 0x01, 0x74, 0x04,
        0x58, 0x28, 0xc8, 0x88, 0x2c, 0x34, 0x00, 0x0d, 0x08, 0x82, 0x30, 0x22,
        0x87, 0x10, 0x00, 0x4c, 0x9a, 0x03, 0xd8, 0x86, 0x11, 0xe0, 0x88, 0x28,
        0x11, 0x50, 0x01, 0x41, 0x50, 0x0c, 0x08, 0x43, 0x00, 0x84, 0x5c, 0x03,
        0x1c, 0x0b, 0xa8, 0x00, 0x40, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
        0x00, 0x21, 0x1c, 0x10, 0x27, 0x58, 0x00, 0xa3, 0x00, 0x80, 0x02, 0x0d,
        0x2d, 0xc4, 0x44, 0x02, 0x88, 0x00, 0x48, 0x15, 0x0c, 0x10, 0x00, 0x86,
        0x2a, 0x80, 0x15, 0x5e, 0x02, 0x40, 0xa0, 0x08, 0x11, 0x52, 0xe8, 0xaf,
        0x00, 0x78, 0x84, 0x22, 0xb2, 0x0b, 0x44, 0x01, 0xa9, 0xb4, 0x29, 0x08,
        0x80, 0x24, 0xc6, 0x20, 0x04, 0x12, 0xc0, 0x00, 0xd0, 0x44, 0x48, 0x14,
        0x0a, 0x00, 0x00, 0x40, 0x14, 0x11, 0x05, 0x01, 0x36, 0x09, 0x80, 0xc1,
        0xf0, 0x96, 0x0b, 0x8c, 0x20, 0x11, 0x02, 0x95, 0x10, 0xf5, 0x42, 0x00,
        0x48, 0x41, 0x18, 0x07, 0x00, 0x21, 0xd1, 0x80, 0x80, 0x08, 0x34, 0x22,
        0x08, 0x08, 0x88, 0x95};
    valtype aorb{
        0xf6, 0x9e, 0xff, 0xdf, 0xe7, 0x77, 0x7b, 0x8f, 0x4f, 0x8f, 0x37, 0x2e,
        0xfd, 0xfd, 0xf6, 0xff, 0xfe, 0x1c, 0xff, 0xf7, 0x7d, 0xed, 0xbe, 0xfb,
        0x8f, 0xfe, 0xdf, 0xf3, 0xbe, 0x6f, 0xef, 0xaf, 0xff, 0xfe, 0xfd, 0x96,
        0x8f, 0x7d, 0x26, 0x3f, 0xff, 0xdb, 0x77, 0x5c, 0x7f, 0xf5, 0xfb, 0x7d,
        0xf7, 0x7f, 0x27, 0xf7, 0xfb, 0x7f, 0xef, 0x6f, 0xfb, 0x7d, 0xfc, 0xf8,
        0xc6, 0xbe, 0x73, 0xfa, 0x37, 0x7d, 0xd7, 0xfb, 0xa3, 0xcd, 0xdd, 0xf7,
        0xef, 0xfe, 0x6e, 0x6e, 0xb9, 0xbc, 0xfd, 0xf4, 0xee, 0xfb, 0xef, 0xff,
        0x77, 0xff, 0x64, 0x46, 0x7a, 0xb6, 0xdb, 0xb6, 0xdf, 0xf5, 0xbf, 0xc6,
        0xbb, 0x6a, 0x6d, 0xdf, 0xd8, 0x59, 0xff, 0xff, 0x7f, 0x7a, 0xfb, 0xab,
        0x79, 0xf7, 0xfd, 0xfe, 0xbf, 0xf5, 0x6f, 0xa7, 0x73, 0xfb, 0x39, 0x6e,
        0x5f, 0x57, 0xcf, 0x95, 0x3b, 0xfc, 0x4e, 0x47, 0xbf, 0xe5, 0xdf, 0xb9,
        0xfe, 0xdf, 0x1b, 0x6f, 0xee, 0x9b, 0x2f, 0xfb, 0xcf, 0xfb, 0xfc, 0xed,
        0xd6, 0xff, 0xfd, 0x7b, 0x54, 0xfe, 0xf3, 0xfe, 0xce, 0xff, 0xf2, 0xdd,
        0xfd, 0xbf, 0xea, 0x6e, 0xff, 0x3f, 0x9a, 0xfb, 0x4f, 0xee, 0xf7, 0xff,
        0x65, 0xf3, 0xf3, 0xbb, 0x77, 0xd7, 0x9c, 0xf7, 0xfb, 0x7c, 0xed, 0xf7,
        0x5f, 0xdf, 0xf7, 0x77, 0x3e, 0xff, 0xfa, 0xf7, 0xfe, 0xee, 0xcd, 0xeb,
        0xb5, 0xaf, 0xeb, 0xce, 0x0f, 0xdd, 0xdb, 0xb9, 0x7d, 0x9f, 0xa4, 0xfe,
        0x8e, 0xef, 0xef, 0xfa, 0xef, 0xfa, 0xf7, 0xfe, 0x5e, 0xdd, 0x6f, 0xbf,
        0xbf, 0x75, 0xcf, 0x7f, 0xce, 0xfe, 0x7f, 0xe0, 0xed, 0xf0, 0xbe, 0xee,
        0x34, 0xaf, 0xc1, 0xcf, 0xff, 0x5d, 0x9b, 0xff, 0xfc, 0xbf, 0xda, 0xff,
        0xf6, 0x3d, 0x78, 0xff, 0xe7, 0xb7, 0xf6, 0xf3, 0x39, 0x7f, 0xbf, 0x7f,
        0xee, 0x91, 0xff, 0x5d, 0xf3, 0xf4, 0xcf, 0xf9, 0x6f, 0xf5, 0xde, 0xff,
        0x5c, 0x9d, 0xbb, 0x91, 0xe5, 0xdf, 0x5f, 0xf7, 0xcb, 0xfb, 0xed, 0x1e,
        0x6f, 0xf5, 0xfb, 0xf5, 0xfe, 0xad, 0xef, 0x6f, 0xb5, 0x4f, 0xe5, 0x4f,
        0xfe, 0xff, 0x1d, 0x73, 0x35, 0xfb, 0xff, 0xff, 0xfd, 0xf7, 0xd1, 0xff,
        0xfe, 0xf6, 0xb7, 0xab, 0xe7, 0xdd, 0xfd, 0xef, 0xbc, 0xdb, 0xb2, 0x3d,
        0xbf, 0xbe, 0xff, 0xf5, 0xee, 0xfd, 0xec, 0x9f, 0xd5, 0xfa, 0xa3, 0xff,
        0xff, 0xef, 0xf0, 0xed, 0xf6, 0x7f, 0xff, 0xef, 0x15, 0xdb, 0xfb, 0xcf,
        0xdf, 0xef, 0xdd, 0xbf, 0x6f, 0x56, 0xff, 0x9f, 0xf0, 0xc7, 0xf6, 0xdf,
        0x7c, 0x99, 0xee, 0xf7, 0xec, 0xff, 0xfd, 0xff, 0x3f, 0xab, 0x0b, 0x77,
        0x79, 0x77, 0xfc, 0x1f, 0x7c, 0xfa, 0xaf, 0xff, 0x28, 0x71, 0xfd, 0x6f,
        0x7e, 0x7b, 0xff, 0xdb, 0xfe, 0x7f, 0xe8, 0x2f, 0xee, 0xe7, 0xf8, 0x7e,
        0xb7, 0x1f, 0x4d, 0x6c, 0xbf, 0xdb, 0xff, 0xee, 0x77, 0xe7, 0xfd, 0xfc,
        0xbf, 0xdc, 0xfd, 0xe3, 0x77, 0x5e, 0xe9, 0x6f, 0x8a, 0xb7, 0x7e, 0x7f,
        0xfd, 0xeb, 0xef, 0xec, 0x75, 0xef, 0xef, 0x73, 0x67, 0x3f, 0xdf, 0x23,
        0xe7, 0x3b, 0xdf, 0x9d, 0xff, 0xff, 0xfd, 0xf3, 0x1f, 0xbf, 0x2e, 0x2f,
        0xbf, 0xcf, 0xef, 0xf3, 0xaa, 0xbb, 0xc9, 0x3f, 0xec, 0xfe, 0x91, 0x96,
        0x6f, 0xe2, 0xff, 0xdf, 0xdb, 0xff, 0xa3, 0x89, 0xdd, 0xdf, 0xff, 0xff,
        0x9d, 0xfb, 0x9d, 0x7e, 0xff, 0xbf, 0x6d, 0xad, 0xfd, 0xff, 0xbf, 0xfa,
        0xdf, 0xf7, 0xff, 0xaa, 0x3d, 0x9b, 0xfc, 0x6e, 0xf3, 0xcd, 0xcb, 0xd5,
        0xaf, 0xdc, 0xff, 0xfc, 0x77, 0x9b, 0x87, 0xff, 0xf6, 0x7f, 0x93, 0xfb,
        0xf5, 0xfe, 0xaf, 0xbe, 0x6e, 0x75, 0xff, 0xff, 0xf4, 0xfd, 0xe7, 0x5a,
        0xe9, 0xff, 0xfb, 0x17, 0xf1, 0x6d, 0xf3, 0xd7, 0xd3, 0xbc, 0xf7, 0xe7,
        0x4e, 0x9e, 0x9a, 0xf7};

    TestBitwiseOpcodes(a, b, aandb, aorb);

    // Check errors conditions.
    // 1. Less than 2 elements on stack.
    CheckAllBitwiseOpErrors({}, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckAllBitwiseOpErrors({{}}, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckAllBitwiseOpErrors({{0x00}}, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckAllBitwiseOpErrors({{0xab, 0xcd, 0xef}},
                            SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckAllBitwiseOpErrors({a}, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckAllBitwiseOpErrors({b}, SCRIPT_ERR_INVALID_STACK_OPERATION);

    // 2. Operand of mismatching length
    CheckAllBitwiseOpErrors({{}, {0x00}}, SCRIPT_ERR_INVALID_OPERAND_SIZE);
    CheckAllBitwiseOpErrors({{0x00}, {}}, SCRIPT_ERR_INVALID_OPERAND_SIZE);
    CheckAllBitwiseOpErrors({{0x00}, {0xab, 0xcd, 0xef}},
                            SCRIPT_ERR_INVALID_OPERAND_SIZE);
    CheckAllBitwiseOpErrors({{0xab, 0xcd, 0xef}, {0x00}},
                            SCRIPT_ERR_INVALID_OPERAND_SIZE);
    CheckAllBitwiseOpErrors({{}, a}, SCRIPT_ERR_INVALID_OPERAND_SIZE);
    CheckAllBitwiseOpErrors({b, {}}, SCRIPT_ERR_INVALID_OPERAND_SIZE);
}

/**
 * String opcodes.
 */
static void CheckStringOp(const valtype &a, const valtype &b,
                          const valtype &n) {
    CheckBinaryOp(a, b, OP_CAT, n);

    // Check concatenation with empty elements.
    CheckBinaryOp(a, {}, OP_CAT, a);
    CheckBinaryOp(b, {}, OP_CAT, b);
    CheckBinaryOp({}, a, OP_CAT, a);
    CheckBinaryOp({}, b, OP_CAT, b);

    // Split n into a and b.
    CheckTestResultForAllFlags({n}, CScript() << a.size() << OP_SPLIT, {a, b});

    // Combine split and cat.
    CheckTestResultForAllFlags({n}, CScript() << a.size() << OP_SPLIT << OP_CAT,
                               {n});
    CheckTestResultForAllFlags(
        {a, b}, CScript() << OP_CAT << a.size() << OP_SPLIT, {a, b});

    // Split away empty elements.
    CheckTestResultForAllFlags({a}, CScript() << 0 << OP_SPLIT, {{}, a});
    CheckTestResultForAllFlags({b}, CScript() << 0 << OP_SPLIT, {{}, b});
    CheckTestResultForAllFlags({a}, CScript() << a.size() << OP_SPLIT, {a, {}});
    CheckTestResultForAllFlags({b}, CScript() << b.size() << OP_SPLIT, {b, {}});

    // Out of bound split.
    CheckErrorForAllFlags({a}, CScript() << (a.size() + 1) << OP_SPLIT,
                          SCRIPT_ERR_INVALID_SPLIT_RANGE);
    CheckErrorForAllFlags({b}, CScript() << (b.size() + 1) << OP_SPLIT,
                          SCRIPT_ERR_INVALID_SPLIT_RANGE);
    CheckErrorForAllFlags({n}, CScript() << (n.size() + 1) << OP_SPLIT,
                          SCRIPT_ERR_INVALID_SPLIT_RANGE);
    CheckErrorForAllFlags({a}, CScript() << (-1) << OP_SPLIT,
                          SCRIPT_ERR_INVALID_SPLIT_RANGE);
}

BOOST_AUTO_TEST_CASE(string_opcodes_test) {
    // Check for empty string.
    CheckStringOp({}, {}, {});

    // Check for simple concats.
    CheckStringOp({0x00}, {0x00}, {0x00, 0x00});
    CheckStringOp({0xab}, {0xcd}, {0xab, 0xcd});
    CheckStringOp({0xab, 0xcd, 0xef}, {0x12, 0x34, 0x56, 0x78},
                  {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78});

    const valtype n{
        0x7b, 0x59, 0xf8, 0x07, 0xc6, 0xc0, 0x70, 0xbc, 0x52, 0x7b, 0xf5, 0xaf,
        0xf5, 0xdd, 0xeb, 0xdc, 0x41, 0xaa, 0x07, 0xf6, 0x80, 0x8d, 0x5d, 0x4d,
        0xbc, 0x91, 0xcd, 0x0a, 0x14, 0x85, 0xd9, 0x98, 0xb6, 0xab, 0x2e, 0x37,
        0x76, 0x78, 0x34, 0x8b, 0x2b, 0xfb, 0x59, 0x3b, 0xea, 0x45, 0x46, 0x72,
        0x64, 0x64, 0x83, 0x73, 0xc3, 0x1d, 0xca, 0x86, 0x03, 0x91, 0xfc, 0xc0,
        0xc4, 0xdf, 0x17, 0x83, 0x22, 0x5d, 0x50, 0xc5, 0x31, 0x45, 0xaf, 0xbc,
        0xfd, 0xc8, 0xb9, 0x6a, 0x72, 0x8b, 0x3c, 0x9b, 0x77, 0x02, 0xd6, 0x18,
        0x62, 0x02, 0xc9, 0x1c, 0x66, 0x29, 0x5c, 0x66, 0xf3, 0x9a, 0x00, 0xc1,
        0x69, 0x47, 0x35, 0x2f, 0xe8, 0x32, 0x2a, 0xb5, 0xc4, 0x9f, 0x3c, 0xbf,
        0xc7, 0x1a, 0x2b, 0xb3, 0xa6, 0x9b, 0xde, 0xcf, 0xc5, 0x15, 0x8c, 0xac,
        0xd0, 0x7c, 0x38, 0xe4, 0x41, 0xe1, 0x81, 0x4e, 0x65, 0xa5, 0x24, 0x08,
        0x5b, 0xa3, 0x19, 0xf3, 0xc2, 0x80, 0x21, 0x01, 0x33, 0xaf, 0x84, 0x53,
        0x1a, 0x00, 0x79, 0x7e, 0x1f, 0xd1, 0x62, 0x53, 0x0d, 0x6a, 0x58, 0xde,
        0x16, 0x23, 0x70, 0x32, 0x81, 0x25, 0xbd, 0xa3, 0x92, 0xae, 0xfd, 0x7f,
        0x47, 0xa2, 0xf2, 0x34, 0x3d, 0xef, 0xc3, 0x71, 0xb1, 0x33, 0x9a, 0xfd,
        0x80, 0x4b, 0x96, 0xcb, 0xaa, 0xda, 0x77, 0x50, 0x58, 0xf7, 0x0c, 0xf3,
        0x75, 0xdf, 0x51, 0x96, 0x75, 0x9a, 0x78, 0xc3, 0xd3, 0xaf, 0xac, 0xee,
        0xf3, 0xcc, 0x79, 0xfb, 0x3f, 0xda, 0x51, 0x94, 0x8f, 0x59, 0x3d, 0xbc,
        0xef, 0x17, 0x47, 0xd4, 0x40, 0x80, 0x8a, 0x78, 0x86, 0x6c, 0x9e, 0x38,
        0xd2, 0x11, 0xaa, 0x94, 0x79, 0x9b, 0x61, 0xf3, 0xaa, 0xcf, 0x66, 0x7e,
        0xa7, 0x11, 0xe9, 0xad, 0x8a, 0xd4, 0x67, 0x23, 0xf9, 0x62, 0x9f, 0x55,
        0xc0, 0x5a, 0x0f, 0x0a, 0xfe, 0x28, 0xd8, 0x80, 0xaf, 0x71, 0x97, 0x65,
        0x49, 0xb1, 0xd3, 0x9c, 0xee, 0x7e, 0x4b, 0xeb, 0x06, 0x3b, 0xe1, 0x66,
        0xf9, 0xa7, 0x77, 0x4f, 0x6a, 0xd1, 0xa0, 0x16, 0xe0, 0xcf, 0xe3, 0x25,
        0x65, 0x08, 0x0f, 0x5e, 0x2c, 0x1e, 0x80, 0x35, 0x75, 0x40, 0x9a, 0xd1,
        0x14, 0xba, 0xaa, 0xa7, 0xfc, 0x3c, 0xf1, 0xeb, 0x16, 0x8d, 0x59, 0xb4,
        0xcf, 0x16, 0x9a, 0xe3, 0xf1, 0x9d, 0x31, 0x97, 0xe5, 0xa4, 0xcc, 0xae,
        0x1c, 0xa2, 0xe7, 0x88, 0x44, 0x05, 0x67, 0x28, 0x21, 0x9f, 0x3e, 0xe2,
        0xfc, 0x25, 0x8c, 0x63, 0x09, 0xde, 0x39, 0xfa, 0xae, 0x26, 0x9b, 0x43,
        0xdf, 0x06, 0x2f, 0xb7, 0xaf, 0xa2, 0x74, 0x1c, 0x17, 0x96, 0x84, 0x26,
        0x1a, 0xe2, 0xcd, 0x90, 0xa8, 0xc3, 0xb6, 0xeb, 0x53, 0xee, 0xdd, 0xf9,
        0x88, 0xc6, 0x05, 0xb5, 0xd4, 0xa3, 0xf0, 0x36, 0xc7, 0xf1, 0xb3, 0x04,
        0x0c, 0xa5, 0xea, 0x22, 0x5b, 0x56, 0x3d, 0x54, 0x0b, 0x69, 0xc2, 0xe1,
        0x4f, 0xa8, 0x28, 0x4e, 0xe2, 0x3d, 0x99, 0x9c, 0x3b, 0xdb, 0xf4, 0x92,
        0x5a, 0xb9, 0xce, 0xeb, 0x33, 0xb5, 0xae, 0x16, 0x58, 0x79, 0x31, 0x8f,
        0x1e, 0x7a, 0x1a, 0xee, 0xbe, 0x9f, 0xea, 0x89, 0xd6, 0x6c, 0x43, 0x76,
        0x94, 0x0d, 0x94, 0x50, 0x6d, 0xdd, 0xc2, 0x68, 0x80, 0x3e, 0x38, 0x51,
        0x51, 0xd1, 0xd5, 0x4e, 0xf7, 0x65, 0xe5, 0x42, 0x3c, 0xa8, 0x28, 0x19,
        0x02, 0xa7, 0xc9, 0x1c, 0x24, 0xa7, 0x91, 0xfe, 0xa1, 0xbc, 0xb9, 0x15,
        0xba, 0x49, 0xac, 0xeb, 0x81, 0xf7, 0xc1, 0xfc, 0xf9, 0x51, 0x0d, 0xa1,
        0xe8, 0x71, 0x2c, 0x4e, 0x59, 0xc1, 0x3a, 0x2a, 0xcc, 0x61, 0xee, 0xe5,
        0x2a, 0x88, 0xf8, 0xec, 0xbd, 0x90, 0xc0, 0x96, 0xe0, 0x93, 0x1f, 0x78,
        0xbe, 0x6b, 0xb1, 0x4c, 0x46, 0x2a, 0x86, 0xd9, 0x2d, 0x20, 0x29, 0xb4,
        0x44, 0x15, 0xb2, 0x7e};

    BOOST_CHECK_EQUAL(n.size(), MAX_SCRIPT_ELEMENT_SIZE);

    for (size_t i = 0; i <= MAX_SCRIPT_ELEMENT_SIZE; i++) {
        valtype a(n.begin(), n.begin() + i);
        valtype b(n.begin() + i, n.end());

        CheckStringOp(a, b, n);

        // One more char and we are oversize.
        valtype extraA = a;
        extraA.push_back(0xaf);

        valtype extraB = b;
        extraB.push_back(0xad);

        CheckOpError({extraA, b}, OP_CAT, SCRIPT_ERR_PUSH_SIZE);
        CheckOpError({a, extraB}, OP_CAT, SCRIPT_ERR_PUSH_SIZE);
        CheckOpError({extraA, extraB}, OP_CAT, SCRIPT_ERR_PUSH_SIZE);
    }

    // Check error conditions.
    CheckOpError({}, OP_CAT, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckOpError({}, OP_SPLIT, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckOpError({{}}, OP_CAT, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckOpError({{}}, OP_SPLIT, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckOpError({{0x00}}, OP_CAT, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckOpError({{0x00}}, OP_SPLIT, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckOpError({{0xab, 0xcd, 0xef}}, OP_CAT,
                 SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckOpError({{0xab, 0xcd, 0xef}}, OP_SPLIT,
                 SCRIPT_ERR_INVALID_STACK_OPERATION);
}

/**
 * Type conversion opcodes.
 */
static void CheckTypeConversionOp(const valtype &bin, const valtype &num) {
    // Check BIN2NUM.
    CheckTestResultForAllFlags({bin}, CScript() << OP_BIN2NUM, {num});

    // Check NUM2BIN. Negative 0 is rebuilt as regular zero, so we need a tweak.
    valtype rebuilt_bin{bin};
    if (num.size() == 0 && bin.size() > 0) {
        rebuilt_bin[rebuilt_bin.size() - 1] &= 0x7f;
    }

    CheckTestResultForAllFlags({num}, CScript() << bin.size() << OP_NUM2BIN,
                               {rebuilt_bin});

    // Check roundtrip with NUM2BIN.
    CheckTestResultForAllFlags(
        {bin}, CScript() << OP_BIN2NUM << bin.size() << OP_NUM2BIN,
        {rebuilt_bin});

    // Grow and shrink back down using NUM2BIN.
    CheckTestResultForAllFlags({bin},
                               CScript()
                                   << MAX_SCRIPT_ELEMENT_SIZE << OP_NUM2BIN
                                   << bin.size() << OP_NUM2BIN,
                               {rebuilt_bin});
    CheckTestResultForAllFlags({num},
                               CScript()
                                   << MAX_SCRIPT_ELEMENT_SIZE << OP_NUM2BIN
                                   << bin.size() << OP_NUM2BIN,
                               {rebuilt_bin});

    // BIN2NUM is indempotent.
    CheckTestResultForAllFlags({bin}, CScript() << OP_BIN2NUM << OP_BIN2NUM,
                               {num});
}

static void CheckBin2NumError(const stacktype &original_stack,
                              ScriptError expected_error) {
    CheckErrorForAllFlags(original_stack, CScript() << OP_BIN2NUM,
                          expected_error);
}

static void CheckNum2BinError(const stacktype &original_stack,
                              ScriptError expected_error) {
    CheckErrorForAllFlags(original_stack, CScript() << OP_NUM2BIN,
                          expected_error);
}

BOOST_AUTO_TEST_CASE(type_conversion_test) {
    valtype empty;
    CheckTypeConversionOp(empty, empty);

    valtype paddedzero, paddednegzero;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE; i++) {
        CheckTypeConversionOp(paddedzero, empty);
        paddedzero.push_back(0x00);

        paddednegzero.push_back(0x80);
        CheckTypeConversionOp(paddednegzero, empty);
        paddednegzero[paddednegzero.size() - 1] = 0x00;
    }

    // Merge leading byte when sign bit isn't used.
    std::vector<uint8_t> k{0x7f}, negk{0xff};
    std::vector<uint8_t> kpadded = k, negkpadded = negk;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE; i++) {
        CheckTypeConversionOp(kpadded, k);
        kpadded.push_back(0x00);

        CheckTypeConversionOp(negkpadded, negk);
        negkpadded[negkpadded.size() - 1] &= 0x7f;
        negkpadded.push_back(0x80);
    }

    // Some known values.
    CheckTypeConversionOp({0xab, 0xcd, 0xef, 0x00}, {0xab, 0xcd, 0xef, 0x00});
    CheckTypeConversionOp({0xab, 0xcd, 0x7f, 0x00}, {0xab, 0xcd, 0x7f});

    // Reductions
    CheckTypeConversionOp({0xab, 0xcd, 0xef, 0x42, 0x80},
                          {0xab, 0xcd, 0xef, 0xc2});
    CheckTypeConversionOp({0xab, 0xcd, 0x7f, 0x42, 0x00},
                          {0xab, 0xcd, 0x7f, 0x42});

    // Empty stack is an error.
    CheckBin2NumError({}, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckNum2BinError({}, SCRIPT_ERR_INVALID_STACK_OPERATION);

    // NUM2BIN require 2 elements on the stack.
    CheckNum2BinError({{0x00}}, SCRIPT_ERR_INVALID_STACK_OPERATION);

    // Values that do not fit in 4 bytes are considered out of range for
    // BIN2NUM.
    CheckBin2NumError({{0xab, 0xcd, 0xef, 0xc2, 0x80}},
                      SCRIPT_ERR_INVALID_NUMBER_RANGE);
    CheckBin2NumError({{0x00, 0x00, 0x00, 0x80, 0x80}},
                      SCRIPT_ERR_INVALID_NUMBER_RANGE);

    // NUM2BIN must not generate oversized push.
    valtype largezero(MAX_SCRIPT_ELEMENT_SIZE, 0);
    BOOST_CHECK_EQUAL(largezero.size(), MAX_SCRIPT_ELEMENT_SIZE);
    CheckTypeConversionOp(largezero, {});

    CheckNum2BinError({{}, {0x09, 0x02}}, SCRIPT_ERR_PUSH_SIZE);

    // Check that the requested encoding is possible.
    CheckNum2BinError({{0xab, 0xcd, 0xef, 0x80}, {0x03}},
                      SCRIPT_ERR_IMPOSSIBLE_ENCODING);
}

/**
 * Arithmetic Opcodes
 */
static void CheckDivMod(const valtype &a, const valtype &b,
                        const valtype &divExpected,
                        const valtype &modExpected) {
    // Negative values for division
    CheckBinaryOp(a, b, OP_DIV, divExpected);
    CheckBinaryOp(a, NegativeValtype(b), OP_DIV, NegativeValtype(divExpected));
    CheckBinaryOp(NegativeValtype(a), b, OP_DIV, NegativeValtype(divExpected));
    CheckBinaryOp(NegativeValtype(a), NegativeValtype(b), OP_DIV, divExpected);

    // Negative values for modulo
    CheckBinaryOp(a, b, OP_MOD, modExpected);
    CheckBinaryOp(a, NegativeValtype(b), OP_MOD, modExpected);
    CheckBinaryOp(NegativeValtype(a), b, OP_MOD, NegativeValtype(modExpected));
    CheckBinaryOp(NegativeValtype(a), NegativeValtype(b), OP_MOD,
                  NegativeValtype(modExpected));

    // Div/Mod by zero
    for (uint32_t flags : flagset) {
        CheckError(flags, {a, {}}, CScript() << OP_DIV, SCRIPT_ERR_DIV_BY_ZERO);
        CheckError(flags, {b, {}}, CScript() << OP_DIV, SCRIPT_ERR_DIV_BY_ZERO);

        if (flags & SCRIPT_VERIFY_MINIMALDATA) {
            CheckError(flags, {a, {0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);
            CheckError(flags, {a, {0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);
            CheckError(flags, {a, {0x00, 0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);
            CheckError(flags, {a, {0x00, 0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);

            CheckError(flags, {b, {0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);
            CheckError(flags, {b, {0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);
            CheckError(flags, {b, {0x00, 0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);
            CheckError(flags, {b, {0x00, 0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_UNKNOWN_ERROR);
        } else {
            CheckError(flags, {a, {0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);
            CheckError(flags, {a, {0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);
            CheckError(flags, {a, {0x00, 0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);
            CheckError(flags, {a, {0x00, 0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);

            CheckError(flags, {b, {0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);
            CheckError(flags, {b, {0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);
            CheckError(flags, {b, {0x00, 0x00}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);
            CheckError(flags, {b, {0x00, 0x80}}, CScript() << OP_DIV,
                       SCRIPT_ERR_DIV_BY_ZERO);
        }
    }

    // Division identities
    CheckBinaryOp(a, {0x01}, OP_DIV, a);
    CheckBinaryOp(a, {0x81}, OP_DIV, NegativeValtype(a));
    CheckBinaryOp(a, a, OP_DIV, {0x01});
    CheckBinaryOp(a, NegativeValtype(a), OP_DIV, {0x81});
    CheckBinaryOp(NegativeValtype(a), a, OP_DIV, {0x81});

    CheckBinaryOp(b, {0x01}, OP_DIV, b);
    CheckBinaryOp(b, {0x81}, OP_DIV, NegativeValtype(b));
    CheckBinaryOp(b, b, OP_DIV, {0x01});
    CheckBinaryOp(b, NegativeValtype(b), OP_DIV, {0x81});
    CheckBinaryOp(NegativeValtype(b), b, OP_DIV, {0x81});

    // Modulo identities
    // a % b % b = a % b
    CheckTestResultForAllFlags(
        {a, b}, CScript() << OP_MOD << CScriptNum(b, true).getint() << OP_MOD,
        {modExpected});
}

static void CheckDivModError(const stacktype &original_stack,
                             ScriptError expected_error) {
    CheckOpError(original_stack, OP_DIV, expected_error);
    CheckOpError(original_stack, OP_MOD, expected_error);
}

BOOST_AUTO_TEST_CASE(div_and_mod_opcode_tests) {
    CheckDivModError({}, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckDivModError({{}}, SCRIPT_ERR_INVALID_STACK_OPERATION);

    // CheckOps not valid numbers
    CheckDivModError(
        {{0x01, 0x02, 0x03, 0x04, 0x05}, {0x01, 0x02, 0x03, 0x04, 0x05}},
        SCRIPT_ERR_UNKNOWN_ERROR);
    CheckDivModError({{0x01, 0x02, 0x03, 0x04, 0x05}, {0x01}},
                     SCRIPT_ERR_UNKNOWN_ERROR);
    CheckDivModError({{0x01, 0x05}, {0x01, 0x02, 0x03, 0x04, 0x05}},
                     SCRIPT_ERR_UNKNOWN_ERROR);

    // 0x185377af / 0x85f41b01 = -4
    // 0x185377af % 0x85f41b01 = 0x00830bab
    // 408123311 / -99883777 = -4
    // 408123311 % -99883777 = 8588203
    CheckDivMod({0xaf, 0x77, 0x53, 0x18}, {0x01, 0x1b, 0xf4, 0x85}, {0x84},
                {0xab, 0x0b, 0x83, 0x00});
    // 0x185377af / 0x00001b01 = 0xe69d
    // 0x185377af % 0x00001b01 = 0x0212
    // 408123311 / 6913 = 59037
    // 408123311 % 6913 = 530
    CheckDivMod({0xaf, 0x77, 0x53, 0x18}, {0x01, 0x1b}, {0x9d, 0xe6, 0x00},
                {0x12, 0x02});

    // 15/4 = 3 (and negative operands)
    CheckDivMod({0x0f}, {0x04}, {0x03}, {0x03});
    // 15000/4 = 3750 (and negative operands)
    CheckDivMod({0x98, 0x3a}, {0x04}, {0xa6, 0x0e}, {});
    // 15000/4000 = 3 (and negative operands)
    CheckDivMod({0x98, 0x3a}, {0xa0, 0x0f}, {0x03}, {0xb8, 0x0b});
    // 15000000/4000 = 3750 (and negative operands)
    CheckDivMod({0xc0, 0xe1, 0xe4, 0x00}, {0xa0, 0x0f}, {0xa6, 0x0e}, {});
    // 15000000/4 = 3750000 (and negative operands)
    CheckDivMod({0xc0, 0xe1, 0xe4, 0x00}, {0x04}, {0x70, 0x38, 0x39}, {});

    // 56488123 % 321 = 148 (and negative operands)
    CheckDivMod({0xbb, 0xf0, 0x5d, 0x03}, {0x41, 0x01}, {0x67, 0xaf, 0x02},
                {0x94, 0x00});
    // 56488123 % 3 = 1 (and negative operands)
    CheckDivMod({0xbb, 0xf0, 0x5d, 0x03}, {0x03}, {0x3e, 0x50, 0x1f, 0x01},
                {0x01});
    // 56488123 % 564881230 = 56488123 (and negative operands)
    CheckDivMod({0xbb, 0xf0, 0x5d, 0x03}, {0x4e, 0x67, 0xab, 0x21}, {},
                {0xbb, 0xf0, 0x5d, 0x03});
}

BOOST_AUTO_TEST_SUITE_END()
