// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATE_TX_H
#define BITCOIN_VALIDATE_TX_H

#include "coins.h"
#include "consensus/validation.h"
#include "parallel.h"
#include "primitives/transaction.h"
#include "txadmission.h"
#include "txmempool.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

bool VerifyTransactionWithMemoryPool(CTxMemPool &pool,
    CValidationState &state,
    const CTransactionRef &ptx,
    bool fLimitFree,
    bool *pfMissingInputs,
    bool fOverrideMempoolLimit,
    bool fRejectAbsurdFee,
    TransactionClass allowedTx,
    CValidationDebugger *debugger = nullptr);

#endif
