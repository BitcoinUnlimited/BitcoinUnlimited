// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "subblock.h"

void CSubBlock::SetNull()
{
    CBlockHeader::SetNull();
    vtx.clear();
}

bool CSubBlock::IsNull() const
{
    return (vtx.empty() && CBlockHeader::IsNull());
}

CBlockHeader CSubBlock::GetBlockHeader() const
{
    CBlockHeader block;
    block.nVersion = nVersion;
    block.hashPrevBlock = hashPrevBlock;
    block.hashMerkleRoot = hashMerkleRoot;
    block.nTime = nTime;
    block.nBits = nBits;
    block.nNonce = nNonce;
    return block;
}

std::string CSubBlock::ToString() const
{
    std::stringstream s;
    s << strprintf(
        "CSubBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(), nVersion, hashPrevBlock.ToString(), hashMerkleRoot.ToString(), nTime, nBits, nNonce,
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i]->ToString() << "\n";
    }
    return s.str();
}

std::set<uint256> CSubBlock::GetAncestorHashes() const
{
    std::set<uint256> ancestors;
    if (vtx.empty())
    {
        return ancestors;
    }
    if (vtx[0]->IsProofBase() == false)
    {
        return ancestors;
    }
    for (auto &input : vtx[0]->vin)
    {
        ancestors.emplace(input.prevout.hash);
    }
    return ancestors;
}

std::vector<uint256> CSubBlock::GetTxHashes() const
{
    std::vector<uint256> hashes;
    for (const auto &txref : vtx)
    {
        hashes.push_back(txref->GetHash());
    }

    return hashes;
}
