// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "banentry.h"

/**
* Default constructor initializes all member variables to "null" equivalents
*/
CBanEntry::CBanEntry()
{
    // set all member variables to null equivalents
    SetNull();
}

/**
* This constructor initializes all member variables to "null" equivalents,
* except for the ban creation time, which is set to the value passed in.
*
* @param[in] nCreateTimeIn Creation time to initialize this CBanEntry to
*/
CBanEntry::CBanEntry(int64_t nCreateTimeIn)
{
    // set all member variables to null equivalents
    SetNull();
    nCreateTime = nCreateTimeIn;
}

/**
* Set all member variables to their "null" equivalent values
*/
void CBanEntry::SetNull()
{
    nVersion = CBanEntry::CURRENT_VERSION;
    nCreateTime = 0;
    nBanUntil = 0;
    banReason = BanReasonUnknown;
    userAgent.clear();
}

/**
* Converts the BanReason to a human readable string representation.
*
* @return Human readable string representation of the BanReason
*/
std::string CBanEntry::banReasonToString()
{
    switch (banReason)
    {
    case BanReasonNodeMisbehaving:
        return "Node Misbehaving";
    case BanReasonManuallyAdded:
        return "Manually Banned";
    case BanReasonTooManyEvictions:
        return "Too Many Evictions";
    case BanReasonTooManyConnectionAttempts:
        return "Too Many Connection Attempts";
    case BanReasonInvalidMessageStart:
        return "Invalid Message Start";
    case BanReasonInvalidInventory:
        return "Invalid Inventory";
    case BanReasonInvalidPeer:
        return "Invalid Peer for this Network";
    default:
        return "unknown";
    }
}
