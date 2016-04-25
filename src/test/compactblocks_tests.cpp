// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/compactblock.h"
#include "chainparams.h"
#include "consensus/merkle.h"
#include "random.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

class HasReason
{
public:
    HasReason(const std::string &reason) : m_reason(reason) {}
    bool operator()(const std::invalid_argument &e) const
    {
        return std::string(e.what()).find(m_reason) != std::string::npos;
    };

private:
    const std::string m_reason;
};

struct RegtestingSetup : public TestingSetup
{
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(blockencodings_tests, RegtestingSetup)

static CBlock TestBlock() {
    CBlock block;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(1);
    tx.vout[0].nValue = 42;

    block.vtx.resize(3);
    block.vtx[0] = MakeTransactionRef(tx);
    block.nVersion = 42;
    block.hashPrevBlock = GetRandHash();
    block.nBits = 0x207fffff;

    tx.vin[0].prevout.hash = GetRandHash();
    tx.vin[0].prevout.n = 0;
    block.vtx[1] = MakeTransactionRef(tx);

    tx.vin.resize(10);
    for (size_t i = 0; i < tx.vin.size(); i++) {
        tx.vin[i].prevout.hash = GetRandHash();
        tx.vin[i].prevout.n = 0;
    }
    block.vtx[2] = MakeTransactionRef(tx);

    bool mutated;
    block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);
    assert(!mutated);
    while (!CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;
    return block;
}

BOOST_AUTO_TEST_CASE(TransactionsRequestSerializationTest)
{
    CompactReRequest req1;
    req1.blockhash = GetRandHash();
    req1.indexes.resize(4);
    req1.indexes[0] = 0;
    req1.indexes[1] = 1;
    req1.indexes[2] = 3;
    req1.indexes[3] = 4;

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << req1;

    CompactReRequest req2;
    stream >> req2;

    BOOST_CHECK_EQUAL(req1.blockhash.ToString(), req2.blockhash.ToString());
    BOOST_CHECK_EQUAL(req1.indexes.size(), req2.indexes.size());
    BOOST_CHECK_EQUAL(req1.indexes[0], req2.indexes[0]);
    BOOST_CHECK_EQUAL(req1.indexes[1], req2.indexes[1]);
    BOOST_CHECK_EQUAL(req1.indexes[2], req2.indexes[2]);
    BOOST_CHECK_EQUAL(req1.indexes[3], req2.indexes[3]);
}

BOOST_AUTO_TEST_CASE(validate_compact_block)
{
    CBlock block = TestBlock(); // valid block
    CompactBlock a(block);
    BOOST_CHECK_NO_THROW(validateCompactBlock(a));

    // invalid header
    CompactBlock b = a;
    b.header.SetNull();
    BOOST_ASSERT(b.header.IsNull());
    BOOST_CHECK_THROW(validateCompactBlock(b), std::invalid_argument);

    // null tx in prefilled
    CompactBlock c = a;
    c.prefilledtxn.at(0).tx = CTransaction();
    BOOST_CHECK_THROW(validateCompactBlock(c), std::invalid_argument);

    // overflowing index
    CompactBlock d = a;
    d.prefilledtxn.push_back(d.prefilledtxn[0]);
    assert(d.prefilledtxn.size() == size_t(2));
    d.prefilledtxn.at(0).index = 1;
    d.prefilledtxn.at(1).index = std::numeric_limits<uint16_t>::max();
    BOOST_CHECK_EXCEPTION(
        validateCompactBlock(d), std::invalid_argument, HasReason("tx index overflows"));

    // too high index
    CompactBlock e = a;
    e.prefilledtxn.at(0).index = std::numeric_limits<uint16_t>::max() / 2;
    BOOST_CHECK_EXCEPTION(
        validateCompactBlock(e), std::invalid_argument, HasReason("invalid index for tx"));

    // no transactions
    CompactBlock f = a;
    f.shorttxids.clear();
    f.prefilledtxn.clear();
    BOOST_CHECK_THROW(validateCompactBlock(f), std::invalid_argument);
}


BOOST_AUTO_TEST_SUITE_END()
