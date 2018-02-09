// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <algorithm>
#include "base58.h"
#include "coins.h"
#include "consensus/validation.h"
#include "coincontrol.h"
#include "primitives/transaction.h"
#include "rpc/protocol.h"
#include "script/script.h"
#include "script/standard.h"
#include "pubkey.h"
#include "cashaddrenc.h"
#include "dstencode.h"
#include "random.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h"
#include "rpc/server.h"
#include "tokengroups.h"

extern CChain chainActive;
extern CCriticalSection cs_main;
bool EnsureWalletIsAvailable(bool avoidException);

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

// Approximate size of signature in a script -- used for guessing fees
const unsigned int TX_SIG_SCRIPT_LEN = 72;

bool IsAnyTxOutputGrouped(const CTransaction &tx)
{
    for (const CTxOut &txout : tx.vout)
    {
        CTokenGroupPair grp = GetTokenGroupPair(txout.scriptPubKey);
        if (grp.associatedGroup != BitcoinGroup) return true;
    }

return false;
}

CTokenGroupID GetTokenGroup(const CScript& script)
{
    return GetTokenGroupPair(script).associatedGroup;
}

CTokenGroupPair GetTokenGroupPair(const CScript& script)
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
        CTokenGroupPair tokenGrp = GetTokenGroupPair(scriptPubKey);
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
        CTokenGroupPair tokenGrp = GetTokenGroupPair(script);
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

CTokenGroupID::CTokenGroupID(const std::string &addr, const CChainParams &params)
{
    CashAddrContent cac = DecodeCashAddrContent(addr, params);
    if (cac.type == CashAddrType::GROUP_TYPE)
        data = cac.hash;
    // otherwise it becomes NoGroup (i.e. data is size 0)
}

std::string CTokenGroupID::Encode(const CChainParams &params)
{
    return EncodeCashAddr(data, CashAddrType::GROUP_TYPE, params);
}

CTxDestination CTokenGroupID::ControllingAddress() const
{
    // TODO figure out whether this is a script or p2pkh address
    return CTxDestination(CKeyID(uint160(data)));
}

class CGroupScriptVisitor : public boost::static_visitor<bool>
{
private:
    CScript *script;
    CTokenGroupID group;
public:
    CGroupScriptVisitor(CTokenGroupID grp, CScript *scriptin):group(grp) { script = scriptin; }
    bool operator()(const CNoDestination &dest) const
    {
        script->clear();
        return false;
    }

    bool operator()(const CKeyID &keyID) const
    {
        script->clear();
        if (group.isUserGroup())
        {
            *script << group.bytes() << OP_GROUP << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        }
        else
        {
            *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        }
        return true;
    }

    bool operator()(const CScriptID &scriptID) const
    {
        script->clear();
        if (group.isUserGroup())
        {
            *script << group.bytes() << OP_GROUP << OP_DROP << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        }
        else
        {
            *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        }
        return true;
    }
};


CAmount GetGroupBalance(const CTokenGroupID &grpID, const CTxDestination &dest, const CWallet *wallet)
{
    std::vector<COutput> coins;
    wallet->FilterCoins(coins, [grpID, dest](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupPair tg = GetTokenGroupPair(out->scriptPubKey);
        if (grpID == tg.associatedGroup) // must be sitting in group address
        {
            if (dest == CTxDestination(CNoDestination()))
                return true;
            // To determine whether dest is in the output script,
            // I don't want to evaluate the script again because GetTokenGroupPair has already done so.
            // But it "groupified" the destination address, so groupify the destination just so we can easily
            // compare whether they are equal.
            if (CTokenGroupID(dest) == tg.mintMeltGroup)
                return true;
        }
        return false;
    });
    CAmount totalAvailable = 0;
    for (auto coin : coins)
    {
        totalAvailable += coin.tx->vout[coin.i].nValue;
    }
    return totalAvailable;
}

