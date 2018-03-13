// Copyright (c) 2016-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOKEN_GROUP_RPC_H
#define TOKEN_GROUP_RPC_H

#include "chainparams.h"
#include "coins.h"
#include "consensus/tokengroups.h"
#include "consensus/validation.h"
#include "pubkey.h"
#include "script/standard.h"
#include <unordered_map>


#endif

// Pass a group and a destination address (or CNoDestination) to get the balance of all outputs in the group
// or all outputs in that group and on that destination address.
CAmount GetGroupBalance(const CTokenGroupID &grpID, const CTxDestination &dest, const CWallet *wallet);

// Returns a mapping of groupID->balance
void GetAllGroupBalances(const CWallet *wallet, std::unordered_map<CTokenGroupID, CAmount> &balances);

// Token group helper functions -- not members because they use objects not available in the consensus lib
//* Initialize the group id from an address
CTokenGroupID GetTokenGroup(const CTxDestination &id);
//* Initialize a group ID from a string representation
CTokenGroupID GetTokenGroup(const std::string &cashAddrGrpId, const CChainParams &params = Params());
// Return the associated group (OP_GROUP) of a script
CTokenGroupID GetTokenGroup(const CScript &script);
CTxDestination ControllingAddress(const CTokenGroupID &grp, txnouttype addrType);
std::string EncodeTokenGroup(const CTokenGroupID &grp, const CChainParams &params = Params());
