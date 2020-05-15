// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <policy/policy.h>
#include <rpc/blockchain.h>
#include <txmempool.h>

#include <univalue.h>

#include <list>
#include <vector>


static void AddTx(const CTransactionRef &tx, const CAmount &nFee, CTxMemPool &pool)
{
    int64_t nTime = 0;
    double dPriority = 10.0;
    unsigned int nHeight = 1;
    bool spendsCoinbase = false;
    unsigned int sigOpCost = 4;
    LockPoints lp;
    pool.addUnchecked(tx->GetHash(), CTxMemPoolEntry(tx, nFee, nTime, dPriority, nHeight, pool.HasNoInputsOf(tx),
                                         tx->GetValueOut(), spendsCoinbase, sigOpCost, lp));
}

static void RpcMempool(benchmark::State &state)
{
    CTxMemPool pool(CFeeRate(1000));

    for (int i = 0; i < 1000; ++i)
    {
        CMutableTransaction tx = CMutableTransaction();
        tx.vin.resize(1);
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vout.resize(1);
        tx.vout[0].scriptPubKey = CScript() << OP_1 << OP_EQUAL;
        tx.vout[0].nValue = i * CENT;
        const CTransactionRef tx_r{MakeTransactionRef(tx)};
        AddTx(tx_r, /* fee */ i * CENT, pool);
    }

    while (state.KeepRunning())
    {
        (void)mempoolToJSON(true);
    }
}

BENCHMARK(RpcMempool, 40);
