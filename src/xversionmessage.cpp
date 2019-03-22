// Copyright (C) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "xversionmessage.h"
#include "hash.h"
#include "random.h"
#include "streams.h"
#include "util.h"


bool xversion_deterministic_hashing = false;

XMapSaltedHasher::XMapSaltedHasher()
    : k0(xversion_deterministic_hashing ? 0x1122334455667788UL : GetRand(std::numeric_limits<uint64_t>::max())),
      k1(xversion_deterministic_hashing ? 0x99aabbccddeeff00UL : GetRand(std::numeric_limits<uint64_t>::max()))
{
}

uint64_t XMapSaltedHasher::operator()(const uint64_t key) const
{
    CSipHasher hasher(k0, k1);
    return hasher.Write(key).Finalize();
}

uint64_t CXVersionMessage::as_u64c(const uint64_t k) const
{
    if (xmap.count(k) == 0)
        return 0;
    if (cache_u64c.count(k) == 0)
    {
        const std::vector<uint8_t> &vec = xmap.at(k);
        uint64_t v = 0;
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
    }
    return cache_u64c.at(k);
}

void CXVersionMessage::set_u64c(const uint64_t key, const uint64_t val)
{
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << COMPACTSIZE(val);

    std::vector<uint8_t> vec;
    vec.insert(vec.begin(), s.begin(), s.end());
    xmap[key] = vec;
    cache_u64c[key] = val;
}
