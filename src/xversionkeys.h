// Copyright (C) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef BITCOIN_XVERSIONKEYS_H
#define BITCOIN_XVERSIONKEYS_H

#include <unordered_map>
#include <unordered_set>

#define ADD_UL(key, ul) key##ul

#define MAKE_KEY_EXPERIMENTAL(suffix)   ADD_UL(0x00000000##suffix, UL)
#define MAKE_KEY_BCHN(suffix)           ADD_UL(0x00000001##suffix, UL)
#define MAKE_KEY_BU(suffix)             ADD_UL(0x00000002##suffix, UL)

// this is a similar system to how we calculate client version

#define XVERSION_MAJOR      0
#define XVERSION_MINOR      1
#define XVERSION_REVISION   0

#define XVERSION_VERSION_VALUE ((10000 * XVERSION_MAJOR ) + (100 * XVERSION_MINOR) + (1 * XVERSION_REVISION))

namespace XVer
{

enum {
// keys ending with _OLD are kept for backwards compat until the HF in november
    BU_LISTEN_PORT_OLD                          = 0x0000000000020000UL,
    BU_GRAPHENE_MAX_VERSION_SUPPORTED_OLD       = 0x0000000000020001UL,
    BU_MSG_IGNORE_CHECKSUM_OLD                  = 0x0000000000020002UL,
    BU_XTHIN_VERSION_OLD                        = 0x0000000000020003UL,
    BU_GRAPHENE_FAST_FILTER_PREF_OLD            = 0x0000000000020004UL,
    BU_GRAPHENE_MIN_VERSION_SUPPORTED_OLD       = 0x0000000000020005UL,
    BU_MEMPOOL_SYNC_OLD                         = 0x0000000000020006UL,
    BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED_OLD   = 0x0000000000020007UL,
    BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED_OLD   = 0x0000000000020008UL,
    BU_MEMPOOL_ANCESTOR_COUNT_LIMIT_OLD         = 0x0000000000020009UL,
    BU_MEMPOOL_ANCESTOR_SIZE_LIMIT_OLD          = 0x000000000002000aUL,
    BU_MEMPOOL_DESCENDANT_COUNT_LIMIT_OLD       = 0x000000000002000bUL,
    BU_MEMPOOL_DESCENDANT_SIZE_LIMIT_OLD        = 0x000000000002000cUL,
    BU_TXN_CONCATENATION_OLD                    = 0x000000000002000dUL,
// there is a gap here from 000d to f00d
    BU_ELECTRUM_SERVER_PORT_TCP_OLD             = 0x000000000002f00dUL,
    BU_ELECTRUM_SERVER_PROTOCOL_VERSION_OLD     = 0x000000000002f00eUL,
// the 0.1.0 xversion spec uses 64 bit keys
    XVERSION_VERSION_KEY                    = 0x0000000000000000UL,
    BU_LISTEN_PORT                          = MAKE_KEY_BU(00000000),
    BU_GRAPHENE_MAX_VERSION_SUPPORTED       = MAKE_KEY_BU(00000001),
    BU_MSG_IGNORE_CHECKSUM                  = MAKE_KEY_BU(00000002),
    BU_XTHIN_VERSION                        = MAKE_KEY_BU(00000003),
    BU_GRAPHENE_FAST_FILTER_PREF            = MAKE_KEY_BU(00000004),
    BU_GRAPHENE_MIN_VERSION_SUPPORTED       = MAKE_KEY_BU(00000005),
    BU_MEMPOOL_SYNC                         = MAKE_KEY_BU(00000006),
    BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED   = MAKE_KEY_BU(00000007),
    BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED   = MAKE_KEY_BU(00000008),
    BU_MEMPOOL_ANCESTOR_COUNT_LIMIT         = MAKE_KEY_BU(00000009),
    BU_MEMPOOL_ANCESTOR_SIZE_LIMIT          = MAKE_KEY_BU(0000000a),
    BU_MEMPOOL_DESCENDANT_COUNT_LIMIT       = MAKE_KEY_BU(0000000b),
    BU_MEMPOOL_DESCENDANT_SIZE_LIMIT        = MAKE_KEY_BU(0000000c),
    BU_TXN_CONCATENATION                    = MAKE_KEY_BU(0000000d),
    // there is a gap here from 000d to f00d
    BU_ELECTRUM_SERVER_PORT_TCP             = MAKE_KEY_BU(0000f00d),
    BU_ELECTRUM_SERVER_PROTOCOL_VERSION     = MAKE_KEY_BU(0000f00e),
}; // enum keys


#define GetKeyName(key) #key

enum {
                xvt_u64c,
}; // enum valtypes


// This map is not actually used right now
const std::unordered_map<uint64_t, int> valtype = {
    {                    XVERSION_VERSION_KEY,  xvt_u64c },
    {             BU_ELECTRUM_SERVER_PORT_TCP,  xvt_u64c },
    {     BU_ELECTRUM_SERVER_PROTOCOL_VERSION,  xvt_u64c },
    {            BU_GRAPHENE_FAST_FILTER_PREF,  xvt_u64c },
    {       BU_GRAPHENE_MAX_VERSION_SUPPORTED,  xvt_u64c },
    {       BU_GRAPHENE_MIN_VERSION_SUPPORTED,  xvt_u64c },
    {                          BU_LISTEN_PORT,  xvt_u64c },
    {         BU_MEMPOOL_ANCESTOR_COUNT_LIMIT,  xvt_u64c },
    {          BU_MEMPOOL_ANCESTOR_SIZE_LIMIT,  xvt_u64c },
    {       BU_MEMPOOL_DESCENDANT_COUNT_LIMIT,  xvt_u64c },
    {        BU_MEMPOOL_DESCENDANT_SIZE_LIMIT,  xvt_u64c },
    {                         BU_MEMPOOL_SYNC,  xvt_u64c },
    {   BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED,  xvt_u64c },
    {   BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED,  xvt_u64c },
    {                  BU_MSG_IGNORE_CHECKSUM,  xvt_u64c },
    {                    BU_TXN_CONCATENATION,  xvt_u64c },
    {                        BU_XTHIN_VERSION,  xvt_u64c },
}; // const unordered_map valtype



const std::unordered_set<uint64_t> setChangableKeys =
{
}; // const unordered_map keytype

inline bool IsChangableKey(const int64_t &key)
{
    return setChangableKeys.count(key);
}

} // namespace XVer

#endif
