// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bobtail.h"

bool IsSubBlockMalformed(const CSubBlock &subblock)
{
    if (subblock.IsNull())
    {
        return true;
    }
    // at a minimum a subblock needs a proofbase transaction to be valid
    if (subblock.vtx.size() == 0)
    {
        return true;
    }
    if (subblock.vtx[0]->IsProofBase() == false)
    {
        return true;
    }
    size_t size_vtx = subblock.vtx.size();
    for (size_t i = 1; i < size_vtx; ++i)
    {
        if (subblock.vtx[i]->IsProofBase())
        {
            return true;
        }
    }
    return false;
}
