// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "core_io.h"
#include "dstencode.h"
#include "init.h"
#include "main.h"
#include "merkleblock.h"
#include "rpc/server.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"
#include "util.h"
#include "utiltime.h"
#include "validation/validation.h"

#include "wallet.h"

#include <fstream>
#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <univalue.h>


using namespace std;

void EnsureWalletIsUnlocked();
bool EnsureWalletIsAvailable(bool avoidException);

std::string static EncodeDumpTime(int64_t nTime) { return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime); }
int64_t static DecodeDumpTime(const std::string &str)
{
    static const boost::posix_time::ptime epoch = boost::posix_time::from_time_t(0);
    static const std::locale loc(std::locale::classic(), new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%SZ"));
    std::istringstream iss(str);
    iss.imbue(loc);
    boost::posix_time::ptime ptime(boost::date_time::not_a_date_time);
    iss >> ptime;
    if (ptime.is_not_a_date_time())
        return 0;
    return (ptime - epoch).total_seconds();
}

std::string static EncodeDumpString(const std::string &str)
{
    std::stringstream ret;
    for (unsigned char c : str)
    {
        if (c <= 32 || c >= 128 || c == '%')
        {
            ret << '%' << HexStr(&c, &c + 1);
        }
        else
        {
            ret << c;
        }
    }
    return ret.str();
}

std::string DecodeDumpString(const std::string &str)
{
    std::stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++)
    {
        unsigned char c = str[pos];
        if (c == '%' && pos + 2 < str.length())
        {
            c = (((str[pos + 1] >> 6) * 9 + ((str[pos + 1] - '0') & 15)) << 4) |
                ((str[pos + 2] >> 6) * 9 + ((str[pos + 2] - '0') & 15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

UniValue importprivkey(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "importprivkey \"bitcoinprivkey\" ( \"label\" rescan )\n"
            "\nAdds a private key (as returned by dumpprivkey) to your wallet.\n"
            "\nArguments:\n"
            "1. \"bitcoinprivkey\"   (string, required) The private key (see dumpprivkey)\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Scan the blockchain for transactions\n"
            "\nNote: This call can take hours to complete if rescan is true.  To import multiple private keys\n"
            "\nuse the importprivatekeys RPC call.\n"
            "\nExamples:\n"
            "\nDump a private key\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") + "\nImport the private key with rescan\n" +
            HelpExampleCli("importprivkey", "\"mykey\"") + "\nImport using rescan and label\n" +
            HelpExampleCli("importprivkey", "\"mykey\" \"mylabel\"") + "\nImport without rescan (must use a label)\n" +
            HelpExampleCli("importprivkey", "\"mykey\" \"mylabel\" false") + "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importprivkey", "\"mykey\", \"mylabel\", false"));


    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    string strSecret = params[0].get_str();
    string strLabel = "";
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescanLocal = true;
    if (params.size() > 2)
        fRescanLocal = params[2].get_bool();

    if (fRescanLocal && fPruneMode)
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled in pruned mode");

    CBitcoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strSecret);

    if (!fGood)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

    CKey key = vchSecret.GetKey();
    if (!key.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    CPubKey pubkey = key.GetPubKey();
    assert(key.VerifyPubKey(pubkey));
    CKeyID vchAddress = pubkey.GetID();
    {
        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, strLabel, "receive");

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            return NullUniValue;

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

        if (fRescanLocal)
        {
            pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
        }
    }

    return NullUniValue;
}

UniValue importprivatekeys(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1)
        throw runtime_error(
            "importprivatekeys [rescan | no-rescan] \"bitcoinprivatekey\"...\n"
            "\nAdds private keys (as returned by dumpprivkey) to your wallet.\n"
            "\nArguments:\n"
            "1. \"rescan | no-rescan\" (string, optional default rescan) If \"no-rescan\", skip wallet rescan\n"
            "2. \"bitcoinprivatekey\"   (string, at least 1 required) The private keys (see dumpprivkey)\n"
            "\nNote: This command will return before the rescan (may take hours) is complete.\n"
            "\nExamples:\n"
            "\nDump a private key\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") + "\nImport the private key with rescan\n" +
            HelpExampleCli("importprivatekey", "\"mykey\"") + "\nImport using a label and without rescan\n" +
            HelpExampleCli("importprivatekeys", "no-rescan \"mykey\"") + "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importprivatekeys", "\"mykey\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    unsigned int paramNum = 0;
    bool fRescanLocal = true;

    if (params[0].get_str() == "no-rescan")
    {
        fRescanLocal = false;
        paramNum++;
    }
    else if (params[0].get_str() == "rescan")
    {
        fRescanLocal = true;
        paramNum++;
    }

    for (; paramNum < params.size(); paramNum++)
    {
        string strSecret = params[paramNum].get_str();
        string strLabel = "";

        CBitcoinSecret vchSecret;
        bool fGood = vchSecret.SetString(strSecret);

        if (!fGood)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

        CKey key = vchSecret.GetKey();
        if (!key.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

        CPubKey pubkey = key.GetPubKey();
        assert(key.VerifyPubKey(pubkey));
        CKeyID vchAddress = pubkey.GetID();

        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, strLabel, "receive");

        // Don't throw error in case a key is already there
        if (!pwalletMain->HaveKey(vchAddress))
        {
            pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

            if (!pwalletMain->AddKeyPubKey(key, pubkey))
                throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

            // whenever a key is imported, we need to scan the whole chain
            pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'
        }
    }

    if (fRescanLocal)
    {
        StartWalletRescanThread();
    }

    return NullUniValue;
}

