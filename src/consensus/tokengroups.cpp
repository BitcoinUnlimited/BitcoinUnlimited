// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "tokengroups.h"
#include "base58.h"
#include "cashaddrenc.h"
#include "coincontrol.h"
#include "coins.h"
#include "consensus/validation.h"
#include "dstencode.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "random.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/standard.h"
#include "unlimited.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h"
#include <algorithm>

CTokenGroupID NoGroup; // No group specified.

bool IsAnyTxOutputGrouped(const CTransaction &tx)
{
    for (const CTxOut &txout : tx.vout)
    {
        CTokenGroupInfo grp(txout.scriptPubKey);
        if (grp.invalid)
            return true; // Its still grouped even if invalid
        if (grp.associatedGroup != NoGroup)
            return true;
    }

    return false;
}

std::vector<unsigned char> SerializeAmount(CAmount num)
{
    if (num < 0)
        throw std::ios_base::failure("SerializeAmount(): negative number");
    CDataStream strm(SER_NETWORK, CLIENT_VERSION);
    if (num < 256)
    {
        ser_writedata8(strm, num);
    }
    else if (num <= std::numeric_limits<unsigned short>::max())
    {
        ser_writedata16(strm, num);
    }
    else if (num <= std::numeric_limits<unsigned int>::max())
    {
        ser_writedata32(strm, num);
    }
    else
    {
        ser_writedata64(strm, num);
    }
    return std::vector<unsigned char>(strm.begin(), strm.end());
}

CAmount DeserializeAmount(std::vector<unsigned char> &vec)
{
    int sz = vec.size();
    if (sz == 1)
    {
        return vec[0];
    }
    CDataStream strm(vec, SER_NETWORK, CLIENT_VERSION);
    if (sz == 2)
    {
        return ser_readdata16(strm);
    }
    if (sz == 4)
    {
        return ser_readdata32(strm);
    }
    if (sz == 8)
    {
        uint64_t v = ser_readdata64(strm);
        // DeserializeAmount is only allowed to return a positive number
        // If the unsigned quantity overflows the CAmount maximum, then its an error
        if (v > std::numeric_limits<CAmount>::max())
            throw std::ios_base::failure("DeserializeAmount(): overflow");
        return v;
    }
    throw std::ios_base::failure("DeserializeAmount(): invalid format");
}


CTokenGroupID ExtractControllingGroup(const CScript &scriptPubKey)
{
    txnouttype whichType;
    typedef std::vector<unsigned char> valtype;
    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return CTokenGroupID();

    // only certain well known script types are allowed to mint or melt
    if ((whichType == TX_PUBKEYHASH) || (whichType == TX_GRP_PUBKEYHASH) || (whichType == TX_SCRIPTHASH) ||
        (whichType == TX_GRP_SCRIPTHASH))
    {
        return CTokenGroupID(uint160(vSolutions[0]));
    }
    return CTokenGroupID();
}

CTokenGroupInfo::CTokenGroupInfo(const CScript &script)
    : associatedGroup(), mintMeltGroup(), quantity(0), invalid(false)
{
    CScript::const_iterator pc = script.begin();
    std::vector<unsigned char> groupId;
    std::vector<unsigned char> tokenQty;
    std::vector<unsigned char> data;
    opcodetype opcode;
    opcodetype opcodeGrp;
    opcodetype opcodeQty;

    mintMeltGroup = ExtractControllingGroup(script);

    if (!script.GetOp(pc, opcodeGrp, groupId))
    {
        associatedGroup = NoGroup;
        return;
    }

    if (!script.GetOp(pc, opcodeQty, tokenQty))
    {
        DbgAssert(!"empty script", ); // this should be impossible since that means script with 1 byte
        associatedGroup = NoGroup;
        return;
    }

    if (!script.GetOp(pc, opcode, data))
    {
        DbgAssert(!"empty script", ); // this should be impossible since that means script with 1 byte
        associatedGroup = NoGroup;
        return;
    }

    if (opcode != OP_GROUP)
    {
        associatedGroup = NoGroup;
        return;
    }
    else // If OP_GROUP is used, enforce rules on the other fields
    {
        // group must be 20 or 32 bytes
        if ((opcodeGrp != 0x14) && (opcodeGrp != 0x20))
        {
            invalid = true;
            return;
        }
        // quantity must be 1, 2, 4, or 8 bytes
        if ((opcodeQty != 1) && (opcodeQty != 2) && (opcodeQty != 4) && (opcodeQty != 8))
        {
            invalid = true;
            return;
        }
    }

    try
    {
        quantity = DeserializeAmount(tokenQty);
    }
    catch (std::ios_base::failure &f)
    {
        invalid = true;
    }
    associatedGroup = groupId;
}


