// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutil.h"
#include "consensus/merkle.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "test/test_random.h"

#ifdef WIN32
#include <shlobj.h>
#endif

#include "fs.h"

fs::path GetTempPath() { return fs::temp_directory_path(); }
void RandomScript(CScript &script)
{
    static const opcodetype oplist[] = {
        OP_FALSE, OP_1, OP_2, OP_3, OP_CHECKSIG, OP_IF, OP_VERIF, OP_RETURN, OP_CODESEPARATOR};
    script = CScript();
    int ops = (insecure_rand() % 10);
    for (int i = 0; i < ops; i++)
        script << oplist[insecure_rand() % (sizeof(oplist) / sizeof(oplist[0]))];
}

void RandomTransaction(CMutableTransaction &tx,
    bool fSingle,
    bool fCoinbase_like,
    std::vector<std::pair<uint256, uint32_t> > *pvInputs)
{
    tx.nVersion = insecure_rand();
    tx.vin.clear();
    tx.vout.clear();
    tx.nLockTime = (insecure_rand() % 2) ? insecure_rand() : 0;
    int ins = (insecure_rand() % 4) + 1;
    int outs = fSingle ? ins : (insecure_rand() % 4) + 1;
    for (int in = 0; in < ins; in++)
    {
        tx.vin.push_back(CTxIn());
        CTxIn &txin = tx.vin.back();
        if (fCoinbase_like)
        {
            txin.prevout.SetNull();
            break;
        }
        else if (pvInputs != nullptr && pvInputs->size())
        {
            std::pair<uint256, uint32_t> prev = pvInputs->back();
            pvInputs->pop_back();
            txin.prevout.hash = prev.first;
            txin.prevout.n = prev.second;
        }
        else
        {
            txin.prevout.hash = GetRandHash();
            txin.prevout.n = insecure_rand() % 4;
        }
        RandomScript(txin.scriptSig);
        txin.nSequence = (insecure_rand() % 2) ? insecure_rand() : (unsigned int)-1;
    }
    for (int out = 0; out < outs; out++)
    {
        tx.vout.push_back(CTxOut());
        CTxOut &txout = tx.vout.back();
        txout.nValue = insecure_rand() % 100000000;
        RandomScript(txout.scriptPubKey);
    }
}

inline double insecure_randf() { return (double)insecure_rand() / (double)0xffffffff; }
CBlockRef RandomBlock(const size_t ntx, float dependent)
{
    CBlockRef block = std::make_shared<CBlock>();
    std::vector<std::pair<uint256, uint32_t> > unconsumed_outputs;

    CMutableTransaction ctx;
    RandomTransaction(ctx, false, true); // coinbase, do not add its outputs to unconsumed outputs
    block->vtx.push_back(MakeTransactionRef(ctx));

    for (size_t i = 0; i < ntx - 1; i++)
    {
        CMutableTransaction tx;
        if (insecure_randf() < dependent)
            // NOTE/FIXME: further bias / oddity here in that a dependent transaction
            // is usually suddenly dependent on a lot of differen txn.
            RandomTransaction(tx, false, false, &unconsumed_outputs);
        else
            RandomTransaction(tx, false, false);

        uint256 hash = tx.GetHash();

        for (size_t i = 0; i < tx.vout.size(); i++)
            unconsumed_outputs.push_back(std::pair<uint256, uint32_t>(hash, i));

        // every so often, randomize inputs taken (FIXME: crude...)
        if (insecure_randf() < 0.01)
            random_shuffle(unconsumed_outputs.begin(), unconsumed_outputs.end());
        block->vtx.push_back(MakeTransactionRef(tx));
    }
    block->hashMerkleRoot = BlockMerkleRoot(*block);
    return block;
}
