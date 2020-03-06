// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_POLICY_MEMPOOL_H
#define BITCOIN_POLICY_MEMPOOL_H

/** BU's default for -limitancestorcount, max number of in-mempool ancestors */
static const unsigned int BU_DEFAULT_ANCESTOR_LIMIT = 500;
/** BU's default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors. */
static const unsigned int BU_DEFAULT_ANCESTOR_SIZE_LIMIT = 2020;
/** BU's default for -limitdescendantcount, max number of in-mempool descendants */
static const unsigned int BU_DEFAULT_DESCENDANT_LIMIT = 500;
/** BU's default for -limitdescendantsize, maximum kilobytes of in-mempool descendants. */
static const unsigned int BU_DEFAULT_DESCENDANT_SIZE_LIMIT = 2020;


/** Network default for the max number of in-mempool ancestors */
static const unsigned int BCH_DEFAULT_ANCESTOR_LIMIT = 25;
/** Network Default for the maximum kilobytes of tx + all in-mempool ancestors */
static const unsigned int BCH_DEFAULT_ANCESTOR_SIZE_LIMIT = 101;
/** Network default for the max number of in-mempool descendants */
static const unsigned int BCH_DEFAULT_DESCENDANT_LIMIT = 25;
/** Default for the maximum kilobytes of in-mempool descendants */
static const unsigned int BCH_DEFAULT_DESCENDANT_SIZE_LIMIT = 101;

#endif // BITCOIN_POLICY_MEMPOOL_H