void ImportAddress(const CTxDestination &dest, const std::string &strLabel);
void ImportScript(const CScript &script, const std::string &strLabel, bool isRedeemScript)
{
    if (!isRedeemScript && ::IsMine(*pwalletMain, script, chainActive.Tip()) == ISMINE_SPENDABLE)
        throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the "
                                             "private key for this address or "
                                             "script");

    pwalletMain->MarkDirty();

    if (!pwalletMain->HaveWatchOnly(script) && !pwalletMain->AddWatchOnly(script))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

    if (isRedeemScript)
    {
        if (!pwalletMain->HaveCScript(script) && !pwalletMain->AddCScript(script))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding p2sh redeemScript to wallet");
        ImportAddress(CScriptID(script), strLabel);
    }
    else
    {
        CTxDestination destination;
        if (ExtractDestination(script, destination))
        {
            pwalletMain->SetAddressBook(destination, strLabel, "receive");
        }
    }
}

void ImportAddress(const CTxDestination &dest, const std::string &strLabel)
{
    CScript script = GetScriptForDestination(dest);
    ImportScript(script, strLabel, false);
    // add to address book or update label
    if (IsValidDestination(dest))
        pwalletMain->SetAddressBook(dest, strLabel, "receive");
}


UniValue importaddress(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "importaddress \"address\" ( \"label\" rescan p2sh )\n"
            "\nAdds a script (in hex) or address that can be watched as if it were in your wallet but cannot be used "
            "to spend.\n"
            "\nArguments:\n"
            "1. \"script\"           (string, required) The hex-encoded script (or address)\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "4. p2sh                 (boolean, optional, default=false) Add the P2SH version of the script as well\n"
            "\nNote: This call can take hours to complete if rescan is true.\n"
            "If you have the full public key, you should call importpublickey instead of this.\n"
            "\nExamples:\n"
            "\nImport a script with rescan\n" +
            HelpExampleCli("importaddress", "\"myscript\"") + "\nImport using a label without rescan\n" +
            HelpExampleCli("importaddress", "\"myscript\" \"testing\" false") + "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importaddress", "\"myscript\", \"testing\", false"));


    string strLabel = "";
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescanLocal = true;
    if (params.size() > 2)
        fRescanLocal = params[2].get_bool();

    if (fRescanLocal && fPruneMode)
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled in pruned mode");

    // Whether to import a p2sh version, too
    bool fP2SH = false;
    if (params.size() > 3)
        fP2SH = params[3].get_bool();

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CTxDestination dest = DecodeDestination(params[0].get_str());
    if (IsValidDestination(dest))
    {
        if (fP2SH)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Cannot use the p2sh flag with an address - use "
                                                           "a script instead");
        }
        ImportAddress(dest, strLabel);
    }
    else if (IsHex(params[0].get_str()))
    {
        std::vector<uint8_t> data(ParseHex(params[0].get_str()));
        ImportScript(CScript(data.begin(), data.end()), strLabel, fP2SH);
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address or script");
    }

    if (fRescanLocal)
    {
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
        pwalletMain->ReacceptWalletTransactions();
    }

    return NullUniValue;
}