CScript GetScriptForDestination(const CTxDestination &dest, const CTokenGroupID& group)
{
    CScript script;

    boost::apply_visitor(CGroupScriptVisitor(group, &script), dest);
    return script;
}

static CAmount AmountFromSatoshiValue(const UniValue &value)
{
    if (!value.isNum() && !value.isStr())
        throw std::runtime_error("Amount is not a number or string");
    CAmount amount;
    if (!ParseFixedPoint(value.getValStr(), 0, &amount))
        throw std::runtime_error("Invalid amount");
    if (!MoneyRange(amount))
        throw std::runtime_error("Amount out of range");
    return amount;
}


// extracts a common RPC call parameter pattern.  Returns curparam.
static unsigned int ParseGroupAddrValue(const UniValue &params,
    unsigned int curparam,
    CTokenGroupID &grpID,
                                        std::vector<CRecipient> &outputs, CAmount& totalValue, bool groupedOutputs)
{
    grpID = CTokenGroupID(params[curparam].get_str());
    if (!grpID.isUserGroup())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
    }
    outputs.reserve(params.size() / 2);
    curparam++;
    totalValue = 0;
    while (curparam + 1 < params.size())
    {
        CTxDestination dst = DecodeDestination(params[curparam].get_str(), Params());
        if (dst == CTxDestination(CNoDestination()))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: destination address");
        }
        CAmount amount = AmountFromSatoshiValue(params[curparam + 1]);
        if (amount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid parameter: amount");
        CScript script;
        if (groupedOutputs)
        {
        script = GetScriptForDestination(dst, grpID);
        }
        else
        {
        script = GetScriptForDestination(dst, BitcoinGroup);
        }
        CRecipient recipient = {script, amount, false};
        totalValue += amount;
        outputs.push_back(recipient);
        curparam += 2;
    }
    return curparam;
}

bool NearestGreaterCoin(const std::vector<COutput>& coins, CAmount amt, COutput& chosenCoin)
{
    bool ret = false;
    CAmount curBest=std::numeric_limits<CAmount>::max();

    for(const auto& coin: coins)
    {
        CAmount camt = coin.GetValue();
        if ((camt > amt)&&(camt < curBest))
        {
            curBest =  camt;
            chosenCoin = coin;
            ret = true;
        }
    }

    return ret;
}


CAmount CoinSelection(const std::vector<COutput>& coins, CAmount amt, std::vector<COutput>& chosenCoins)
{
    // simple algorithm grabs until amount exceeded
    CAmount cur=0;

    for(const auto& coin: coins)
    {
        chosenCoins.push_back(coin);
        cur += coin.GetValue();
        if (cur >= amt) break;
    }
    return cur;
}

