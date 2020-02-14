// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutil.h"

#ifdef WIN32
#include <shlobj.h>
#endif

#include "fs.h"
#include "key.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"

fs::path GetTempPath() { return fs::temp_directory_path(); }
CMutableTransaction CreateRandomTx()
{
    CKey key;
    key.MakeNewKey(true);

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.n = 0;
    tx.vin[0].prevout.hash = InsecureRand256();
    tx.vin[0].scriptSig << OP_1;
    tx.vout.resize(1);
    tx.vout[0].nValue = 1 * CENT;
    tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    return tx;
}
