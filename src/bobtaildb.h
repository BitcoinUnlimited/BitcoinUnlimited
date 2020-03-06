// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BOBTAILDB_H
#define BITCOIN_BOBTAILDB_H

#include "dbwrapper.h"
#include "uint256.h"

#include <string>
#include <vector>

/** Access to the bobtail proof database (bobtail/) */
class CBobtailDB : public CDBWrapper
{
public:
    CBobtailDB(size_t nCacheSize, std::string folder, bool fMemory = false, bool fWipe = false);

private:
    CBobtailDB(const CBobtailDB &);
    void operator=(const CBobtailDB &);

public:
    bool ReadProof(const uint256 &block_hash, std::vector<uint256> proofs);
    bool WriteProof(const uint256 &block_hash, const std::vector<uint256> &proofs);
    bool LoadProofs();
};

#endif