UniValue importaddresses(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1)
        throw runtime_error(
            "importaddresses [rescan | no-rescan] \"address\"...\n"
            "\nAdds a script (in hex) or address that can be watched as if it were in your wallet but cannot be used "
            "to spend.\n"
            "\nArguments:\n"
            "1. \"rescan | no-rescan\" (string, optional, default=rescan) If \"no-rescan\", skip wallet rescan\n"
            "2. \"address\"           (string, 0 or more) The address(es) or hex-encoded P2SH script(s)\n"
            "\nNote, this command will return before the rescan (may take hours) is complete.\n"
            "If you have the full public key, you should call importpublickey instead of this.\n"
            "This command assumes all scripts are P2SH, so you should call importaddress to\n"
            "import a nonstandard non-P2SH script.\n"
            "\nExamples:\n"
            "\nImport 2 scripts with rescan\n" +
            HelpExampleCli("importaddresses", "\"myscript1\" \"myscript2\"") + "\nImport 2 scripts without rescan\n" +
            HelpExampleCli("importaddresses", "no-rescan \"myscript1\" \"myscript2\"") + "\nRescan without import\n" +
            HelpExampleCli("importaddresses", "rescan") + "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importaddresses", "\"myscript1\", \"myscript2\""));

    // Whether to perform rescan after import
    bool fRescanLocal = true;

    unsigned int paramNum = 0;

    if (params[0].get_str() == "no-rescan")
    {
        fRescanLocal = false;
        paramNum++;
    }
    else if (params[0].get_str() == "rescan")
    {
        fRescanLocal = true;
        paramNum++;
    }

    if (fRescanLocal && fPruneMode)
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled in pruned mode");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    for (; paramNum < params.size(); paramNum++)
    {
        std::string param = params[paramNum].get_str();
        CTxDestination dest = DecodeDestination(param);
        if (IsValidDestination(dest))
        {
            ImportAddress(dest, "");
        }
        else if (IsHex(param))
        {
            bool fP2SH = true;
            std::vector<unsigned char> data(ParseHex(param));
            ImportScript(CScript(data.begin(), data.end()), "", fP2SH);
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address or script");
        }
    }

    if (fRescanLocal)
    {
        StartWalletRescanThread();
    }

    return NullUniValue;
}

UniValue importprunedfunds(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "importprunedfunds\n"
            "\nImports funds without rescan. Corresponding address or script must previously be included in wallet. "
            "Aimed towards pruned wallets. The end-user is responsible to import additional transactions that "
            "subsequently spend the imported outputs or rescan after the point in the blockchain the transaction is "
            "included.\n"
            "\nArguments:\n"
            "1. \"rawtransaction\" (string, required) A raw transaction in hex funding an already-existing address in "
            "wallet\n"
            "2. \"txoutproof\"     (string, required) The hex output from gettxoutproof that contains the transaction\n"
            "3. \"label\"          (string, optional) An optional label\n");

    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    uint256 hashTx = tx.GetHash();
    CWalletTx wtx(pwalletMain, tx);

    CDataStream ssMB(ParseHexV(params[1], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    string strLabel = "";
    if (params.size() == 3)
        strLabel = params[2].get_str();

    // Search partial merkle tree in proof for our transaction and index in valid block
    vector<uint256> vMatch;
    vector<unsigned int> vIndex;
    unsigned int txnIndex = 0;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) == merkleBlock.header.hashMerkleRoot)
    {
        auto *tmp = LookupBlockIndex(merkleBlock.header.GetHash());
        LOCK(cs_main); // for chainActive
        if (!tmp || !chainActive.Contains(tmp))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

        vector<uint256>::const_iterator it;
        if ((it = std::find(vMatch.begin(), vMatch.end(), hashTx)) == vMatch.end())
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction given doesn't exist in proof");
        }

        txnIndex = vIndex[it - vMatch.begin()];
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Something wrong with merkleblock");
    }

    wtx.nIndex = txnIndex;
    wtx.hashBlock = merkleBlock.header.GetHash();

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (pwalletMain->IsMine(tx))
    {
        CWalletDB walletdb(pwalletMain->strWalletFile, "r+", false);
        pwalletMain->AddToWallet(wtx, false, &walletdb);
        return NullUniValue;
    }

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No addresses in wallet correspond to included transaction");
}

