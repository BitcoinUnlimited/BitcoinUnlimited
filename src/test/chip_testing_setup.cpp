// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/chip_testing_setup.h>

#include <config.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <streams.h>
#include <txmempool.h>
#include <txadmission.h>
#include <util/defer.h>
#include <validation/validation.h>

#include <test/data/chip_test_vectors.json.h>
#include <test/data/expected_test_fail_reasons.json.h>
#include <test/jsonutil.h>

#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <map>
#include <string>

/* static */
std::map<std::string, std::vector<ChipTestingSetup::TestVector>> ChipTestingSetup::allChipsVectors = {};

/* static */
void ChipTestingSetup::LoadChipsVectors() {
    if (!allChipsVectors.empty()) return;

    static_assert(sizeof(json_tests::chip_test_vectors[0]) == 1 && sizeof(json_tests::expected_test_fail_reasons[0]) == 1,
                  "Assumption is that the test vectors are byte blobs of json data");

    const auto allChipsTests = read_json({ reinterpret_cast<const char *>(json_tests::chip_test_vectors),
                                           std::size(json_tests::chip_test_vectors) });
    const auto bchnReasons = read_json({ reinterpret_cast<const char *>(json_tests::expected_test_fail_reasons),
                                         std::size(json_tests::expected_test_fail_reasons) });

    // Holds all expected failure reasons for all CHIP tests for all CHIPs
    // chipName: {activationType: {chipActive: {standardness: {ident: "reason"}}}}
    std::map<std::string, std::map<bool, std::map<TxStandard,  std::map<std::string, std::string>>>> reasonsDictionary;

    BOOST_CHECK( ! bchnReasons.empty());
    for (size_t i = 0; i < bchnReasons.size(); ++i) {
        const auto chipWrap = bchnReasons[i];
        BOOST_CHECK(chipWrap.isObject());
        if (chipWrap.isObject()) {
            std::map<std::string,UniValue> kv1;
            chipWrap.getObjMap(kv1);
            for (const auto & [chipName, chip] : kv1) {
                BOOST_CHECK(chip.isObject());
                if (chip.isObject()) {
                    std::map<std::string,UniValue> kv2;
                    chip.getObjMap(kv2);
                    for (const auto & [activationType, reasons] : kv2) {
                        BOOST_CHECK(reasons.isObject());
                        if (reasons.isObject()) {
                            bool chipActive = activationType == "postactivation";
                            std::map<std::string,UniValue> reasonsmap;
                            reasons.getObjMap(reasonsmap);
                            for (const auto & [ident, obj] : reasonsmap) {
                                if (ident[0] == '_') { // Treat idents that start with an underscore as comments
                                    continue;
                                }
                                if (obj.isArray()) {
                                    auto standard = STANDARD;
                                    for (size_t j = 0; j < obj.get_array().size(); ++j) {
                                        const auto &item = obj.get_array()[j];
                                        if (!reasonsDictionary[chipName][chipActive][standard][ident].empty()) {
                                            BOOST_WARN_MESSAGE(false, strprintf("Too many reasons given for the %s %s test "
                                                                                "'%s' in expected_test_fail_reasons.json",
                                                                                chipName, activationType, ident));
                                        }
                                        reasonsDictionary[chipName][chipActive][standard][ident] = item.get_str();
                                        standard = NONSTANDARD;
                                    }
                                } else {
                                    throw std::runtime_error(
                                        strprintf("Bad expected BCHN failure 'reason' JSON for test \"%s\", expected "
                                                  "array of strings.", ident));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    BOOST_CHECK( ! allChipsTests.empty());
    unsigned coinHeights = []{
        LOCK(cs_main);
        return chainActive.Tip()->nHeight;
    }();
    for (size_t j = 0; j < allChipsTests.size(); ++j) {
        auto &chip = allChipsTests[j];
        BOOST_CHECK(chip.isObject());
        if (chip.isObject()) {
            auto &chipObj = chip.get_obj();
            BOOST_CHECK(chipObj.exists("name"));
            if (chipObj.exists("name")) {
                auto chipName = chipObj["name"].get_str();
                std::vector<TestVector> chipVec;
                std::map<bool, std::map<TxStandard, std::map<std::string, std::string>>> libauthReasonsMap;
                for (size_t x = 0; x < chipObj["tests"].get_array().size(); ++x) {
                    const auto &uv = chipObj["tests"].get_array()[x];
                    BOOST_CHECK(uv.isObject());
                    if (uv.isObject()) {
                        auto &uvObj = uv.get_obj();
                        BOOST_CHECK(uvObj.exists("name"));
                        if (uvObj.exists("name")) {
                            std::string testName = uvObj["name"].get_str();
                            std::string preactivePrefix = "preactivation_";
                            bool chipActive = testName.rfind(preactivePrefix, 0) != 0;
                            std::string standardnessStr = chipActive ? testName
                                                                     : testName.substr(preactivePrefix.size());
                            BOOST_CHECK(standardnessStr == "invalid" ||
                                        standardnessStr == "nonstandard" ||
                                        standardnessStr == "standard" );
                            TxStandard testStandardness = INVALID;
                            if (standardnessStr == "nonstandard") {
                                testStandardness = NONSTANDARD;
                            } else if (standardnessStr == "standard") {
                                testStandardness = STANDARD;
                            }
                            std::string descActiveString = chipActive ? "Post-Activation" : "Pre-Activation";
                            std::string descStdString = "fail validation in both nonstandard and standard mode";
                            if (testStandardness == NONSTANDARD) {
                                descStdString = "fail validation in standard mode but pass validation in nonstandard mode";
                            } else if (testStandardness == STANDARD) {
                                descStdString = "pass validation in both standard and nonstandard mode";
                            }
                            std::string testDescription = descActiveString + ": Test vectors that must " + descStdString;
                            TestVector testVec;
                            testVec.name = testName;
                            testVec.description = testDescription;
                            testVec.chipActive = chipActive;
                            testVec.standardness = testStandardness;

                            const auto &libauthReasons = uvObj["reasons"];
                            if (libauthReasons.isObject()) { // may be null
                                std::map<std::string,UniValue> reasonsmap;
                                libauthReasons.getObjMap(reasonsmap);
                                for (const auto & [ident, obj] : reasonsmap) {
                                    if (obj.isStr()) {
                                        libauthReasonsMap[chipActive][testStandardness][ident] = obj.get_str();
                                    }
                                }
                            }
                            for (size_t y = 0; y < uvObj["tests"].get_array().size(); ++y) {
                                const auto &t = uvObj["tests"].get_array()[y];
                                const UniValue &vec = t.get_array();
                                BOOST_CHECK_GE(vec.size(), 6);
                                TestVector::Test test;
                                test.ident = vec[0].get_str();
                                test.description = vec[1].get_str();
                                test.stackAsm = vec[2].get_str();
                                test.scriptAsm = vec[3].get_str();

                                // Invalid tests are expected to return both standard and nonstandard mode errors.
                                // Nonstandard tests are only expected to return standard mode errors.
                                if (testStandardness == INVALID || testStandardness == NONSTANDARD) {
                                    auto &reasons = reasonsDictionary[chipName][chipActive];
                                    test.standardReason = reasons[STANDARD][test.ident];
                                    if (testStandardness == INVALID) {
                                        test.nonstandardReason = reasons[NONSTANDARD][test.ident];
                                    }
                                }

                                CTransaction tmptx;
                                BOOST_CHECK(DecodeHexTx(tmptx, vec[4].get_str()));
                                CMutableTransaction mtx(tmptx);
                                test.tx = MakeTransactionRef(std::move(mtx));
                                const auto serinputs = ParseHex(vec[5].get_str());
                                std::vector<CTxOut> utxos;
                                {
                                    VectorReader vr(SER_NETWORK, INIT_PROTO_VERSION, serinputs, 0);
                                    vr >> utxos;
                                    BOOST_CHECK(vr.empty());
                                }
                                BOOST_CHECK_EQUAL(utxos.size(), test.tx->vin.size());
                                std::string skipReason;
                                for (size_t i = 0; i < utxos.size(); ++i) {
                                    auto [it, inserted] = test.inputCoins.emplace(std::piecewise_construct,
                                                                                  std::forward_as_tuple(test.tx->vin[i].prevout),
                                                                                  std::forward_as_tuple(Coin(utxos[i],
                                                                                                             coinHeights, false)));
                                    it->second.flags = CCoinsCacheEntry::FRESH;
                                    if (!inserted) {
                                        skipReason += strprintf("\n- Skipping bad tx due to dupe input Input[%i]: %s, Coin1: %s,"
                                                                " Coin2: %s\n%s",
                                                                i, it->first.ToString(true),
                                                                it->second.coin.out.ToString(true),
                                                                utxos[i].ToString(true), test.tx->ToString(true));
                                    }
                                    BOOST_CHECK(!it->second.coin.IsSpent());
                                }
                                test.txSize = ::GetSerializeSize(*test.tx, SER_NETWORK, INIT_PROTO_VERSION);
                                if ( ! skipReason.empty()) {
                                    BOOST_WARN_MESSAGE(false, strprintf("Skipping test \"%s\": %s", test.ident, skipReason));
                                } else {
                                    testVec.vec.push_back(std::move(test));
                                }
                            }
                            chipVec.push_back(std::move(testVec));
                        }
                    }
                }
                // Assign libauth's suggested failure reasons to each test
                for (auto &tv : chipVec) {
                    for (auto &test : tv.vec) {
                        if (tv.standardness == INVALID || tv.standardness == NONSTANDARD) {
                            test.libauthStandardReason = libauthReasonsMap[tv.chipActive][INVALID][test.ident];
                            test.libauthNonstandardReason = libauthReasonsMap[tv.chipActive][tv.standardness][test.ident];
                        }
                    }
                }
                allChipsVectors[chipName] = std::move(chipVec);
            }
        }
    }
    BOOST_CHECK( ! allChipsVectors.empty());

    // Check there are no orphan expected reasons for nonexistent tests
    std::set<std::string> orphans;
    for (auto const& [chipName, chipVec] : reasonsDictionary) {
        for (auto const& [chipActive, chipTests] : chipVec) {
            for (auto const& [standard, testVec] : chipTests) {
                for (auto const& [ident, _] : testVec) {
                    // See if there is a test matching this expected failure reason
                    if (orphans.count(ident) > 0) continue;  // Already found this one
                    bool found = false;
                    for (auto const& testVector : allChipsVectors[chipName]) {
                        if (!found && testVector.chipActive == chipActive && testVector.standardness != STANDARD) {
                            for (auto const& test : testVector.vec) {
                                if (test.ident == ident) {
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!found) {
                        orphans.insert(ident);
                        BOOST_CHECK_MESSAGE(false, strprintf("Found expected test failure reasons for a nonexistent "
                                                             "test \"%\".", ident));
                    }
                }
            }
        }
    }
}

/* static */
void ChipTestingSetup::RunTestVector(const TestVector &test) {
    std::string activeStr = test.chipActive ? "postactivation" : "preactivation";
    const bool expectStd = test.standardness == STANDARD;
    const bool expectNonStd = test.standardness == STANDARD || test.standardness == NONSTANDARD;
    BOOST_TEST_MESSAGE(strprintf("Running test vectors \"%s\", description: \"%s\" ...", test.name, test.description));

    size_t num = 0;
    for (const auto &tv : test.vec) {
        ++num;
        BOOST_TEST_MESSAGE(strprintf("Executing \"%s\" test %i \"%s\": \"%s\", tx-size: %i, nInputs: %i ...\n",
                                     test.name, num, tv.ident, tv.description, ::GetSerializeSize(*tv.tx, SER_NETWORK, INIT_PROTO_VERSION),
                                     tv.inputCoins.size()));
        Defer cleanup([&]{
            LOCK(cs_main);
            mempool.clear();
            for (auto & [outpt, _] : tv.inputCoins) {
                // clear utxo set of the temp coins we added for this tx
                pcoinsTip->SpendCoin(outpt);
            }
        });
        LOCK(cs_main);
        for (const auto &[outpt, entry] : tv.inputCoins) {
            // add each coin that the tx spends to the utxo set
            Coin cpy = entry.coin;
            pcoinsTip->AddCoin(outpt, std::move(cpy), false);;
        }
        // First, do "standard" test; result should match `expectStd`
        CValidationState state;
        bool missingInputs{};
        bool const ok1 = AcceptToMemoryPool(mempool, state, tv.tx, false, &missingInputs, false, TransactionClass::STANDARD);
        std::string reason = state.GetRejectReason();
        if (reason.empty() && !ok1 && missingInputs) reason = "Missing inputs";
        BOOST_CHECK_MESSAGE(ok1 == expectStd, strprintf("(%s standard) %s Wrong result. %s.", activeStr, tv.ident,
                                                        expectStd ? "Pass expected, test failed." :
                                                                    "Fail expected, test passed."));
        bool goodStandardReason = expectStd || tv.standardReason == reason;
        bool goodNonstandardReason = true;
        BOOST_CHECK_MESSAGE(goodStandardReason,
                            strprintf("(%s standard) %s Unexpected reject reason. Expected \"%s\", got \"%s\". "
                                      "Libauth's reason: \"%s\".", activeStr, tv.ident, tv.standardReason, reason,
                                      tv.libauthStandardReason));
        bool ok2 = expectNonStd;
        if (!expectStd) {
            // Next, do "nonstandard" test but only if `!expectStd`; result should match `expectNonStd`
            state = {};
            missingInputs = false;
            ok2 = AcceptToMemoryPool(mempool, state, tv.tx,false,  &missingInputs, false, TransactionClass::NONSTANDARD);
            reason = state.GetRejectReason();
            if (reason.empty() && !ok2 && missingInputs) reason = "Missing inputs";
            BOOST_CHECK_MESSAGE(ok2 == expectNonStd,
                                strprintf("(%s nonstandard) %s Wrong result. %s.", activeStr, tv.ident,
                                          expectNonStd ? "Pass expected, test failed."
                                                       : "Fail expected, test passed."));
            goodNonstandardReason = expectNonStd || tv.nonstandardReason == reason;
            if (!goodNonstandardReason)
            {
                mempool.Remove(tv.tx->GetHash());
                bool ok3 = AcceptToMemoryPool(mempool, state, tv.tx,false,  &missingInputs, false, TransactionClass::NONSTANDARD);
                printf("problem\n");  // DBG
            }
            BOOST_CHECK_MESSAGE(goodNonstandardReason,
                                strprintf("(%s nonstandard) %s Unexpected reject reason. Expected \"%s\", got \"%s\". "
                                          "Libauth's reason: \"%s\".", activeStr, tv.ident, tv.nonstandardReason,
                                          reason, tv.libauthNonstandardReason));
        }
        if (ok1 != expectStd || ok2 != expectNonStd || !goodStandardReason || !goodNonstandardReason) {
            auto &tx = tv.tx;
            BOOST_TEST_MESSAGE(strprintf("TxId %s for test \"%s\" details:", tx->GetHash().ToString(), tv.ident));
            size_t i = 0;
            for (auto &inp : tx->vin) {
                READLOCK(pcoinsTip->cs_utxo);
                const CTxOut &txout = pcoinsTip->_AccessCoin(inp.prevout).out;
                BOOST_TEST_MESSAGE(strprintf("Input %i: %s, coin = %s",
                                             i, inp.prevout.ToString(true), txout.ToString(true)));
                ++i;
            }
            i = 0;
            for (auto &outp : tx->vout) {
                BOOST_TEST_MESSAGE(strprintf("Output %i: %s", i, outp.ToString(true)));
                ++i;
            }
        }
    }
}

ChipTestingSetup::ChipTestingSetup() {}

ChipTestingSetup::~ChipTestingSetup() {
}


void ChipTestingSetup::RunTestsForChip(const std::string &chipName) {
    LoadChipsVectors();
    const auto it = allChipsVectors.find(chipName);
    if (it != allChipsVectors.end()) {
        BOOST_TEST_MESSAGE(strprintf("----- Running '%s' CHIP tests -----", chipName));
        for (const TestVector &testVector : it->second) {
            ActivateChip(testVector.chipActive);
            RunTestVector(testVector);
        }
    } else {
        // fail if test vectors for `chipName` are not found
        BOOST_CHECK_MESSAGE(false, strprintf("No tests found for '%s' CHIP!", chipName));
    }
}