// local class that just keeps track of the amounts of each group coming into and going out of a transaction
class CBalance
{
public:
    CBalance() : mintMelt(false), input(0), output(0) {}
    CTokenGroupInfo groups; // possible groups
    bool mintMelt;
    CAmount input;
    CAmount output;
};

bool CheckTokenGroups(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &view)
{
    std::unordered_map<CTokenGroupID, CBalance> gBalance;
    // This is an optimization allowing us to skip single-mint hashes if there are no output groups
    bool anyOutputGroups = false;

    // Iterate through all the outputs constructing the final balances of every group.
    for (const auto &outp : tx.vout)
    {
        const CScript &scriptPubKey = outp.scriptPubKey;
        CTokenGroupInfo tokenGrp(scriptPubKey);
        if (tokenGrp.invalid)
            return state.Invalid(false, REJECT_INVALID, "bad OP_GROUP");
        if (tokenGrp.associatedGroup != NoGroup)
        {
            // Underflow below zero is redundant since negative numbers are disallowed in deserialization,
            // but is included here for clarity.  There may be some interesting use cases for 0 so allow it.
            DbgAssert(tokenGrp.quantity >= 0, return state.Invalid(false, REJECT_INVALID, "bad OP_GROUP"));
            if (std::numeric_limits<CAmount>::max() - gBalance[tokenGrp.associatedGroup].output < tokenGrp.quantity)
                return state.Invalid(false, REJECT_INVALID, "token overflow");
            gBalance[tokenGrp.associatedGroup].output += tokenGrp.quantity;
            anyOutputGroups = true;
        }
    }

    // Now iterate through the inputs applying them to match outputs.
    // If any input utxo address matches a non-bitcoin group address, defer since this could be a mint or burn
    for (const auto &inp : tx.vin)
    {
        const COutPoint &prevout = inp.prevout;
        const Coin &coin = view.AccessCoin(prevout);
        if (coin.IsSpent()) // should never happen because you've already CheckInputs(tx,...)
        {
            DbgAssert(!"Checking token group for spent coin", );
            return state.Invalid(false, REJECT_INVALID, "already-spent");
        }
        // no prior coins can be grouped.
        if (coin.nHeight < miningEnforceOpGroup.value)
            continue;
        const CScript &script = coin.out.scriptPubKey;
        CTokenGroupInfo tokenGrp(script);
        // The prevout should never be invalid because that would mean that this node accepted a block with an
        // invalid OP_GROUP tx in it.
        if (tokenGrp.invalid)
            continue;
        CAmount amount = tokenGrp.quantity;
        if (tokenGrp.mintMeltGroup != NoGroup)
        {
            gBalance[tokenGrp.mintMeltGroup].mintMelt = true;
        }
        if (tokenGrp.associatedGroup != NoGroup)
        {
            if (std::numeric_limits<CAmount>::max() - gBalance[tokenGrp.associatedGroup].input < amount)
                return state.Invalid(false, REJECT_INVALID, "token overflow");
            gBalance[tokenGrp.associatedGroup].input += amount;
        }

        if (anyOutputGroups)
        {
            // Implement a limited quantity token via a one-time mint operation
            // by minting to the sha256 of a COutPoint.  A COutPoint provides entropy (is extremely likely to be unique)
            // because it contains the sha256 of the input tx and an index.
            CHashWriter oneTimeGrp(SER_GETHASH, PROTOCOL_VERSION);
            oneTimeGrp << prevout;
            CTokenGroupID otg(oneTimeGrp.GetHash());

            auto item = gBalance.find(otg); // Is there an output to this group?
            if (item != gBalance.end()) // If so, indicate that it can be minted
            {
                item->second.mintMelt = true;
            }
        }
    }

    // Now pass thru the outputs ensuring balance or mint/melt permission
    for (auto &txo : gBalance)
    {
        CBalance &bal = txo.second;
        if (!bal.mintMelt && (bal.input != bal.output))
        {
            return state.Invalid(false, REJECT_GROUP_IMBALANCE, "grp-invalid-mint",
                "Group output exceeds input, including all mintable");
        }
    }

    return true;
}


bool CTokenGroupID::isUserGroup(void) const { return (!data.empty()); }
