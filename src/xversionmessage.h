// Copyright (C) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_XVERSIONMESSAGE_H
#define BITCOIN_XVERSIONMESSAGE_H


#include "protocol.h"
#include "serialize.h"
#include "sync.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include <map>
#include <string>
#include <vector>

/** Maximum length of strSubVer in `version` message */
static const unsigned int MAX_SUBVERSION_LENGTH = 256;

const size_t MAX_XVERSION_MAP_SIZE = 100000;

typedef std::map<uint64_t, std::vector<uint8_t> > XVersionMap;

class CompactMapSerialization
{
    XVersionMap &map;

public:
    CompactMapSerialization(XVersionMap &_map) : map(_map) {}
    template <typename Stream>
    void Serialize(Stream &s) const
    {
        WriteCompactSize<Stream>(s, map.size());
        for (auto &kv : map)
        {
            WriteCompactSize<Stream>(s, kv.first);
            s << kv.second;
        }
    }

    template <typename Stream>
    void Unserialize(Stream &s)
    {
        size_t n = ReadCompactUint64<Stream>(s);
        for (size_t i = 0; i < n; i++)
        {
            uint64_t k;
            std::vector<uint8_t> v;
            k = ReadCompactUint64<Stream>(s);
            s >> v;
            map[k] = v;
        }
    }
};


/*!
  Bitcoin Cash extended version message implementation.

  This version message de-/serializes the same fields as the version
  message format as in the BU BCH implementation as of July 2018,
  but in addition supports an appended
  (key, value) map, with the keys and values being uint64_t values.

  The keys are declared in the xversion_keys.h header file which
  should obviously be kept in sync between different implementations.

  A size of 100kB for the serialized map must not be exceeded.

  FIXME: Auto-generate the xversion_keys.h file to also support non-C++
  clients and other software decoding these fields.
*/
class CXVersionMessage
{
protected:
    // cached values for conversion to uint64 (u64c)
    mutable CCriticalSection cacheProtector;
    mutable std::map<uint64_t, uint64_t> cache_u64c;

public:
    // extensible map for general settings
    XVersionMap xmap;

    CXVersionMessage() {}
    ADD_SERIALIZE_METHODS;

    //! Access xmap by given uint64_t key. Non-existent entries will return zero
    uint64_t as_u64c(const uint64_t k) const;

    // complement to as_u64c for building map
    void set_u64c(const uint64_t key, const uint64_t val);

    template <typename Stream>
    const auto CheckSize(Stream &s)
    {
        if (s.size() > MAX_XVERSION_MAP_SIZE)
        {
            throw std::ios_base::failure(
                strprintf("A version message xmap may be at most %d bytes.", MAX_XVERSION_MAP_SIZE));
        }
    };


    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        // checking the stream size in unserialize (ForRead()) only works when we are unserializing
        // a stream that only holds this one object in it. This is guaranteed to be the only object
        // in the stream when unserializing a network message in net_processing but is not
        // a guarantee of streams in general. If this is not the only object in the stream, this
        // check might return a false positive with regards to being larger than the max size
        if (ser_action.ForRead())
        {
            CheckSize(s);
        }
        READWRITE(REF(CompactMapSerialization(xmap)));
        if (!ser_action.ForRead())
        {
            CheckSize(s);
        }
    }
};

#endif
