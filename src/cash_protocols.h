// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CASH_PROTOCOLS_H
#define BITCOIN_CASH_PROTOCOLS_H

#include <stdint.h>

/* clang-format off */

enum CASH_PROTOCOLS : uint32_t
{
    TOKENDA             = 0x00000010,
    TOKENIZED           = 0x00000020,
    BCHTORRENTS         = 0X0000005C,
    GAMECHAIN_LOBBY     = 0X00001337,
    SATCHAT             = 0X000015B3,
    TURINGNOTE          = 0X000022B8,
    KEOKEN_PLATFORM     = 0X00004B50,
    TRADELAYER          = 0X0000544C,
    BOOKCHAIN           = 0X0000B006,
    GAMECHAIN           = 0X00031337,
    OFGP                = 0X00666770,
    BCML                = 0X00434D4C,
    ORACLE_DATA         = 0X004F5243,
    BITCOINFILES        = 0X00504642,
    SLP                 = 0x00504c53,   // Simple_Ledger_Protocol
    SSP                 = 0x005171C0,   // Silico_Signing_Protocol
    CHAINBET            = 0X00544542,
    UNISOT              = 0X00555354,
    COUNTERPARTY_CASH   = 0X00584350,
    BCHAN               = 0X00626368,
    CASHSLIDE           = 0X006D7367,
    KEYPORT             = 0X00746C6B,
    CASH_ACCOUNTS       = 0X01010101,
    SATOSHIDICE         = 0X02446365,
    BCH_DNS             = 0X04008080,
    TOKENGROUPS         = 0X054C5638,
    WORMHOLE            = 0X08776863
};
/* clang-format on */

#endif
