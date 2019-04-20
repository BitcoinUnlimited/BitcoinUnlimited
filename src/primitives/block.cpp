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
        mtx.size());
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
    return 0; // FIXME!!
    /*
    size_t mem = 0; // FIXME! memusage::DynamicUsage(mtx);
    for (const auto &tx : mtx)
    {
        mem += memusage::DynamicUsage(tx) + ::RecursiveDynamicUsage(*tx);
    }
    return mem;*/
}

struct NumericallyLessTxHashComparator
{
public:
    bool operator()(const CTransactionRef &a, const CTransactionRef &b) const { return a->GetHash() < b->GetHash(); }
};


void CBlock::sortLTOR(const bool no_dups)
{
    CPersistentTransactionMap mtxnew;
    if (no_dups)
    {
        std::vector<CTransactionRef> vtx;
        for (auto iter : *this)
            vtx.emplace_back(iter);
        std::random_shuffle(vtx.begin(), vtx.end());
        for (auto txref : vtx)
            mtxnew = mtxnew.insert(CTransactionSlot(txref), txref);
    }
    else
    {
        /* some tests use blocks with duplicate transactions,
           e.g. txvalidationcache_tests.  To not break any tests, also support
           the old way of sorting (instead of relying on the
           persistent_map intrinsic order) for now. */
        std::vector<CTransactionRef> vtx;
        for (auto iter : *this)
            vtx.emplace_back(iter);
        std::sort(vtx.begin() + 1, vtx.end(), NumericallyLessTxHashComparator());
        size_t i = 0;
        for (auto txref : vtx)
            mtxnew = mtxnew.insert(CTransactionSlot(txref, i++), txref);
    }
    mtx = mtxnew;
}

std::string CTransactionSlot::ToString() const
{
    return strprintf("(slot:%d, %s)", idx, tx == nullptr ? "(null)" : tx->GetHash().GetHex());
}

bool CTransactionSlot::operator<(const CTransactionSlot &other) const
{
    // semantics: index overrides hash always. IsCoinbase() takes precedence over Hash value
    // this means if all slots are set to 'ignore idx' (e.g. -1) the result should be CTOR order.
    // if tx == nullptr, this takes precedence over IsCoinbase().
    if (idx < 0)
    {
        if (other.idx < 0)
        {
            if (tx == nullptr)
                return other.tx != nullptr;
            else if (other.tx == nullptr)
                return false;
            if (tx->IsCoinBase())
                return !other.tx->IsCoinBase();
            else if (other.tx->IsCoinBase())
                return false;
            else
                return tx->GetHash() < other.tx->GetHash();
        }
        else
            return false; // a set idx value is always coming first
    }
    else
    {
        if (other.idx < 0)
            return true; // same
        return idx < other.idx;
    }
}