UniValue removeprunedfunds(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "removeprunedfunds \"txid\"\n"
            "\nDeletes the specified transaction from the wallet. Meant for use with pruned wallets and as a companion "
            "to importprunedfunds. This will effect wallet balances.\n"
            "\nArguments:\n"
            "1. \"txid\"           (string, required) The hex-encoded id of the transaction you are deleting\n"
            "\nExamples:\n" +
            HelpExampleCli(
                "removeprunedfunds", "\"a8d0c0184dde994a09ec054286f1ce581bebf46446a512166eae7628734ea0a5\"") +
            "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("removprunedfunds", "\"a8d0c0184dde994a09ec054286f1ce581bebf46446a512166eae7628734ea0a5\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hash;
    hash.SetHex(params[0].get_str());
    vector<uint256> vHash;
    vHash.push_back(hash);
    vector<uint256> vHashOut;

    if (pwalletMain->ZapSelectTx(vHash, vHashOut) != DB_LOAD_OK)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not properly delete the transaction.");
    }

    if (vHashOut.empty())
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction does not exist in wallet.");
    }

    return NullUniValue;
}

UniValue importpubkey(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "importpubkey \"pubkey\" ( \"label\" rescan )\n"
            "\nAdds a public key (in hex) that can be watched as if it were in your wallet but cannot be used to "
            "spend.\n"
            "\nArguments:\n"
            "1. \"pubkey\"           (string, required) The hex-encoded public key\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "\nNote: This call can take minutes to complete if rescan is true.\n"
            "\nExamples:\n"
            "\nImport a public key with rescan\n" +
            HelpExampleCli("importpubkey", "\"mypubkey\"") + "\nImport using a label without rescan\n" +
            HelpExampleCli("importpubkey", "\"mypubkey\" \"testing\" false") + "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importpubkey", "\"mypubkey\", \"testing\", false"));


    string strLabel = "";
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescanLocal = true;
    if (params.size() > 2)
        fRescanLocal = params[2].get_bool();

    if (fRescanLocal && fPruneMode)
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled in pruned mode");

    if (!IsHex(params[0].get_str()))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey must be a hex string");
    std::vector<unsigned char> data(ParseHex(params[0].get_str()));
    CPubKey pubKey(data.begin(), data.end());
    if (!pubKey.IsFullyValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey is not a valid public key");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    ImportAddress(pubKey.GetID(), strLabel);
    ImportScript(GetScriptForRawPubKey(pubKey), strLabel, false);

    if (fRescanLocal)
    {
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
        pwalletMain->ReacceptWalletTransactions();
    }

    return NullUniValue;
}


