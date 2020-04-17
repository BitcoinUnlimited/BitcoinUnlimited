// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Utility functions shared by unit tests
 */
#ifndef BITCOIN_TEST_TESTUTIL_H
#define BITCOIN_TEST_TESTUTIL_H

#include "fs.h"

struct CMutableTransaction;

fs::path GetTempPath();
CMutableTransaction CreateRandomTx();

#endif // BITCOIN_TEST_TESTUTIL_H
