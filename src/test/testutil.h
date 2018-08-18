// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Utility functions shared by unit tests
 */
#ifndef BITCOIN_TEST_TESTUTIL_H
#define BITCOIN_TEST_TESTUTIL_H

#include "fs.h"
#include "primitives/block.h"

fs::path GetTempPath();

class CTransaction;
class CMutableTransaction;
class CScript;

void RandomScript(CScript &script);

//! fCoinbase_like: Make the transaction so that it has a single input with
//  prevout being a null hash and the input number being zero as well.
//! vInputs: Inputs to select from (or None)
void RandomTransaction(CMutableTransaction &tx,
    bool fSingle,
    bool fCoinbase_like = false,
    std::vector<std::pair<uint256, uint32_t> > *pvInputs = nullptr);


//! Create block of size ntx transaction with a fraction of 'dependent' dependent transactions
//! The transactions in this block will not full any further validation rules, however
// they'll be in the block in topological order
CBlockRef RandomBlock(const size_t ntx, float dependent);

#endif // BITCOIN_TEST_TESTUTIL_H