UniValue importwallet(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error("importwallet \"filename\"\n"
                            "\nImports keys from a wallet dump file (see dumpwallet).\n"
                            "\nArguments:\n"
                            "1. \"filename\"    (string, required) The wallet file\n"
                            "\nExamples:\n"
                            "\nDump the wallet\n" +
                            HelpExampleCli("dumpwallet", "\"test\"") + "\nImport the wallet\n" +
                            HelpExampleCli("importwallet", "\"test\"") + "\nImport using the json rpc call\n" +
                            HelpExampleRpc("importwallet", "\"test\""));

    if (fPruneMode)
        throw JSONRPCError(RPC_WALLET_ERROR, "Importing wallets is disabled in pruned mode");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    ifstream file;
    file.open(params[0].get_str().c_str(), std::ios::in | std::ios::ate);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    int64_t nTimeBegin = chainActive.Tip()->GetBlockTime();

    bool fGood = true;

    int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);

    pwalletMain->ShowProgress(_("Importing..."), 0); // show progress dialog in GUI
    while (file.good())
    {
        pwalletMain->ShowProgress(
            "", std::max(1, std::min(99, (int)(((double)file.tellg() / (double)nFilesize) * 100))));
        std::string line;
        std::getline(file, line);
        if (line.empty() || line[0] == '#')
            continue;

        std::vector<std::string> vstr;
        boost::split(vstr, line, boost::is_any_of(" "));
        if (vstr.size() < 2)
            continue;
        CBitcoinSecret vchSecret;
        if (!vchSecret.SetString(vstr[0]))
            continue;
        CKey key = vchSecret.GetKey();
        CPubKey pubkey = key.GetPubKey();
        assert(key.VerifyPubKey(pubkey));
        CKeyID keyid = pubkey.GetID();
        if (pwalletMain->HaveKey(keyid))
        {
            LOGA("Skipping import of %s (key already present)\n", EncodeDestination(keyid));
            continue;
        }
        int64_t nTime = DecodeDumpTime(vstr[1]);
        std::string strLabel;
        bool fLabel = true;
        for (unsigned int nStr = 2; nStr < vstr.size(); nStr++)
        {
            if (boost::algorithm::starts_with(vstr[nStr], "#"))
                break;
            if (vstr[nStr] == "change=1")
                fLabel = false;
            if (vstr[nStr] == "reserve=1")
                fLabel = false;
            if (boost::algorithm::starts_with(vstr[nStr], "label="))
            {
                strLabel = DecodeDumpString(vstr[nStr].substr(6));
                fLabel = true;
            }
        }
        LOGA("Importing %s...\n", EncodeDestination(keyid));
        if (!pwalletMain->AddKeyPubKey(key, pubkey))
        {
            fGood = false;
            continue;
        }
        pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
        if (fLabel)
            pwalletMain->SetAddressBook(keyid, strLabel, "receive");
        nTimeBegin = std::min(nTimeBegin, nTime);
    }
    file.close();
    pwalletMain->ShowProgress("", 100); // hide progress dialog in GUI

    CBlockIndex *pindex = chainActive.Tip();
    while (pindex && pindex->pprev && pindex->GetBlockTime() > nTimeBegin - 7200)
        pindex = pindex->pprev;

    if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey)
        pwalletMain->nTimeFirstKey = nTimeBegin;

    LOGA("Rescanning last %i blocks\n", chainActive.Height() - pindex->nHeight + 1);
    pwalletMain->ScanForWalletTransactions(pindex);
    pwalletMain->MarkDirty();

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return NullUniValue;
}

