// Copyright (C) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "xversionmessage.h"
#include "hashwrapper.h"
#include "random.h"
#include "streams.h"
#include "util.h"

uint64_t CXVersionMessage::as_u64c(const uint64_t k) const
{
    LOCK(cacheProtector);
    const auto xmap_iter = xmap.find(k);
    if (xmap_iter == xmap.end())
    {
        // we dont have that key, assuming zero
        return 0;
    }
    const auto cache_iter = cache_u64c.find(k);
    if (cache_iter == cache_u64c.end())
    {
        uint64_t v = 0;
        const std::vector<uint8_t> &vec = xmap_iter->second;
        try
        {
            CDataStream s(vec, SER_NETWORK, PROTOCOL_VERSION);
            s >> COMPACTSIZE(v);
        }
        catch (...)
        {
            LOG(NET, "Error reading extended configuration key %016llx as u64c. Assuming zero.\n", k);
            v = 0;
        }
        cache_u64c[k] = v;
        return v;
    }
    return cache_iter->second;
}

void CXVersionMessage::set_u64c(const uint64_t key, const uint64_t val)
{
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << COMPACTSIZE(val);

    std::vector<uint8_t> vec;
    vec.insert(vec.begin(), s.begin(), s.end());

    LOCK(cacheProtector);
    xmap[key] = std::move(vec);
    cache_u64c[key] = val;
}
