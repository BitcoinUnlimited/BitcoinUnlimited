// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "tinyformat.h"
#include "tweak.h"
extern CTweak<unsigned int> txWalletDust; // minimum wallet tx output size
extern CTweak<unsigned int> nDustThreshold; // minimum "standard" tx output size (below this won't relay)

CFeeRate::CFeeRate(const CAmount &nFeePaid, size_t nSize)
{
    if (nSize > 0)
        nSatoshisPerK = nFeePaid * 1000 / nSize;
    else
        nSatoshisPerK = 0;
}

CAmount CFeeRate::GetFee(size_t nSize) const
{
    CAmount nFee = nSatoshisPerK * nSize / 1000;

    if (nFee == 0 && nSatoshisPerK > 0)
        nFee = nSatoshisPerK;

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%08d %s/kB", nSatoshisPerK / COIN, nSatoshisPerK % COIN, CURRENCY_UNIT);
}


CAmount CFeeRate::GetDust() const
{
    CAmount dust = txWalletDust.Value();
    // If dust has not been configured, then
    // "Dust" is defined in terms of CTransaction::minRelayTxFee, which has units satoshis-per-kilobyte.
    // If you'd pay more than 1/3 in fees to spend something, then we consider it dust.
    // A typical spendable txout is 34 bytes big, and will need a CTxIn of at least 148 bytes to spend:
    if (dust == 0)
        dust = 3 * minRelayTxFee.GetFee(TYPICAL_UTXO_LIFECYCLE_SIZE);
    return std::max(dust, (CAmount)nDustThreshold.Value());
}