UniValue dumpprivkey(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error("dumpprivkey \"bitcoinaddress\"\n"
                            "\nReveals the private key corresponding to 'bitcoinaddress'.\n"
                            "Then the importprivkey can be used with this output\n"
                            "\nArguments:\n"
                            "1. \"bitcoinaddress\"   (string, required) The bitcoin address for the private key\n"
                            "\nResult:\n"
                            "\"key\"                (string) The private key\n"
                            "\nExamples:\n" +
                            HelpExampleCli("dumpprivkey", "\"myaddress\"") +
                            HelpExampleCli("importprivkey", "\"mykey\"") +
                            HelpExampleRpc("dumpprivkey", "\"myaddress\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strAddress = params[0].get_str();
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");
    }
    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    if (!keyID)
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    }
    CKey vchSecret;
    if (!pwalletMain->GetKey(*keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    return CBitcoinSecret(vchSecret).ToString();
}


UniValue dumpwallet(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "dumpwallet \"filename\"\n"
            "\nDumps all wallet keys in a human-readable format to a server-side file. This does not allow overwriting "
            "existing files.\n"
            "\nArguments:\n"
            "1. \"filename\"    (string, required) The filename with path (either absolute or relative to bitcoind)\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"filename\" : {        (string) The filename with full absolute path\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("dumpwallet", "\"test\"") + HelpExampleRpc("dumpwallet", "\"test\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    boost::filesystem::path filepath = params[0].get_str();
    filepath = boost::filesystem::absolute(filepath);

    /* Prevent arbitrary files from being overwritten. There have been reports
     * that users have overwritten wallet files this way:
     * https://github.com/bitcoin/bitcoin/issues/9934
     * It may also avoid other security issues.
     */
    if (boost::filesystem::exists(filepath))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            filepath.string() + " already exists. If you are sure this is what you want, move it out of the way first");
    }

    std::ofstream file;
    file.open(filepath.string().c_str());
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    std::map<CKeyID, int64_t> mapKeyBirth;
    std::set<CKeyID> setKeyPool;
    pwalletMain->GetKeyBirthTimes(mapKeyBirth);
    pwalletMain->GetAllReserveKeys(setKeyPool);

    // sort time/key pairs
    std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
    for (std::map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin(); it != mapKeyBirth.end(); it++)
    {
        vKeyBirth.push_back(std::make_pair(it->second, it->first));
    }
    mapKeyBirth.clear();
    std::sort(vKeyBirth.begin(), vKeyBirth.end());

    // produce output
    file << strprintf("# Wallet dump created by Bitcoin %s (%s)\n", CLIENT_BUILD, CLIENT_DATE);
    file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()));
    file << strprintf("# * Best block at time of backup was %i (%s),\n", chainActive.Height(),
        chainActive.Tip()->GetBlockHash().ToString());
    file << strprintf("#   mined on %s\n", EncodeDumpTime(chainActive.Tip()->GetBlockTime()));
    file << "\n";

    // add the base58check encoded extended master if the wallet uses HD
    CKeyID masterKeyID = pwalletMain->GetHDChain().masterKeyID;
    if (!masterKeyID.IsNull())
    {
        CKey key;
        if (pwalletMain->GetKey(masterKeyID, key))
        {
            CExtKey masterKey;
            masterKey.SetMaster(key.begin(), key.size());

            CBitcoinExtKey b58extkey;
            b58extkey.SetKey(masterKey);

            file << "# extended private masterkey: " << b58extkey.ToString() << "\n\n";
        }
    }
    for (std::vector<std::pair<int64_t, CKeyID> >::const_iterator it = vKeyBirth.begin(); it != vKeyBirth.end(); it++)
    {
        const CKeyID &keyid = it->second;
        std::string strTime = EncodeDumpTime(it->first);
        std::string strAddr = EncodeDestination(keyid);
        CKey key;
        if (pwalletMain->GetKey(keyid, key))
        {
            file << strprintf("%s %s ", CBitcoinSecret(key).ToString(), strTime);
            if (pwalletMain->mapAddressBook.count(keyid))
            {
                file << strprintf("label=%s", EncodeDumpString(pwalletMain->mapAddressBook[keyid].name));
            }
            else if (keyid == masterKeyID)
            {
                file << "hdmaster=1";
            }
            else if (setKeyPool.count(keyid))
            {
                file << "reserve=1";
            }
            else if (pwalletMain->mapKeyMetadata[keyid].hdKeypath == "m")
            {
                file << "inactivehdmaster=1";
            }
            else
            {
                file << "change=1";
            }
            file << strprintf(
                " # addr=%s%s\n", strAddr, (pwalletMain->mapKeyMetadata[keyid].hdKeypath.size() > 0 ?
                                                   " hdkeypath=" + pwalletMain->mapKeyMetadata[keyid].hdKeypath :
                                                   ""));
        }
    }
    file << "\n";
    file << "# End of dump\n";
    file.close();
    return NullUniValue;
}
