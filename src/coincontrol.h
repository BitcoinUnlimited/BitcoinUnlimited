// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINCONTROL_H
#define BITCOIN_COINCONTROL_H

#include "primitives/transaction.h"
#include "pubkey.h" // Required for CTxDestination
#include "script/standard.h" // CTxDestination, CNoDestination

/** Coin Control Features. */
class CCoinControl
{
public:
    CTxDestination destChange;
    //! If false, allows unselected inputs, but requires all selected inputs be used
    bool fAllowOtherInputs;
    //! Includes watch only addresses which match the ISMINE_WATCH_SOLVABLE criteria
    bool fAllowWatchOnly;
    //! Minimum absolute fee (not per kilobyte)
    CAmount nMinimumTotalFee;
    //! Allow spending of coins that have tokens on them
    bool m_allow_tokens;
    //! Only select coins that have tokens on them (requires m_allow_tokens == true)
    bool m_tokens_only;

    CCoinControl() { SetNull(); }
    void SetNull()
    {
        destChange = CNoDestination();
        fAllowOtherInputs = false;
        fAllowWatchOnly = false;
        setSelected.clear();
        nMinimumTotalFee = 0;
        m_allow_tokens = false;
        m_tokens_only = false;
    }

    bool HasSelected() const { return (setSelected.size() > 0); }
    bool IsSelected(const uint256 &hash, unsigned int n) const
    {
        COutPoint outpt(hash, n);
        return (setSelected.count(outpt) > 0);
    }

    void Select(const COutPoint &output) { setSelected.insert(output); }
    void UnSelect(const COutPoint &output) { setSelected.erase(output); }
    void UnSelectAll() { setSelected.clear(); }
    void ListSelected(std::vector<COutPoint> &vOutpoints) const
    {
        vOutpoints.assign(setSelected.begin(), setSelected.end());
    }

    void SetTokensOnly(const bool tokens_only)
    {
        m_tokens_only = tokens_only;
        if (tokens_only)
        {
            m_allow_tokens = true;
        }
    }

private:
    std::set<COutPoint> setSelected;
};

#endif // BITCOIN_COINCONTROL_H
