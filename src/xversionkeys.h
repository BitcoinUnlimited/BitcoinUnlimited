// Copyright (C) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef BITCOIN_XVERSIONKEYS_H
#define BITCOIN_XVERSIONKEYS_H

#include <unordered_map>
#include <unordered_set>

#define EXP_VER_PREFIX      0x0000
#define BCHN_PREFIX         0x0001
#define BU_PREFIX           0x0002


/// old header


namespace XVer
{

enum {
                   BU_ELECTRUM_SERVER_PORT_TCP = 0x000000000002f00dUL,
           BU_ELECTRUM_SERVER_PROTOCOL_VERSION = 0x000000000002f00eUL,
                  BU_GRAPHENE_FAST_FILTER_PREF = 0x0000000000020004UL,
             BU_GRAPHENE_MAX_VERSION_SUPPORTED = 0x0000000000020001UL,
             BU_GRAPHENE_MIN_VERSION_SUPPORTED = 0x0000000000020005UL,
                                BU_LISTEN_PORT = 0x0000000000020000UL,
               BU_MEMPOOL_ANCESTOR_COUNT_LIMIT = 0x0000000000020009UL,
                BU_MEMPOOL_ANCESTOR_SIZE_LIMIT = 0x000000000002000aUL,
             BU_MEMPOOL_DESCENDANT_COUNT_LIMIT = 0x000000000002000bUL,
              BU_MEMPOOL_DESCENDANT_SIZE_LIMIT = 0x000000000002000cUL,
                               BU_MEMPOOL_SYNC = 0x0000000000020006UL,
         BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED = 0x0000000000020008UL,
         BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED = 0x0000000000020007UL,
                        BU_MSG_IGNORE_CHECKSUM = 0x0000000000020002UL,
                          BU_TXN_CONCATENATION = 0x000000000002000dUL,
                              BU_XTHIN_VERSION = 0x0000000000020003UL,
}; // enum keys


#define GetKeyName(key) #key

enum {
                xvt_u64c,
}; // enum valtypes



const std::unordered_map<uint64_t, int> valtype = {
    {             BU_ELECTRUM_SERVER_PORT_TCP,                                   xvt_u64c },
    {     BU_ELECTRUM_SERVER_PROTOCOL_VERSION,                                   xvt_u64c },
    {            BU_GRAPHENE_FAST_FILTER_PREF,                                   xvt_u64c },
    {       BU_GRAPHENE_MAX_VERSION_SUPPORTED,                                   xvt_u64c },
    {       BU_GRAPHENE_MIN_VERSION_SUPPORTED,                                   xvt_u64c },
    {                          BU_LISTEN_PORT,                                   xvt_u64c },
    {         BU_MEMPOOL_ANCESTOR_COUNT_LIMIT,                                   xvt_u64c },
    {          BU_MEMPOOL_ANCESTOR_SIZE_LIMIT,                                   xvt_u64c },
    {       BU_MEMPOOL_DESCENDANT_COUNT_LIMIT,                                   xvt_u64c },
    {        BU_MEMPOOL_DESCENDANT_SIZE_LIMIT,                                   xvt_u64c },
    {                         BU_MEMPOOL_SYNC,                                   xvt_u64c },
    {   BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED,                                   xvt_u64c },
    {   BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED,                                   xvt_u64c },
    {                  BU_MSG_IGNORE_CHECKSUM,                                   xvt_u64c },
    {                    BU_TXN_CONCATENATION,                                   xvt_u64c },
    {                        BU_XTHIN_VERSION,                                   xvt_u64c },
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
