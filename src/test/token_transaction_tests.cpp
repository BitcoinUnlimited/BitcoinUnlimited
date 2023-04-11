// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <config.h>
// #include <util/system.h>
#include <validation/validation.h>

#include <test/chip_testing_setup.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <map>
#include <string>

namespace {

/// Test fixture that:
/// - tracks if we set "-upgrade9activationtime", and resets it on test end
struct TokenTransactionTestingSetup : ChipTestingSetup {

    TokenTransactionTestingSetup() {
        UnsetArg("-upgrade9activationtime");
    }

    ~TokenTransactionTestingSetup() override {
        UnsetArg("-upgrade9activationtime");
    }

    /// Activates or deactivates upgrade 9 by setting the activation time in the past or future respectively
    void SetUpgrade9Active(bool active) {
        const auto currentMTP = []{
            LOCK(cs_main);
            return chainActive.Tip()->GetMedianTimePast();
        }();
        auto activationMtp = active ? currentMTP - 1 : currentMTP + 1;
        SetArg("-upgrade9activationtime", strprintf("%d", activationMtp));
    }

protected:
    /// Concrete implementation of abstract base pure virtual method
    void ActivateChip(bool active) override { SetUpgrade9Active(active); }
};

} // namespace


BOOST_AUTO_TEST_SUITE(token_transaction_tests)

BOOST_FIXTURE_TEST_CASE(test_chips, TokenTransactionTestingSetup) {
    RunTestsForChip("cashtokens");
}

BOOST_AUTO_TEST_SUITE_END()