void ConstructTx(CWalletTx& wtxNew, const std::vector<COutput>& chosenCoins, const std::vector<CRecipient> &outputs, CAmount totalAvailable, CAmount totalNeeded, CTokenGroupID grpID, CWallet* wallet)
{
    std::string strError;
    CMutableTransaction tx;
    CReserveKey groupChangeKeyReservation(wallet);
    CReserveKey feeChangeKeyReservation(wallet);

    {
        if (GetRandInt(10) == 0)
            tx.nLockTime = std::max(0, (int)tx.nLockTime - GetRandInt(100));
        assert(tx.nLockTime <= (unsigned int)chainActive.Height());
        assert(tx.nLockTime < LOCKTIME_THRESHOLD);
        unsigned int approxSize = 0;

        // Add group input and output
        for (const CRecipient &recipient : outputs)
        {
            CTxOut txout(recipient.nAmount, recipient.scriptPubKey);
            tx.vout.push_back(txout);
            approxSize += ::GetSerializeSize(txout, SER_DISK, CLIENT_VERSION);
        }

        unsigned int inpSize = 0;
        for (const auto &coin : chosenCoins)
        {
            CTxIn txin(coin.GetOutPoint(), CScript(), std::numeric_limits<unsigned int>::max() - 1);
            tx.vin.push_back(txin);
            inpSize = ::GetSerializeSize(txin, SER_DISK, CLIENT_VERSION) + TX_SIG_SCRIPT_LEN;
            approxSize += inpSize;
        }

        if (totalAvailable > totalNeeded) // need to make a group change output
        {
            CPubKey newKey;

            if (!groupChangeKeyReservation.GetReservedKey(newKey))
                throw JSONRPCError(
                    RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

            CTxOut txout(totalAvailable - totalNeeded, GetScriptForDestination(newKey.GetID(), grpID));
            tx.vout.push_back(txout);
            approxSize += ::GetSerializeSize(txout, SER_DISK, CLIENT_VERSION);
        }

        // Add another input for the bitcoin used for the fee
        // this ignores the additional change output
        approxSize += inpSize;

        // Now add bitcoin fee
        CAmount fee = wallet->GetRequiredFee(approxSize);

        // find a fee input
        std::vector<COutput> bchcoins;
        wallet->FilterCoins(bchcoins, [](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupPair tg = GetTokenGroupPair(out->scriptPubKey);
            return BitcoinGroup == tg.associatedGroup;
        });

        COutput feeCoin(nullptr,0,0,false);
        if (!NearestGreaterCoin(bchcoins, fee, feeCoin))
        {
            strError = strprintf("Not enough funds for fee of %d.", FormatMoney(fee));
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
        }

        CTxIn txin(feeCoin.GetOutPoint(), CScript(), std::numeric_limits<unsigned int>::max() - 1);
        tx.vin.push_back(txin);

        if (feeCoin.GetValue() > 2 * fee) // make change if input is too big
        {
            CPubKey newKey;

            if (!feeChangeKeyReservation.GetReservedKey(newKey))
                throw JSONRPCError(
                    RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

            CTxOut txout(feeCoin.GetValue() - fee, GetScriptForDestination(newKey.GetID()));
            tx.vout.push_back(txout);
        }

        if (!wallet->SignTransaction(tx))
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing transaction failed");
        }
    }

    wtxNew.BindWallet(wallet);
    wtxNew.fFromMe = true;
    *static_cast<CTransaction *>(&wtxNew) = CTransaction(tx);
    CReserveKey dummy(wallet);  // I'll manage my own keys because I have multiple.  Passing a valid key down breaks layering.
    if (!wallet->CommitTransaction(wtxNew, dummy))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the "
                                             "coins in your wallet were already spent, such as if you used a copy of "
                                             "wallet.dat and coins were spent in the copy but not marked as spent "
                                             "here.");

    feeChangeKeyReservation.KeepKey();
    groupChangeKeyReservation.KeepKey();
}


void GroupMelt(CWalletTx& wtxNew, const CTokenGroupID &grpID,
    const std::vector<CRecipient> &outputs,
    CAmount totalNeeded,
    CWallet *wallet)
{
    CAmount totalAvailable = 0;
    LOCK2(cs_main, wallet->cs_wallet);

    // Find meltable coins
    std::vector<COutput> coins;
    wallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupPair tg = GetTokenGroupPair(out->scriptPubKey);
        // must be a grouped output sitting in group address
        return ((grpID == tg.associatedGroup) && (grpID == tg.mintMeltGroup));
    });

    // Get a near but greater quantity
    std::vector<COutput> chosenCoins;
    totalAvailable = CoinSelection(coins, totalNeeded, chosenCoins);

    if (totalAvailable < totalNeeded)
    {
        std::string strError;
        strError = strprintf("Not enough tokens in the controlling address.  Need %d more.", totalNeeded - totalAvailable);
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
    }
        

    ConstructTx(wtxNew, chosenCoins, outputs, totalAvailable, totalNeeded, grpID, wallet);
}

