// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOKEN_GROUPS_H
#define TOKEN_GROUPS_H

#include "chainparams.h"
#include "coins.h"
#include "consensus/validation.h"
#include "pubkey.h"
#include <unordered_map>
class CWallet;

/** Transaction cannot be committed on my fork */
static const unsigned int REJECT_GROUP_IMBALANCE = 0x104;

// The definitions below are used internally.  They are defined here for use in unit tests.
class CTokenGroupID
{
protected:
    std::vector<unsigned char> data;

public:
    //* no token group, which is distinct from the bitcoin token group
    CTokenGroupID() {}
    //* for special token groups, of which there is currently only the bitcoin token group (0)
    CTokenGroupID(unsigned char c) : data(1) { data[0] = c; }
    //* handles CKeyID and CScriptID
    CTokenGroupID(const uint160 &id) : data(ToByteVector(id)) {}
    //* handles single mint group id, and possibly future larger size CScriptID
    CTokenGroupID(const uint256 &id) : data(ToByteVector(id)) {}
    //* Assign the groupID from a vector
    CTokenGroupID(const std::vector<unsigned char> &id) : data(id)
    {
        // for the conceivable future there is no possible way a group could be bigger but the spec does allow larger
        DbgAssert(id.size() < OP_PUSHDATA1, );
    }

    void NoGroup(void) { data.resize(0); }
    bool operator==(const CTokenGroupID &id) const { return data == id.data; }
    bool operator!=(const CTokenGroupID &id) const { return data != id.data; }
    //* returns true if this is a user-defined group -- ie NOT bitcoin cash or no group
    bool isUserGroup(void) const;

    const std::vector<unsigned char> &bytes(void) const { return data; }
    //* Convert this token group ID into a mint/melt address
    // CTxDestination ControllingAddress(txnouttype addrType) const;
    //* Returns this groupID as a string in cashaddr format
    // std::string Encode(const CChainParams &params = Params()) const;
};

namespace std
{
template <>
struct hash<CTokenGroupID>
{
public:
    size_t operator()(const CTokenGroupID &s) const
    {
        const std::vector<unsigned char> &v = s.bytes();
        int sz = v.size();
        if (sz >= 4)
            return (v[0] << 24) | (v[1] << 16) | (v[2] << 8) << v[3];
        else if (sz > 0)
            return v[0]; // It would be better to return all bytes but sizes 1 to 3 currently unused
        else
            return 0;
    }
};
}

class CTokenGroupInfo
{
public:
    CTokenGroupInfo() : associatedGroup(), mintMeltGroup(), quantity(0), invalid(true) {}
    CTokenGroupInfo(const CTokenGroupID &associated, const CTokenGroupID &mintable, CAmount qty = 0)
        : associatedGroup(associated), mintMeltGroup(mintable), quantity(qty), invalid(false)
    {
    }
    CTokenGroupInfo(const CKeyID &associated, const CKeyID &mintable, CAmount qty = 0)
        : associatedGroup(associated), mintMeltGroup(mintable), quantity(qty), invalid(false)
    {
    }
    // Return the controlling (can mint and burn) and associated (OP_GROUP in script) group of a script
    CTokenGroupInfo(const CScript &script);

    CTokenGroupID associatedGroup; // The group announced by the script (or the bitcoin group if no OP_GROUP)
    CTokenGroupID mintMeltGroup; // The script's address
    CAmount quantity; // The number of tokens specified in this script
    bool invalid;
    bool operator==(const CTokenGroupInfo &g)
    {
        return ((associatedGroup == g.associatedGroup) && (mintMeltGroup == g.mintMeltGroup));
    }
};

// Verify that the token groups in this transaction properly balance
bool CheckTokenGroups(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &view);

// Return true if any output in this transaction is part of a group
bool IsAnyTxOutputGrouped(const CTransaction &tx);

// Serialize a CAmount into an array of bytes.
// This serialization does not store the length of the serialized data within the serialized data.
// It is therefore useful only within a system that already identifies the length of this field (such as a CScript).
std::vector<unsigned char> SerializeAmount(CAmount num);

// Deserialize a CAmount from an array of bytes.
// This function uses the size of the vector to determine how many bytes were used in serialization.
// It is therefore useful only within a system that already identifies the length of this field (such as a CScript).
CAmount DeserializeAmount(std::vector<unsigned char> &vec);

// Convenience function to just extract the group from a script
inline CTokenGroupID GetTokenGroup(const CScript &script) { return CTokenGroupInfo(script).associatedGroup; }
extern CTokenGroupID NoGroup;

#endif
