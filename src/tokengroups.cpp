// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <algorithm>
#include "base58.h"
#include "coins.h"
#include "consensus/validation.h"
#include "coincontrol.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "pubkey.h"
#include "cashaddrenc.h"
#include "dstencode.h"
#include "random.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h"
#include "tokengroups.h"

/* Grouped transactions look like this:

GP2PKH:

OP_DATA(group address)
OP_GROUP
OP_DROP
OP_DUP
OP_HASH160
OP_DATA(pubkeyhash)
OP_EQUALVERIFY
OP_CHECKSIG

GP2SH:

OP_DATA(group address)
OP_GROUP
OP_DROP
OP_HASH160 [20-byte-hash-value] OP_EQUAL

FUTURE: GP2SH version 2:

OP_DATA(group address)
OP_GROUP
OP_DROP
OP_HASH256 [32-byte-hash-value] OP_EQUAL
*/

CTokenGroupID BitcoinGroup(0);  // the "native" bitcoin cash group id is one byte 0.  This is only used internally

bool IsAnyTxOutputGrouped(const CTransaction &tx)
{
    for (const CTxOut &txout : tx.vout)
    {
        CTokenGroupPair grp = GetTokenGroup(txout.scriptPubKey);
        if (grp.associatedGroup != BitcoinGroup) return true;
    }

return false;
}


CTokenGroupPair GetTokenGroup(const CScript& script)
{
    CTokenGroupPair ret;
    CScript::const_iterator pc = script.begin();
    std::vector<unsigned char> data;
    std::vector<unsigned char> data2;
    opcodetype opcode;

    // The destination address could also be the group so I need to get it
    CTxDestination address;
    txnouttype whichType;
    if (!ExtractDestinationAndType(script, address, whichType))
    {
        ret.mintMeltGroup.NoGroup();
    }
    else
    {
        // only certain well known script types are allowed to mint or melt
        if ((whichType == TX_PUBKEYHASH) || (whichType == TX_SCRIPTHASH) || (whichType == TX_GRP_PUBKEYHASH) ||
            (whichType == TX_GRP_SCRIPTHASH))
            ret.mintMeltGroup = address;
    }

    if (!script.GetOp(pc, opcode, data))
    {
        // empty script is the bitcoin group
        ret.associatedGroup = BitcoinGroup;
        return ret;
    }
    opcodetype opcode2;
    if (!script.GetOp(pc, opcode2, data2))
    {
        DbgAssert(!"empty script", ); // this should be impossible since that means script with 1 byte
        ret.associatedGroup = BitcoinGroup;
        return ret;
    }
    // The script does not begin with the correct data size or the OP_GROUP prefix
    // so its the bitcoin group
    if (((opcode != 0x14) && (opcode != 0x20)) || (opcode2 != OP_GROUP))
    {
        ret.associatedGroup = BitcoinGroup;
        return ret;
    }

    ret.associatedGroup = data;
    return ret;
}


// local class that just keeps track of the amounts of each group coming into and going out of a transaction
class CBalance
{
public:
    CBalance():mintable(0), meltable(0), input(0),output(0) {}
    CTokenGroupPair groups; // possible groups
    CAmount mintable;
    CAmount meltable;
    CAmount input;
    CAmount output;
};

