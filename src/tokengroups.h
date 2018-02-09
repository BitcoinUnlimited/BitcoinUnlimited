// Copyright (c) 2016-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOKEN_GROUPS_H
#define TOKEN_GROUPS_H

#include "pubkey.h"
#include "chainparams.h"
#include "script/standard.h"
#include "consensus/validation.h"
class CWallet;

/** Transaction cannot be committed on my fork */
static const unsigned int REJECT_GROUP_IMBALANCE = 0x104;

// Verify that the token groups in this transaction properly balance
bool CheckTokenGroups(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &view);

// Return true if any output in this transaction is part of a group
bool IsAnyTxOutputGrouped(const CTransaction &tx);

// The definitions below are used internally.  They are defined here for use in unit tests.
class CTokenGroupID
{
protected:
    std::vector<unsigned char> data;

public:
    //* no token group, which is distinct from the bitcoin token group
    CTokenGroupID() {}
    //* for special token groups, of which there is currently only the bitcoin token group (0)
    CTokenGroupID(unsigned char c): data(1) { data[0] = c; }
    //* handles CKeyID and CScriptID
    CTokenGroupID(const uint160 &id) : data(ToByteVector(id)) {}
    //* Will handle the future longer CScriptID
    CTokenGroupID(const uint256 &id) : data(ToByteVector(id)) {}
    //* Assign the groupID from a vector
    CTokenGroupID(const std::vector<unsigned char>& id) : data(id)
    {
        // for the conceivable future there is no possible way a group could be bigger but the spec does allow larger
        DbgAssert(id.size() < OP_PUSHDATA1, );
    }
    //* Initialize the group id from an address
    CTokenGroupID(const CTxDestination &id);
    //* Initialize a group ID from a string representation
    CTokenGroupID(const std::string &cashAddrGrpId,const CChainParams &params=Params());

    void NoGroup(void) { data.resize(0); }
    bool operator==(const CTokenGroupID &id) const { return data == id.data; }
    bool operator!=(const CTokenGroupID &id) const { return data != id.data; }

    //* returns true if this is a user-defined group -- ie NOT bitcoin cash or no group
    bool isUserGroup(void) const;

    const std::vector<unsigned char>& bytes(void) const { return data; }

    //* Convert this token group ID into a mint/melt address
    CTxDestination ControllingAddress() const;
    //* Returns this groupID as a string in cashaddr format
    std::string Encode(const CChainParams &params=Params());
};

// Return the associated group (OP_GROUP) of a script
CTokenGroupID GetTokenGroup(const CScript& script);

namespace std
{
template <>
struct hash<CTokenGroupID>
{
public:
    size_t operator()(const CTokenGroupID &s) const
    {
        const std::vector<unsigned char>& v = s.bytes();
        int sz = v.size();
        if (sz >= 4)
          return (v[0] << 24) | (v[1] << 16) | (v[2] << 8) << v[3];
        else if (sz > 0) return v[0];  // It would be better to return all bytes but sizes 1 to 3 currently unused
        else return 0;
    }
};
}

class CTokenGroupPair
{
public:
    CTokenGroupPair():associatedGroup(),mintMeltGroup() {}
    CTokenGroupPair(const CTokenGroupID& associated, const CTokenGroupID& mintable):associatedGroup(associated), mintMeltGroup(mintable) {}
    CTokenGroupPair(const CKeyID& associated, const CKeyID& mintable):associatedGroup(associated), mintMeltGroup(mintable) {}
    CTokenGroupID associatedGroup;  // The group announced by the script (or the bitcoin group if no OP_GROUP)
    CTokenGroupID mintMeltGroup;    // The script's address
    bool operator == (const CTokenGroupPair& g)
       { return ((associatedGroup == g.associatedGroup) && (mintMeltGroup == g.mintMeltGroup)); }
};

// Return the controlling (can mint and burn) and associated (OP_GROUP in script) group of a script
CTokenGroupPair GetTokenGroupPair(const CScript& script);


// Pass a group and a destination address (or CNoDestination) to get the balance of all outputs in the group
// or all outputs in that group and on that destination address.
CAmount GetGroupBalance(const CTokenGroupID &grpID, const CTxDestination &dest, const CWallet *wallet);

extern CTokenGroupID BitcoinGroup;

#endif
