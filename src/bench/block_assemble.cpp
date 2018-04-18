// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
#include <pow.h>
#include <test/test_bitcoin.h>
#include <txadmission.h>
#include <validation/validation.h>

#include <boost/thread.hpp>

#include <list>
#include <vector>

std::shared_ptr<CBlock> PrepareBlock(const CScript &coinbase_scriptPubKey, const CChainParams &chainparams)
{
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    auto block = std::make_shared<CBlock>(BlockAssembler(chainparams).CreateNewBlock(coinbase_scriptPubKey)->block);
    block->nTime = chainActive.Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}

static CTxIn MineBlock(const CScript &coinbase_scriptPubKey, const CChainParams &chainparams)
{
    auto block = PrepareBlock(coinbase_scriptPubKey, chainparams);

    block->nTime = chainActive.Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);
    while (!CheckProofOfWork(block->GetHash(), block->nBits, chainparams.GetConsensus()))
    {
        ++block->nNonce;
        assert(block->nNonce);
    }

    CValidationState state;
    // need to copy the object corresponding by the shared_pointer because
    // ProcessNewBlock accept plain pointer to a block not shared one.
    auto aux_block = *block;
    bool processed = ProcessNewBlock(state, chainparams, nullptr, &aux_block, true, nullptr, false);
    assert(processed);
    assert(state.IsValid());

    return CTxIn{block->vtx[0]->GetHash(), 0};
}


static void AssembleBlock(benchmark::State &state)
{
    TestingSetup test_setup(CBaseChainParams::REGTEST);
    const CChainParams &chainparams = Params(CBaseChainParams::REGTEST);

    const CScript redeemScript = CScript() << OP_DROP << OP_TRUE;
    const CScript SCRIPT_PUB = CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemScript)) << OP_EQUAL;

    const CScript scriptSig = CScript() << std::vector<uint8_t>(100, 0xff) << ToByteVector(redeemScript);

    // Collect some loose transactions that spend the coinbases of our mined blocks
    constexpr size_t NUM_BLOCKS{200};
    std::array<CTransactionRef, NUM_BLOCKS - COINBASE_MATURITY + 1> txs;
    for (size_t b = 0; b < NUM_BLOCKS; ++b)
    {
        CMutableTransaction tx;
        tx.vin.push_back(MineBlock(SCRIPT_PUB, chainparams));
        tx.vin.back().scriptSig = scriptSig;
        tx.vout.emplace_back(49.999 * COIN, SCRIPT_PUB);
        if (NUM_BLOCKS - b >= COINBASE_MATURITY)
        {
            txs.at(b) = MakeTransactionRef(tx);
        }
    }

    {
        // Required for AcceptToMemoryPool.
        LOCK(cs_main);

        for (const auto &txr : txs)
        {
            CValidationState vstate;
            bool ret{AcceptToMemoryPool(mempool, vstate, txr, false, /* fLimitFree */
                nullptr /* pfMissingInputs */, false, /* fOverrideMempoolLimit */
                true, /* fRejectAbsurdFee */
                TransactionClass::DEFAULT)};
            assert(ret);
        }
    }

    while (state.KeepRunning())
    {
        PrepareBlock(SCRIPT_PUB, chainparams);
    }
}

BENCHMARK(AssembleBlock, 700);
