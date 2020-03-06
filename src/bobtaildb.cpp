// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bobtaildb.h"
#include "threadgroup.h" // for shutdown_threads

#include <map>
#include <utility>

extern std::map<uint256, std::vector<uint256> > mapBobtailProofs;

static const char DB_BLOCK_PROOFS = 'p';

CBobtailDB::CBobtailDB(size_t nCacheSize, std::string folder, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "bobtail", nCacheSize, fMemory, fWipe)
{
}

bool CBobtailDB::ReadProof(const uint256 &block_hash, std::vector<uint256> proofs)
{
    return Read(std::make_pair(DB_BLOCK_PROOFS, block_hash), proofs);
}

bool CBobtailDB::WriteProof(const uint256 &block_hash, const std::vector<uint256> &proofs)
{
    return Write(std::make_pair(DB_BLOCK_PROOFS, block_hash), proofs);
}

bool CBobtailDB::LoadProofs()
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_BLOCK_PROOFS, uint256()));

    // Load mapBlockIndex
    while (pcursor->Valid())
    {
        if (shutdown_threads.load() == true)
        {
            return false;
        }
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_PROOFS)
        {
            std::vector<uint256> proofs;
            if (pcursor->GetValue(proofs))
            {
                mapBobtailProofs.emplace(key.second, proofs);
                pcursor->Next();
            }
            else
            {
                return error("LoadProofs() : failed to read value");
            }
        }
        else
        {
            break;
        }
    }
    return true;
}