void GroupSend(CWalletTx& wtxNew, const CTokenGroupID &grpID, const std::vector<CRecipient> &outputs, CAmount totalNeeded, CWallet* wallet)
{
    LOCK2(cs_main, wallet->cs_wallet);
    std::string strError;
    CTxDestination mintableAddress = grpID.ControllingAddress();
    // Find mintable coins
    std::vector<COutput> coins;
    wallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupPair tg = GetTokenGroupPair(out->scriptPubKey);
        return grpID == tg.associatedGroup; // must be sitting in group address
    });
    CAmount totalAvailable = 0;
    for (auto coin : coins)
    {
        totalAvailable += coin.tx->vout[coin.i].nValue;
    }
    if (totalAvailable < totalNeeded)
    {
        strError = strprintf("Not enough tokens.  Need %d more.", totalNeeded - totalAvailable);
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
    }

    // Get a near but greater quantity
    std::vector<COutput> chosenCoins;
    totalAvailable = CoinSelection(coins, totalNeeded, chosenCoins);

    ConstructTx(wtxNew, chosenCoins, outputs, totalAvailable, totalNeeded, grpID, wallet);
}


extern UniValue token(const UniValue &params, bool fHelp)
{
    CWallet* wallet = pwalletMain;
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1)
        throw std::runtime_error(
            "token [new, mint, melt, send] \n"
            "\nToken functions.\n"
            "new creates a new token type.\n"
            "mint creates new tokens. args: groupId address quantity\n"
            "melt removes tokens from circulation. args: groupId address quantity\n"
            "balance reports quantity of this token. args: groupId [address]\n"
            "send sends tokens to a new address. args: groupId address quantity\n"
            "\nArguments:\n"
            "1. \"groupId\"     (string, required) the group identifier\n"
            "2. \"address\"     (string, required) the destination address\n"
            "3. \"quantity\"    (numeric, required) the quantity desired\n"
            "\nResult:\n"
            "\n"
            "\nExamples:\n"
            "\nCreate a transaction with no inputs\n" +
            HelpExampleCli("createrawtransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") +
            "\nAdd sufficient unsigned inputs to meet the output value\n" +
            HelpExampleCli("fundrawtransaction", "\"rawtransactionhex\"") + "\nSign the transaction\n" +
            HelpExampleCli("signrawtransaction", "\"fundedtransactionhex\"") + "\nSend the transaction\n" +
            HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\""));

    std::string operation;
    std::string p0 = params[0].get_str();
    std::transform(p0.begin(), p0.end(), std::back_inserter(operation), ::tolower);
    EnsureWalletIsUnlocked();

    if (operation == "new")
    {
        std::string account = "";
        CPubKey newKey;
        if (!wallet->GetKeyFromPool(newKey))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        CKeyID keyID = newKey.GetID();
        wallet->SetAddressBook(keyID, account, "receive");
        CTokenGroupID grpID(keyID);
        UniValue ret(UniValue::VOBJ);
        ret.push_back(Pair("groupIdentifier", grpID.Encode()));
        ret.push_back(Pair("controllingAddress",EncodeDestination(keyID)));
        return ret;
    }
    else if (operation == "mint")
    {
        CTokenGroupID grpID;
        CAmount totalNeeded=0;
        unsigned int curparam = 1;
        std::vector<CRecipient> outputs;
        curparam = ParseGroupAddrValue(params, curparam, grpID, outputs, totalNeeded, true);

        if (!wallet->HaveTxDestination(grpID.ControllingAddress()))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter 1: Group is not owned by this wallet");
        }

        if (outputs.empty())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "No destination address or payment amount");
        }
        if (curparam != params.size())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Improper number of parameters, did you forget the payment amount?");
        }

        CWalletTx wtx;
        CReserveKey reservekey(wallet);
        CCoinControl coinControl;
        coinControl.fAllowOtherInputs = true;  // Allow a normal bitcoin input for change
        CAmount nFeeRequired;
        int nChangePosRet = -1;
        std::string strError;

        CTxDestination mintableAddress = grpID.ControllingAddress();
        // Find mintable coins
        std::vector<COutput> coins;
        int nOptions = wallet->FilterCoins(coins, [grpID] (const CWalletTx* tx, const CTxOut* out)
                                 {
                                     CTokenGroupPair tg = GetTokenGroupPair(out->scriptPubKey);
                                     if (tg.associatedGroup != BitcoinGroup) return false;  // need bitcoin only
                                     return grpID == tg.mintMeltGroup; // must be sitting in group address
                                 });
        if (nOptions == 0)
        {
            strError =  strprintf("To mint coins, first send %s to the group's controlling address.", CURRENCY_UNIT);
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
        }
        CAmount totalAvailable=0;
        for (auto coin: coins)
        {
            totalAvailable += coin.tx->vout[coin.i].nValue;
        }
        if (totalAvailable < totalNeeded)
        {
            strError =  strprintf("Minting requires %d more satoshis in the group's controlling address.", totalNeeded-totalAvailable);
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
        }
        std::vector<COutput> chosenCoins;
        CoinSelection(coins, totalNeeded, chosenCoins);
        for (const auto& c: chosenCoins)
        {
            coinControl.Select(COutPoint(c.tx->GetHash(),c.i));
        }
        
        if (!wallet->CreateTransaction(outputs, wtx, reservekey, nFeeRequired, nChangePosRet, strError, &coinControl))
        {
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its "
                                     "amount, complexity, or use of recently received funds!",
                    FormatMoney(nFeeRequired));
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
        if (!wallet->CommitTransaction(wtx, reservekey))
            throw JSONRPCError(RPC_WALLET_ERROR,
                "Error: The transaction was rejected! This might happen if some of the "
                "coins in your wallet were already spent, such as if you used a copy of "
                "wallet.dat and coins were spent in the copy but not marked as spent "
                "here.");
        return wtx.GetHash().GetHex();
    }
    else if (operation == "balance")
    {
        if (params.size() > 3)
        {
            throw std::runtime_error("Invalid number of argument to token balance");
        }
        CTokenGroupID grpID(params[1].get_str());
        if (!grpID.isUserGroup())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter 1: No group specified");
        }
        CTxDestination dst;
        if (params.size() > 2)
        {
        dst = DecodeDestination(params[2].get_str(), Params());
        }
        return UniValue(GetGroupBalance(grpID, dst, wallet));
    }
    else if (operation == "send")
    {
        CTokenGroupID grpID;
        CAmount totalNeeded=0;
        unsigned int curparam = 1;
        std::vector<CRecipient> outputs;
        curparam = ParseGroupAddrValue(params, curparam, grpID, outputs, totalNeeded, true);

        if (outputs.empty())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "No destination address or payment amount");
        }
        if (curparam != params.size())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Improper number of parameters, did you forget the payment amount?");
        }
        CWalletTx wtx;
        GroupSend(wtx, grpID, outputs, totalNeeded, wallet);
        return wtx.GetHash().GetHex();
    }
    else if (operation == "melt")
    {
        CTokenGroupID grpID;
        CAmount totalNeeded=0;
        unsigned int curparam = 1;
        std::vector<CRecipient> outputs;
        curparam = ParseGroupAddrValue(params, curparam, grpID, outputs, totalNeeded, false);

        if (!wallet->HaveTxDestination(grpID.ControllingAddress()))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Group is not owned by this wallet");
        }
        
        if (outputs.empty())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "No destination address or payment amount");
        }
        if (curparam != params.size())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Improper number of parameters, did you forget the payment amount?");
        }

        CWalletTx wtx;
        GroupMelt(wtx, grpID, outputs, totalNeeded, wallet);
        return wtx.GetHash().GetHex();
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Unknown group operation");
    }
    return NullUniValue;
}
