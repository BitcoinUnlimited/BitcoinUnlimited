// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Utility functions shared by unit tests
 */
#ifndef BITCOIN_TEST_TESTUTIL_H
#define BITCOIN_TEST_TESTUTIL_H

#include "fs.h"

fs::path GetTempPath();

class CMutableTransaction;
class CScript;

void RandomScript(CScript &script);
void RandomTransaction(CMutableTransaction &tx, bool fSingle);

#endif // BITCOIN_TEST_TESTUTIL_H
