// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core_io.h"

#include "dstencode.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include <univalue.h>

using namespace std;

string FormatScript(const CScript &script)
{
    string ret;
    CScript::const_iterator it = script.begin();
    opcodetype op;
    while (it != script.end())
    {
        CScript::const_iterator it2 = it;
        vector<unsigned char> vch;
        if (script.GetOp2(it, op, &vch))
        {
            if (op == OP_0)
            {
                ret += "0 ";
                continue;
            }
            else if ((op >= OP_1 && op <= OP_16) || op == OP_1NEGATE)
            {
                ret += strprintf("%i ", op - OP_1NEGATE - 1);
                continue;
            }

            if (op >= OP_NOP && op < FIRST_UNDEFINED_OP_VALUE)
            {
                string str(GetOpName(op));
                if (str.substr(0, 3) == string("OP_"))
                {
                    ret += str.substr(3, string::npos) + " ";
                    continue;
                }
            }
            if (vch.size() > 0)
            {
                ret += strprintf("0x%x 0x%x ", HexStr(it2, it - vch.size()), HexStr(it - vch.size(), it));
            }
            else
            {
                ret += strprintf("0x%x ", HexStr(it2, it));
            }
            continue;
        }
        ret += strprintf("0x%x ", HexStr(it2, script.end()));
        break;
    }
    return ret.substr(0, ret.size() - 1);
}

const map<unsigned char, string> mapSigHashTypes = {
    {(unsigned char)SIGHASH_ALL, std::string("ALL")},
    {(unsigned char)(SIGHASH_ALL | SIGHASH_ANYONECANPAY), std::string("ALL|ANYONECANPAY")},
    {(unsigned char)(SIGHASH_ALL | SIGHASH_FORKID), std::string("ALL|FORKID")},
    {(unsigned char)(SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_ANYONECANPAY), std::string("ALL|FORKID|ANYONECANPAY")},
    {(unsigned char)SIGHASH_NONE, std::string("NONE")},
    {(unsigned char)(SIGHASH_NONE | SIGHASH_ANYONECANPAY), std::string("NONE|ANYONECANPAY")},
    {(unsigned char)(SIGHASH_NONE | SIGHASH_FORKID), std::string("NONE|FORKID")},
    {(unsigned char)(SIGHASH_NONE | SIGHASH_FORKID | SIGHASH_ANYONECANPAY), std::string("NONE|FORKID|ANYONECANPAY")},
    {(unsigned char)SIGHASH_SINGLE, std::string("SINGLE")},
    {(unsigned char)(SIGHASH_SINGLE | SIGHASH_ANYONECANPAY), std::string("SINGLE|ANYONECANPAY")},
    {(unsigned char)(SIGHASH_SINGLE | SIGHASH_FORKID), std::string("SINGLE|FORKID")},
    {(unsigned char)(SIGHASH_SINGLE | SIGHASH_FORKID | SIGHASH_ANYONECANPAY),
        std::string("SINGLE|FORKID|ANYONECANPAY")},
};

/**
 * Create the assembly string representation of a CScript object.
 * @param[in] script    CScript object to convert into the asm string representation.
 * @param[in] fAttemptSighashDecode    Whether to attempt to decode sighash types on data within the script that matches
 * the format
 *                                     of a signature. Only pass true for scripts you believe could contain signatures.
 * For example,
 *                                     pass false, or omit the this argument (defaults to false), for scriptPubKeys.
 */
string ScriptToAsmStr(const CScript &script, const bool fAttemptSighashDecode, bool f64BitNums)
{
    string str;
    opcodetype opcode;
    vector<uint8_t> vch;
    CScript::const_iterator pc = script.begin();
    while (pc < script.end())
    {
        if (!str.empty())
        {
            str += " ";
        }
        if (!script.GetOp(pc, opcode, vch))
        {
            str += "[error]";
            return str;
        }
        size_t const maxScriptNumSize =
            f64BitNums ? CScriptNum::MAXIMUM_ELEMENT_SIZE_64_BIT : CScriptNum::MAXIMUM_ELEMENT_SIZE_32_BIT;
        if (0 <= opcode && opcode <= OP_PUSHDATA4)
        {
            if (vch.size() <= maxScriptNumSize)
            {
                str += strprintf("%d", CScriptNum(vch, false, maxScriptNumSize).getint64());
            }
            else
            {
                // the IsUnspendable check makes sure not to try to decode OP_RETURN data that may match the format of a
                // signature
                if (fAttemptSighashDecode && !script.IsUnspendable())
                {
                    string strSigHashDecode;
                    // goal: only attempt to decode a defined sighash type from data that looks like a signature within
                    // a scriptSig.
                    // this won't decode correctly formatted public keys in Pubkey or Multisig scripts due to
                    // the restrictions on the pubkey formats (see IsCompressedOrUncompressedPubKey) being incongruous
                    // with the
                    // checks in CheckSignatureEncoding.
                    if (CheckSignatureEncoding(vch, SCRIPT_VERIFY_STRICTENC, nullptr))
                    {
                        const unsigned char chSigHashType = vch.back();
                        if (mapSigHashTypes.count(chSigHashType))
                        {
                            strSigHashDecode = "[" + mapSigHashTypes.find(chSigHashType)->second + "]";
                            vch.pop_back(); // remove the sighash type byte. it will be replaced by the decode.
                        }
                    }
                    str += HexStr(vch) + strSigHashDecode;
                }
                else
                {
                    str += HexStr(vch);
                }
            }
        }
        else
        {
            str += GetOpName(opcode);
        }
    }
    return str;
}

string EncodeHexTx(const CTransaction &tx)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}

void ScriptPubKeyToUniv(const CScript &scriptPubKey, UniValue &out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired))
    {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    UniValue a(UniValue::VARR);
    for (const CTxDestination &addr : addresses)
    {
        a.push_back(EncodeDestination(addr));
    }
    out.pushKV("addresses", a);
}

void TxToUniv(const CTransaction &tx, const uint256 &hashBlock, UniValue &entry)
{
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("locktime", (int64_t)tx.nLockTime);

    UniValue vin(UniValue::VARR);
    for (const CTxIn &txin : tx.vin)
    {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else
        {
            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("vout", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", o);
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut &txout = tx.vout[i];

        UniValue out(UniValue::VOBJ);

        UniValue outValue(UniValue::VNUM, FormatMoney(txout.nValue));
        out.pushKV("value", outValue);
        out.pushKV("n", (int64_t)i);

        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToUniv(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);
    }
    entry.pushKV("vout", vout);

    if (!hashBlock.IsNull())
        entry.pushKV("blockhash", hashBlock.GetHex());

    // the hex-encoded transaction. used the name "hex" to be consistent with the verbose output of "getrawtransaction".
    entry.pushKV("hex", EncodeHexTx(tx));
}
