// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <validation/validation.h>
#include <coins.h>
#include <primitives/transaction.h>

#include <test/test_bitcoin.h>

#include <map>
#include <string>

/// Testing setup that:
/// - loads all of the json data for all of the "chip" tests into a static structure (lazy load, upon first use)
/// - tracks if we overrode ::fRequireStandard, and resets it on test end
/// Subclasses must reimplement "ActivateChip()" (see token_transaction_tests.cpp for a subclass that uses this setup)
class ChipTestingSetup : public TestChain100Setup {
    enum TxStandard { INVALID, NONSTANDARD, STANDARD };

    struct TestVector {
        std::string name;
        std::string description;
        bool chipActive; // Whether or not the chip should be activated for this test
        TxStandard standardness; // Which validation standard this test should meet

        struct Test {
            std::string ident;
            std::string description;
            std::string stackAsm;
            std::string scriptAsm;
            CTransactionRef tx;
            size_t txSize{};
            CCoinsMap inputCoins;
            std::string standardReason; //! Expected failure reason when validated in standard mode
            std::string nonstandardReason; //! Expected failure reason when validated in nonstandard mode
            std::string libauthStandardReason; //! Libauth suggested failure reason when validated in standard mode
            std::string libauthNonstandardReason; //! Libauth suggested failure reason when validated in nonstandard mode
        };

        std::vector<Test> vec;
    };

    static std::map<std::string, std::vector<TestVector>> allChipsVectors;

    static void LoadChipsVectors();
    static void RunTestVector(const TestVector &test);


protected:
    /// Reimplement this in subclasses to turn on/off the chip in question.
    virtual void ActivateChip(bool active) = 0;

public:
    ChipTestingSetup();
    ~ChipTestingSetup() override;

    void RunTestsForChip(const std::string &chipName);
};
