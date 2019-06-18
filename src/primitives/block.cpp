// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"
#include <iostream>

#include "core_memusage.h"
#include "crypto/common.h"
#include "hashwrapper.h"
#include "memusage.h"
#include "serialize.h"
#include "streams.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
uint256 CBlockHeader::GetHash() const { return SerializeHash(*this); }
std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf(
        "CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, ntx=%u)\n",
        GetHash().ToString(), nVersion, hashPrevBlock.ToString(), hashMerkleRoot.ToString(), nTime, nBits, nNonce,
        vtx.size());
    for (CTransactionRef txref : *this)
        s << "  " << txref->ToString() << "\n";
    return s.str();
}

uint64_t CBlock::GetBlockSize() const
{
    if (nBlockSize == 0)
        nBlockSize = ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
    return nBlockSize;
}

size_t CBlock::RecursiveDynamicUsage() const
{
    size_t mem = memusage::DynamicUsage(vtx);
    for (const auto &tx : vtx)
    {
        mem += memusage::DynamicUsage(tx) + ::RecursiveDynamicUsage(*tx);
    }
    return mem;
}

struct NumericallyLessTxHashComparator
{
public:
    bool operator()(const CTransactionRef &a, const CTransactionRef &b) const { return a->GetHash() < b->GetHash(); }
};


void CBlock::sortLTOR() { std::sort(vtx.begin() + 1, vtx.end(), NumericallyLessTxHashComparator()); }
