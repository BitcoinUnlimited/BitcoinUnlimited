// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/mempool.h>

#include <validation/forks.h>

uint32_t GetBCHDefaultAncestorLimit(const Consensus::Params &params, const CBlockIndex *pindexPrev)
{
    return IsMay2020Enabled(params, pindexPrev) ? BCH_DEFAULT_ANCESTOR_LIMIT_LONGER
                                                : BCH_DEFAULT_ANCESTOR_LIMIT;
}

uint32_t GetBCHDefaultDescendantLimit(const Consensus::Params &params, const CBlockIndex *pindexPrev)
{
    return IsMay2020Enabled(params, pindexPrev) ? BCH_DEFAULT_DESCENDANT_LIMIT_LONGER
                                                : BCH_DEFAULT_DESCENDANT_LIMIT;
}