bool CheckTokenGroups(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &view)
{
    std::unordered_map<CTokenGroupID,CBalance> gBalance;

    // Iterate through all the outputs constructing the final balances of every group.
    for (const auto& outp: tx.vout)
    {
        const CScript &scriptPubKey = outp.scriptPubKey;
        const CAmount amount = outp.nValue;
        CTokenGroupPair tokenGrp = GetTokenGroup(scriptPubKey);
        gBalance[tokenGrp.associatedGroup].output += amount;
    }

    // Now iterate through the inputs applying them to match outputs.
    // If any input utxo address matches a non-bitcoin group address, defer since this could be a mint or burn
    for (const auto& inp: tx.vin)
    {
        const COutPoint &prevout = inp.prevout;
        const Coin &coin = view.AccessCoin(prevout);
        if (coin.IsSpent())  // should never happen because you've already CheckInputs(tx,...)
        {
            DbgAssert(!"Checking token group for spent coin", );
            return state.Invalid(false, REJECT_INVALID, "already-spent");
        }
        const CScript &script = coin.out.scriptPubKey;
        CAmount amount = coin.out.nValue;
        CTokenGroupPair tokenGrp = GetTokenGroup(script);
        bool possibleBurn = false;
        if (tokenGrp.associatedGroup == BitcoinGroup) // Minting can only happen from raw bitcoin tokens
        {
            auto item = gBalance.find(tokenGrp.mintMeltGroup);
            if (item != gBalance.end()) // this address exists as a group output, so this input could be mint
            {
                gBalance[tokenGrp.mintMeltGroup].mintable += amount;
                amount = 0;
            }
            else  // the address does not exist as a group output so this must be a normal bitcoin xfer
            {
                gBalance[BitcoinGroup].input += amount;
                amount = 0;
            }
        }
        else
        {
            if (tokenGrp.associatedGroup == tokenGrp.mintMeltGroup)
                possibleBurn = true;

            auto item = gBalance.find(tokenGrp.associatedGroup);  // get this input's group
            if (item == gBalance.end())  // no output group matches the input, so this must be a burn
            {
                if (!possibleBurn) // but the tx isn't signed by the group id so burn is illegal
                {
                    return state.Invalid(false, REJECT_GROUP_IMBALANCE, "grp-invalid-burn", "Group: Token burn is not signed by group id");
                }
                else
                {
                    gBalance[BitcoinGroup].input += amount;
                    amount = 0;
                }
            }
            else // this is either a burn or a normal group input
            {
                if (possibleBurn)
                {
                    item->second.meltable += amount;
                    amount = 0;
                }
                else
                {
                    item->second.input += amount;
                    amount = 0;
                }
            }
        }
        DbgAssert(amount == 0, );
    }

    // Now pass thru the outputs deciding what to do with the mintable and meltable coins
    for (auto& txo: gBalance)
    {
        if (txo.first == BitcoinGroup) continue;
        CBalance& bal = txo.second;
        if (bal.input < bal.output)  // coins must be minted or melted
        {
            CAmount diff = bal.output - bal.input;
            CAmount mint = std::min(diff, bal.mintable);
            bal.mintable -= mint;  // mint what we need into the group
            bal.input += mint;
            diff -= mint;
            CAmount noburn = std::min(diff, bal.meltable);  // If we need more, don't burn some of the meltable
            bal.meltable -= noburn;
            bal.input += noburn;
            diff -= noburn;

            if (bal.input != bal.output)
            {
                return state.Invalid(false, REJECT_GROUP_IMBALANCE, "grp-invalid-mint", "Group output exceeds input, including all mintable");
            }
        }
        else if (bal.input > bal.output)
        {
           return state.Invalid(false, REJECT_GROUP_IMBALANCE, "grp-invalid-mint", "Group input exceeds output, including all meltable");
        }

        // Assign what we didn't use to the bitcoin group
        gBalance[BitcoinGroup].input += bal.mintable;
        bal.mintable = 0;
        gBalance[BitcoinGroup].input += bal.meltable;
        bal.mintable = 0;
    }

    if (gBalance[BitcoinGroup].input < gBalance[BitcoinGroup].output)
    {
        return state.Invalid(false, REJECT_GROUP_IMBALANCE, "grp-invalid-tx", "Group transaction imbalance");
    }

    return true;
}


class CTxDestinationTokenGroupExtractor: public boost::static_visitor<CTokenGroupID>
{
public:
    CTokenGroupID operator()(const CKeyID &id) const
    {
        return CTokenGroupID(id);
    }

    CTokenGroupID operator()(const CScriptID &id) const
    {
        return CTokenGroupID(id);
    }

    CTokenGroupID operator()(const CNoDestination &) const
    { return CTokenGroupID();
    }
};

CTokenGroupID::CTokenGroupID(const CTxDestination& id)
{
    *this = boost::apply_visitor(CTxDestinationTokenGroupExtractor(), id);
}

bool CTokenGroupID::isUserGroup(void) const
{
    return !((data.size()==0) || (*this == BitcoinGroup));
}
