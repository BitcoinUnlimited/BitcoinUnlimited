// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/wallet.h"

#include "chain.h"
#include "checkpoints.h"
#include "coincontrol.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "core_io.h" // Freeze for debug only
#include "dstencode.h"
#include "fs.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sign.h"
#include "timedata.h"
#include "txmempool.h"
#include "uahf_fork.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"

#include <assert.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>

using namespace std;

CWallet *pwalletMain = nullptr;
/** Transaction fee set by the user */
CFeeRate payTxFee(DEFAULT_TRANSACTION_FEE);
unsigned int nTxConfirmTarget = DEFAULT_TX_CONFIRM_TARGET;
bool bSpendZeroConfChange = DEFAULT_SPEND_ZEROCONF_CHANGE;
bool fSendFreeTransactions = DEFAULT_SEND_FREE_TRANSACTIONS;

const char *DEFAULT_WALLET_DAT = "wallet.dat";
const uint32_t BIP32_HARDENED_KEY_LIMIT = 0x80000000;

/**
 * Fees smaller than this (in satoshi) are considered zero fee (for transaction creation)
 * Override with -mintxfee
 */
CFeeRate CWallet::minTxFee = CFeeRate(DEFAULT_TRANSACTION_MINFEE);
/**
 * If fee estimation does not have enough data to provide estimates, use this fee instead.
 * Has no effect if not using fee estimation
 * Override with -fallbackfee
 */
CFeeRate CWallet::fallbackFee = CFeeRate(DEFAULT_FALLBACK_FEE);

const uint256 CMerkleTx::ABANDON_HASH(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));

/** @defgroup mapWallet
 *
 * @{
 */

struct CompareValueOnly
{
    bool operator()(const pair<CAmount, pair<const CWalletTx *, unsigned int> > &t1,
        const pair<CAmount, pair<const CWalletTx *, unsigned int> > &t2) const
    {
        return t1.first < t2.first;
    }
};

std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->vout[i].nValue));
}

const CWalletTx *CWallet::GetWalletTx(const uint256 &hash) const
{
    LOCK(cs_wallet);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return nullptr;
    return &(it->second);
}

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    // default to compressed public keys if we want 0.6.0 wallets
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY);

    CKey secret;

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // use HD key derivation if HD was enabled during wallet creation
    if (IsHDEnabled())
    {
        DeriveNewChildKey(metadata, secret);
    }
    else
    {
        secret.MakeNewKey(fCompressed);
    }

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    mapKeyMetadata[pubkey.GetID()] = metadata;
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKeyPubKey(secret, pubkey))
        throw std::runtime_error("CWallet::GenerateNewKey(): AddKey failed");
    return pubkey;
}

void CWallet::DeriveNewChildKey(CKeyMetadata &metadata, CKey &secret)
{
    // for now we use a fixed keypath scheme of m/0'/0'/k
    CKey key; // master key seed (256bit)
    CExtKey masterKey; // hd master key
    CExtKey accountKey; // key at m/0'
    CExtKey externalChainChildKey; // key at m/0'/0'
    CExtKey childKey; // key at m/0'/0'/<n>'

    // try to get the master key
    if (!GetKey(hdChain.masterKeyID, key))
        throw std::runtime_error(std::string(__func__) + ": Master key not found");

    masterKey.SetMaster(key.begin(), key.size());

    // derive m/0'
    // use hardened derivation (child keys >= 0x80000000 are hardened after bip32)
    masterKey.Derive(accountKey, BIP32_HARDENED_KEY_LIMIT);

    // derive m/0'/0'
    accountKey.Derive(externalChainChildKey, BIP32_HARDENED_KEY_LIMIT);

    // derive child key at next index, skip keys already known to the wallet
    do
    {
        // always derive hardened keys
        // childIndex | BIP32_HARDENED_KEY_LIMIT = derive childIndex in hardened child-index-range
        // example: 1 | BIP32_HARDENED_KEY_LIMIT == 0x80000001 == 2147483649
        externalChainChildKey.Derive(childKey, hdChain.nExternalChainCounter | BIP32_HARDENED_KEY_LIMIT);
        metadata.hdKeypath = "m/0'/0'/" + std::to_string(hdChain.nExternalChainCounter) + "'";
        metadata.hdMasterKeyID = hdChain.masterKeyID;
        // increment childkey index
        hdChain.nExternalChainCounter++;
    } while (HaveKey(childKey.key.GetPubKey().GetID()));
    secret = childKey.key;

    // update the chain model in the database
    if (!CWalletDB(strWalletFile).WriteHDChain(hdChain))
        throw std::runtime_error(std::string(__func__) + ": Writing HD chain model failed");
}

bool CWallet::AddKeyPubKey(const CKey &secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey))
        return false;

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);
    script = GetScriptForRawPubKey(pubkey);
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);

    if (!fFileBacked)
        return true;
    if (!IsCrypted())
    {
        return CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey, const vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
        else
            return CWalletDB(strWalletFile)
                .WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript &redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript &redeemScript)
{
    /**
     * A sanity check was added in pull #3843 to avoid adding redeemScripts that
     * never can be redeemed. However, old wallets may still contain these. Do
     * not add them to the wallet and warn.
     */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        std::string strAddr = EncodeDestination(CScriptID(redeemScript));
        LOGA("%s: Warning: This wallet contains a redeemScript of size %i "
             "which exceeds maximum size %i thus can never be redeemed. "
             "Do not use address %s.\n",
            __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::LoadFreezeScript(CPubKey newKey, CScriptNum nFreezeLockTime, std::string strLabel, std::string &address)
{
    // Template rpcdump.cpp::ImportAddress();

    // Get Freeze Script
    CScript freezeScript = GetScriptForFreeze(nFreezeLockTime, newKey);

    // Test and Add Script to wallet
    if (!this->HaveCScript(freezeScript) && !this->AddCScript(freezeScript))
    {
        LOGA("LoadFreezeScript: Error adding p2sh freeze redeemScript to wallet. \n ");
        return false;
    }
    // If just added then return P2SH for user
    address = EncodeDestination(CScriptID(freezeScript));
    LOGA("CLTV Freeze Script Load \n %s => %s \n ", ::ScriptToAsmStr(freezeScript), address.c_str());
    return true;
}

bool CWallet::AddWatchOnly(const CScript &dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    NotifyWatchonlyChanged(true);
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (fFileBacked)
        if (!CWalletDB(strWalletFile).EraseWatchOnly(dest))
            return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest) { return CCryptoKeyStore::AddWatchOnly(dest); }
bool CWallet::Unlock(const SecureString &strWalletPassphrase)
{
    CCrypter crypter;
    CKeyingMaterial _vMasterKey;

    {
        LOCK(cs_wallet);
        for (const MasterKeyMap::value_type &pMasterKey : mapMasterKeys)
        {
            if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt,
                    pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::Unlock(_vMasterKey))
                return true;
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString &strOldWalletPassphrase,
    const SecureString &strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial _vMasterKey;
        for (MasterKeyMap::value_type &pMasterKey : mapMasterKeys)
        {
            if (!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt,
                    pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(_vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt,
                    pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations =
                    pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt,
                    pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations =
                    (pMasterKey.second.nDeriveIterations +
                        pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) /
                    2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                LOGA("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt,
                        pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(_vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::SetBestChain(const CBlockLocator &loc)
{
    CWalletDB walletdb(strWalletFile);
    walletdb.WriteBestBlock(loc);
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB *pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
        nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked)
    {
        CWalletDB *pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
        if (nWalletVersion > 40000)
            pwalletdb->WriteMinVersion(nWalletVersion);
        if (!pwalletdbIn)
            delete pwalletdb;
    }

    return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

set<uint256> CWallet::GetConflicts(const uint256 &txid) const
{
    set<uint256> result;
    AssertLockHeld(cs_wallet);

    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(txid);
    if (it == mapWallet.end())
        return result;
    const CWalletTx &wtx = it->second;

    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    for (const CTxIn &txin : wtx.vin)
    {
        if (mapTxSpends.count(txin.prevout) <= 1)
            continue; // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator it2 = range.first; it2 != range.second; ++it2)
            result.insert(it2->second);
    }
    return result;
}

void CWallet::Flush(bool shutdown) { bitdb.Flush(shutdown); }
bool CWallet::Verify()
{
    std::string walletFile = GetArg("-wallet", DEFAULT_WALLET_DAT);

    LOGA("Using wallet %s\n", walletFile);
    uiInterface.InitMessage(_("Verifying wallet..."));

    // Wallet file must be a plain filename without a directory
    if (walletFile != boost::filesystem::basename(walletFile) + boost::filesystem::extension(walletFile))
        return InitError(
            strprintf(_("Wallet %s resides outside data directory %s"), walletFile, GetDataDir().string()));

    if (!bitdb.Open(GetDataDir()))
    {
        // try moving the database env out of the way
        boost::filesystem::path pathDatabase = GetDataDir() / "database";
        boost::filesystem::path pathDatabaseBak = GetDataDir() / strprintf("database.%d.bak", GetTime());
        try
        {
            boost::filesystem::rename(pathDatabase, pathDatabaseBak);
            LOGA("Moved old %s to %s. Retrying.\n", pathDatabase.string(), pathDatabaseBak.string());
        }
        catch (const boost::filesystem::filesystem_error &)
        {
            // failure is ok (well, not really, but it's not worse than what we started with)
        }

        // try again
        if (!bitdb.Open(GetDataDir()))
        {
            // if it still fails, it probably means we can't even create the database env
            return InitError(strprintf(_("Error initializing wallet database environment %s!"), GetDataDir()));
        }
    }

    if (GetBoolArg("-salvagewallet", false))
    {
        // Recover readable keypairs:
        if (!CWalletDB::Recover(bitdb, walletFile, true))
            return false;
    }

    if (boost::filesystem::exists(GetDataDir() / walletFile))
    {
        CDBEnv::VerifyResult r = bitdb.Verify(walletFile, CWalletDB::Recover);
        if (r == CDBEnv::RECOVER_OK)
        {
            InitWarning(strprintf(_("Warning: Wallet file corrupt, data salvaged!"
                                    " Original %s saved as %s in %s; if"
                                    " your balance or transactions are incorrect you should"
                                    " restore from a backup."),
                walletFile, "wallet.{timestamp}.bak", GetDataDir()));
        }
        if (r == CDBEnv::RECOVER_FAIL)
            return InitError(strprintf(_("%s corrupt, salvage failed"), walletFile));
    }

    return true;
}

void CWallet::SyncMetaData(pair<TxSpends::iterator, TxSpends::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTx *copyFrom = nullptr;
    for (TxSpends::iterator it = range.first; it != range.second; ++it)
    {
        const uint256 &hash = it->second;
        int n = mapWallet[hash].nOrderPos;
        if (n < nMinOrderPos)
        {
            nMinOrderPos = n;
            copyFrom = &mapWallet[hash];
        }
    }
    // Now copy data from copyFrom to rest:
    for (TxSpends::iterator it = range.first; it != range.second; ++it)
    {
        const uint256 &hash = it->second;
        CWalletTx *copyTo = &mapWallet[hash];
        if (copyFrom == copyTo)
            continue;
        if (!copyFrom->IsEquivalentTo(*copyTo))
            continue;
        copyTo->mapValue = copyFrom->mapValue;
        copyTo->vOrderForm = copyFrom->vOrderForm;
        // fTimeReceivedIsTxTime not copied on purpose
        // nTimeReceived not copied on purpose
        copyTo->nTimeSmart = copyFrom->nTimeSmart;
        copyTo->fFromMe = copyFrom->fFromMe;
        copyTo->strFromAccount = copyFrom->strFromAccount;
        // nOrderPos not copied on purpose
        // cached members not copied on purpose
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256 &hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
    {
        const uint256 &wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end())
        {
            int depth = mit->second.GetDepthInMainChain();
            if (depth > 0 || (depth == 0 && !mit->second.isAbandoned()))
                return true; // Spent
        }
    }
    return false;
}

void CWallet::AddToSpends(const COutPoint &outpoint, const uint256 &wtxid)
{
    mapTxSpends.insert(make_pair(outpoint, wtxid));

    pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData(range);
}


void CWallet::AddToSpends(const uint256 &wtxid)
{
    assert(mapWallet.count(wtxid));
    CWalletTx &thisTx = mapWallet[wtxid];
    if (thisTx.IsCoinBase()) // Coinbases don't spend anything!
        return;

    for (const CTxIn &txin : thisTx.vin)
    {
        AddToSpends(txin.prevout, wtxid);
    }
}

bool CWallet::EncryptWallet(const SecureString &strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial _vMasterKey;
    RandAddSeedPerfmon();

    _vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetRandBytes(&_vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;
    RandAddSeedPerfmon();

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(
        strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations =
        (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) /
        2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    LOGA("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(
            strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(_vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked)
        {
            assert(!pwalletdbEncryption);
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            if (!pwalletdbEncryption->TxnBegin())
            {
                delete pwalletdbEncryption;
                pwalletdbEncryption = nullptr;
                return false;
            }
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        if (!EncryptKeys(_vMasterKey))
        {
            if (fFileBacked)
            {
                pwalletdbEncryption->TxnAbort();
                delete pwalletdbEncryption;
            }
            // We now probably have half of our keys encrypted in memory, and half not...
            // die and let the user reload the unencrypted wallet.
            assert(false);
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        if (fFileBacked)
        {
            if (!pwalletdbEncryption->TxnCommit())
            {
                delete pwalletdbEncryption;
                // We now have keys encrypted in memory, but not on disk...
                // die to avoid confusion and let the user reload the unencrypted wallet.
                assert(false);
            }

            delete pwalletdbEncryption;
            pwalletdbEncryption = nullptr;
        }

        Lock();
        Unlock(strWalletPassphrase);

        // if we are using HD, replace the HD master key with a new one
        if (!hdChain.masterKeyID.IsNull())
        {
            CKey key;
            CPubKey masterPubKey = GenerateNewHDMasterKey();
            if (!SetHDMasterKey(masterPubKey))
                return false;
        }

        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);
    }
    NotifyStatusChanged(this);

    return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb)
    {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    }
    else
    {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

void CWallet::MarkDirty()
{
    LOCK(cs_wallet);
    for (PAIRTYPE(const uint256, CWalletTx) & item : mapWallet)
    {
        item.second.MarkDirty();
    }
}

bool CWallet::AddToWallet(const CWalletTx &wtxIn, bool fFromLoadWallet, CWalletDB *pwalletdb)
{
    uint256 hash = wtxIn.GetHash();

    LOCK2(cs_main, cs_wallet);
    if (fFromLoadWallet)
    {
        mapWallet[hash] = wtxIn;
        CWalletTx &wtx = mapWallet[hash];
        wtx.BindWallet(this);
        wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry *)0)));
        AddToSpends(hash);
        for (const CTxIn &txin : wtx.vin)
        {
            if (mapWallet.count(txin.prevout.hash))
            {
                CWalletTx &prevtx = mapWallet[txin.prevout.hash];
                if (prevtx.nIndex == -1 && !prevtx.hashUnset())
                {
                    MarkConflicted(prevtx.hashBlock, wtx.GetHash());
                }
            }
        }
    }
    else
    {
        // Inserts only if not already there, returns tx inserted or tx found
        pair<map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(make_pair(hash, wtxIn));
        CWalletTx &wtx = (*ret.first).second;
        wtx.BindWallet(this);
        bool fInsertedNew = ret.second;
        if (fInsertedNew)
        {
            wtx.nTimeReceived = GetAdjustedTime();
            wtx.nOrderPos = IncOrderPosNext(pwalletdb);
            wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry *)0)));

            wtx.nTimeSmart = wtx.nTimeReceived;
            if (!wtxIn.hashUnset())
            {
                if (mapBlockIndex.count(wtxIn.hashBlock))
                {
                    int64_t latestNow = wtx.nTimeReceived;
                    int64_t latestEntry = 0;
                    {
                        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
                        int64_t latestTolerated = latestNow + 300;
                        const TxItems &txOrdered = wtxOrdered;
                        for (TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
                        {
                            CWalletTx *const pwtx = (*it).second.first;
                            if (pwtx == &wtx)
                                continue;
                            CAccountingEntry *const pacentry = (*it).second.second;
                            int64_t nSmartTime;
                            if (pwtx)
                            {
                                nSmartTime = pwtx->nTimeSmart;
                                if (!nSmartTime)
                                    nSmartTime = pwtx->nTimeReceived;
                            }
                            else
                                nSmartTime = pacentry->nTime;
                            if (nSmartTime <= latestTolerated)
                            {
                                latestEntry = nSmartTime;
                                if (nSmartTime > latestNow)
                                    latestNow = nSmartTime;
                                break;
                            }
                        }
                    }

                    int64_t blocktime = mapBlockIndex[wtxIn.hashBlock]->GetBlockTime();
                    wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
                }
                else
                    LOGA("AddToWallet(): found %s in block %s not in index\n", wtxIn.GetHash().ToString(),
                        wtxIn.hashBlock.ToString());
            }
            AddToSpends(hash);
        }

        bool fUpdated = false;
        if (!fInsertedNew)
        {
            // Merge
            if (!wtxIn.hashUnset() && wtxIn.hashBlock != wtx.hashBlock)
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            // If no longer abandoned, update
            if (wtxIn.hashBlock.IsNull() && wtx.isAbandoned())
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.nIndex != wtx.nIndex))
            {
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
            {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
        }

        //// debug print
        LOGA("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""),
            (fUpdated ? "update" : ""));

        // Write to disk
        if (fInsertedNew || fUpdated)
            if (!wtx.WriteToDisk(pwalletdb))
                return false;

        // Break debit/credit balance caches:
        wtx.MarkDirty();

        // Notify UI of new or updated transaction
        NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

        // notify an external script when a wallet transaction comes in or is updated
        std::string strCmd = GetArg("-walletnotify", "");

        if (!strCmd.empty())
        {
            boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }
    }
    return true;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 */
bool CWallet::AddToWalletIfInvolvingMe(const CTransactionRef &ptx, const CBlock *pblock, bool fUpdate, int txIndex)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    if (pblock)
    {
        for (const CTxIn &txin : ptx->vin)
        {
            std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range = mapTxSpends.equal_range(txin.prevout);
            while (range.first != range.second)
            {
                if (range.first->second != ptx->GetHash())
                {
                    LOGA("Transaction %s (in block %s) conflicts with wallet transaction %s (both spend %s:%i)\n",
                        ptx->GetHash().ToString(), pblock->GetHash().ToString(), range.first->second.ToString(),
                        range.first->first.hash.ToString(), range.first->first.n);
                    MarkConflicted(pblock->GetHash(), range.first->second);
                }
                range.first++;
            }
        }
    }

    bool fExisted = mapWallet.count(ptx->GetHash()) != 0;
    if (fExisted && !fUpdate)
        return false;
    if (fExisted || IsMine(*ptx) || IsFromMe(*ptx))
    {
        CWalletTx wtx(this, *ptx);

        // Get merkle branch if transaction was found in a block
        if (pblock)
            wtx.SetMerkleBranch(*pblock, txIndex);

        // Do not flush the wallet here for performance reasons
        // this is safe, as in case of a crash, we rescan the necessary blocks on startup through our
        // SetBestChain-mechanism
        CWalletDB walletdb(strWalletFile, "r+", false);

        return AddToWallet(wtx, false, &walletdb);
    }
    return false;
}

bool CWallet::AbandonTransaction(const uint256 &hashTx)
{
    LOCK2(cs_main, cs_wallet);

    // Do not flush the wallet here for performance reasons
    CWalletDB walletdb(strWalletFile, "r+", false);

    std::set<uint256> todo;
    std::set<uint256> done;

    // Can't mark abandoned if confirmed or in mempool
    assert(mapWallet.count(hashTx));
    CWalletTx &origtx = mapWallet[hashTx];
    if (origtx.GetDepthInMainChain() > 0 || origtx.InMempool())
    {
        return false;
    }

    todo.insert(hashTx);

    while (!todo.empty())
    {
        uint256 now = *todo.begin();
        todo.erase(now);
        done.insert(now);
        assert(mapWallet.count(now));
        CWalletTx &wtx = mapWallet[now];
        int currentconfirm = wtx.GetDepthInMainChain();
        // If the orig tx was not in block, none of its spends can be
        assert(currentconfirm <= 0);
        // if (currentconfirm < 0) {Tx and spends are already conflicted, no need to abandon}
        if (currentconfirm == 0 && !wtx.isAbandoned())
        {
            // If the orig tx was not in block/mempool, none of its spends can be in mempool
            assert(!wtx.InMempool());
            wtx.nIndex = -1;
            wtx.setAbandoned();
            wtx.MarkDirty();
            wtx.WriteToDisk(&walletdb);
            NotifyTransactionChanged(this, wtx.GetHash(), CT_UPDATED);
            // Iterate over all its outputs, and mark transactions in the wallet that spend them abandoned too
            TxSpends::const_iterator iter = mapTxSpends.lower_bound(COutPoint(hashTx, 0));
            while (iter != mapTxSpends.end() && iter->first.hash == now)
            {
                if (!done.count(iter->second))
                {
                    todo.insert(iter->second);
                }
                iter++;
            }
            // If a transaction changes 'conflicted' state, that changes the balance
            // available of the outputs it spends. So force those to be recomputed
            for (const CTxIn &txin : wtx.vin)
            {
                if (mapWallet.count(txin.prevout.hash))
                    mapWallet[txin.prevout.hash].MarkDirty();
            }
        }
    }

    return true;
}

void CWallet::MarkConflicted(const uint256 &hashBlock, const uint256 &hashTx)
{
    LOCK2(cs_main, cs_wallet);

    int conflictconfirms = 0;
    if (mapBlockIndex.count(hashBlock))
    {
        CBlockIndex *pindex = mapBlockIndex[hashBlock];
        if (chainActive.Contains(pindex))
        {
            conflictconfirms = -(chainActive.Height() - pindex->nHeight + 1);
        }
    }
    // If number of conflict confirms cannot be determined, this means
    // that the block is still unknown or not yet part of the main chain,
    // for example when loading the wallet during a reindex. Do nothing in that
    // case.
    if (conflictconfirms >= 0)
        return;

    // Do not flush the wallet here for performance reasons
    CWalletDB walletdb(strWalletFile, "r+", false);

    std::set<uint256> todo;
    std::set<uint256> done;

    todo.insert(hashTx);

    while (!todo.empty())
    {
        uint256 now = *todo.begin();
        todo.erase(now);
        done.insert(now);
        assert(mapWallet.count(now));
        CWalletTx &wtx = mapWallet[now];
        int currentconfirm = wtx.GetDepthInMainChain();
        if (conflictconfirms < currentconfirm)
        {
            // Block is 'more conflicted' than current confirm; update.
            // Mark transaction as conflicted with this block.
            wtx.nIndex = -1;
            wtx.hashBlock = hashBlock;
            wtx.MarkDirty();
            wtx.WriteToDisk(&walletdb);
            // Iterate over all its outputs, and mark transactions in the wallet that spend them conflicted too
            TxSpends::const_iterator iter = mapTxSpends.lower_bound(COutPoint(now, 0));
            while (iter != mapTxSpends.end() && iter->first.hash == now)
            {
                if (!done.count(iter->second))
                {
                    todo.insert(iter->second);
                }
                iter++;
            }
            // If a transaction changes 'conflicted' state, that changes the balance
            // available of the outputs it spends. So force those to be recomputed
            for (const CTxIn &txin : wtx.vin)
            {
                if (mapWallet.count(txin.prevout.hash))
                    mapWallet[txin.prevout.hash].MarkDirty();
            }
        }
    }
}

void CWallet::SyncTransaction(const CTransactionRef &ptx, const CBlock *pblock, int txIdx)
{
    LOCK2(cs_main, cs_wallet);

    if (!AddToWalletIfInvolvingMe(ptx, pblock, true, txIdx))
        return; // Not one of ours

    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    for (const CTxIn &txin : ptx->vin)
    {
        if (mapWallet.count(txin.prevout.hash))
            mapWallet[txin.prevout.hash].MarkDirty();
    }
}

CAmount CWallet::GetDebit(const CTxIn &txin, const isminefilter &filter) const
{
    LOCK(cs_wallet);
    map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
    if (mi != mapWallet.end())
    {
        const CWalletTx &prev = (*mi).second;
        if (txin.prevout.n < prev.vout.size())
            if (IsMine(prev.vout[txin.prevout.n]) & filter)
                return prev.vout[txin.prevout.n].nValue;
    }
    return 0;
}

isminetype CWallet::IsMine(const CTxDestination &dest) const { return ::IsMine(*this, dest, chainActive.Tip()); }
isminetype CWallet::IsMine(const CTxOut &txout) const { return ::IsMine(*this, txout.scriptPubKey, chainActive.Tip()); }
isminetype CWallet::IsMine(const CTxIn &txin) const
{
    LOCK(cs_wallet);
    map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
    if (mi != mapWallet.end())
    {
        const CWalletTx &prev = (*mi).second;
        if (txin.prevout.n < prev.vout.size())
            return IsMine(prev.vout[txin.prevout.n]);
    }
    return ISMINE_NO;
}

bool CWallet::IsMine(const CTransaction &tx) const
{
    for (const CTxOut &txout : tx.vout)
    {
        if (IsMine(txout) != ISMINE_NO)
            return true;
    }
    return false;
}

CAmount CWallet::GetCredit(const CTxOut &txout, const isminefilter &filter) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetCredit(): value out of range");
    return ((IsMine(txout) & filter) ? txout.nValue : 0);
}

bool CWallet::IsChange(const CTxOut &txout) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, txout.scriptPubKey, chainActive.Tip()))
    {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
            return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

CAmount CWallet::GetChange(const CTxOut &txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetChange(): value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}

bool CWallet::IsFromMe(const CTransaction &tx) const { return (GetDebit(tx, ISMINE_ALL) > 0); }
CAmount CWallet::GetDebit(const CTransaction &tx, const isminefilter &filter) const
{
    CAmount nDebit = 0;
    for (const CTxIn &txin : tx.vin)
    {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit))
            throw std::runtime_error("CWallet::GetDebit(): value out of range");
    }
    return nDebit;
}

CAmount CWallet::GetCredit(const CTransaction &tx, const isminefilter &filter) const
{
    CAmount nCredit = 0;
    for (const CTxOut &txout : tx.vout)
    {
        nCredit += GetCredit(txout, filter);
        if (!MoneyRange(nCredit))
            throw std::runtime_error("CWallet::GetCredit(): value out of range");
    }
    return nCredit;
}

CAmount CWallet::GetChange(const CTransaction &tx) const
{
    CAmount nChange = 0;
    for (const CTxOut &txout : tx.vout)
    {
        nChange += GetChange(txout);
        if (!MoneyRange(nChange))
            throw std::runtime_error("CWallet::GetChange(): value out of range");
    }
    return nChange;
}

CPubKey CWallet::GenerateNewHDMasterKey()
{
    CKey key;
    key.MakeNewKey(true);

    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // calculate the pubkey
    CPubKey pubkey = key.GetPubKey();
    assert(key.VerifyPubKey(pubkey));

    // set the hd keypath to "m" -> Master, refers the masterkeyid to itself
    metadata.hdKeypath = "m";
    metadata.hdMasterKeyID = pubkey.GetID();

    {
        LOCK(cs_wallet);

        // mem store the metadata
        mapKeyMetadata[pubkey.GetID()] = metadata;

        // write the key&metadata to the database
        if (!AddKeyPubKey(key, pubkey))
            throw std::runtime_error("CWallet::GenerateNewKey(): AddKey failed");
    }

    return pubkey;
}

bool CWallet::SetHDMasterKey(const CPubKey &pubkey)
{
    LOCK(cs_wallet);

    // ensure this wallet.dat can only be opened by clients supporting HD
    SetMinVersion(FEATURE_HD);

    // store the keyid (hash160) together with
    // the child index counter in the database
    // as a hdchain object
    CHDChain newHdChain;
    newHdChain.masterKeyID = pubkey.GetID();
    SetHDChain(newHdChain, false);

    return true;
}

bool CWallet::SetHDChain(const CHDChain &chain, bool memonly)
{
    LOCK(cs_wallet);
    if (!memonly && !CWalletDB(strWalletFile).WriteHDChain(chain))
        throw runtime_error("AddHDChain(): writing chain failed");

    hdChain = chain;
    return true;
}

bool CWallet::IsHDEnabled() { return !hdChain.masterKeyID.IsNull(); }
int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int CWalletTx::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1;
    {
        LOCK(pwallet->cs_wallet);
        if (IsCoinBase())
        {
            // Generated block
            if (!hashUnset())
            {
                map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                if (mi != pwallet->mapRequestCount.end())
                    nRequests = (*mi).second;
            }
        }
        else
        {
            // Did anyone request this transaction?
            map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(GetHash());
            if (mi != pwallet->mapRequestCount.end())
            {
                nRequests = (*mi).second;

                // How about the block it's in?
                if (nRequests == 0 && !hashUnset())
                {
                    map<uint256, int>::const_iterator mi2 = pwallet->mapRequestCount.find(hashBlock);
                    if (mi2 != pwallet->mapRequestCount.end())
                        nRequests = (*mi2).second;
                    else
                        nRequests = 1; // If it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

void CWalletTx::GetAmounts(list<COutputEntry> &listReceived,
    list<COutputEntry> &listSent,
    CAmount &nFee,
    string &strSentAccount,
    const isminefilter &filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Compute fee:
    CAmount nDebit = GetDebit(filter);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        CAmount nValueOut = GetValueOut();
        nFee = nDebit - nValueOut;
    }

    // Sent/received.
    for (unsigned int i = 0; i < vout.size(); ++i)
    {
        const CTxOut &txout = vout[i];
        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        }
        else if (!(fIsMine & filter))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;

        if (!ExtractDestination(txout.scriptPubKey, address) && !txout.scriptPubKey.IsUnspendable())
        {
            LOGA("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n", this->GetHash().ToString());
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }
}

void CWalletTx::GetAccountAmounts(const string &strAccount,
    CAmount &nReceived,
    CAmount &nSent,
    CAmount &nFee,
    const isminefilter &filter) const
{
    nReceived = nSent = nFee = 0;

    CAmount allFee;
    string strSentAccount;
    list<COutputEntry> listReceived;
    list<COutputEntry> listSent;
    GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

    if (strAccount == strSentAccount)
    {
        for (const COutputEntry &s : listSent)
        {
            nSent += s.amount;
        }
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        for (const COutputEntry &r : listReceived)
        {
            if (pwallet->mapAddressBook.count(r.destination))
            {
                map<CTxDestination, CAddressBookData>::const_iterator mi = pwallet->mapAddressBook.find(r.destination);
                if (mi != pwallet->mapAddressBook.end() && (*mi).second.name == strAccount)
                    nReceived += r.amount;
            }
            else if (strAccount.empty())
            {
                nReceived += r.amount;
            }
        }
    }
}


bool CWalletTx::WriteToDisk(CWalletDB *pwalletdb) { return pwalletdb->WriteTx(GetHash(), *this); }
/**
 * Scan the block chain (starting in pindexStart) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 */
int CWallet::ScanForWalletTransactions(CBlockIndex *pindexStart, bool fUpdate)
{
    // Begin rescan by setting fRescan to true.  This prevents any new inbound network connections
    // from being initiated and thus prevents us from banning repeated and failed network connection
    // attempts while the rescan is in progress.  Once the flag is set then it is safe to disconnect
    // any current connections. Note: we don't disconnect nodes in regtest as this prevents the tests
    // from passing since the nodes will not auto-reconnect after a wallet scan has completed.
    fRescan = true;
    if (Params().NetworkIDString() != "regtest")
    {
        LOCK(cs_vNodes);
        for (CNode *pnode : vNodes)
        {
            LOGA("Disconnecting peer: %s before wallet rescan\n", pnode->GetLogName());
            pnode->fDisconnect = true;
        }
    }

    int ret = 0;
    int64_t nNow = GetTime();
    const CChainParams &chainParams = Params();

    CBlockIndex *pindex = pindexStart;
    {
        LOCK2(cs_main, cs_wallet);

        // no need to read and scan block, if block was created before
        // our wallet birthday (as adjusted for block time variability)
        while (pindex && nTimeFirstKey && (pindex->GetBlockTime() < (nTimeFirstKey - 7200)))
            pindex = chainActive.Next(pindex);

        // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
        ShowProgress(_("Rescanning..."), 0);
        double dProgressStart = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex, false);
        double dProgressTip =
            Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip(), false);
        while (pindex)
        {
            if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
                ShowProgress(
                    _("Rescanning..."), std::max(1, std::min(99, (int)((Checkpoints::GuessVerificationProgress(
                                                                            chainParams.Checkpoints(), pindex, false) -
                                                                           dProgressStart) /
                                                                       (dProgressTip - dProgressStart) * 100))));

            CBlock block;
            ReadBlockFromDisk(block, pindex, Params().GetConsensus());
            int txIdx = 0;
            for (const auto &ptx : block.vtx)
            {
                if (AddToWalletIfInvolvingMe(ptx, &block, fUpdate, txIdx))
                    ret++;
                txIdx++;
            }
            pindex = chainActive.Next(pindex);
            if (GetTime() >= nNow + 60)
            {
                nNow = GetTime();
                LOGA("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight,
                    Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex));
            }
        }
        ShowProgress(_("Rescanning..."), 100); // hide progress dialog in GUI
    }
    // Rescan is now finished. Set to false to allow network connections to resume.
    fRescan = false;

    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    // If transactions aren't being broadcasted, don't let them into local mempool either
    if (!fBroadcastTransactions)
        return;
    LOCK2(cs_main, cs_wallet);
    std::map<int64_t, CWalletTx *> mapSorted;

    // Sort pending wallet transactions based on their initial wallet insertion order
    for (PAIRTYPE(const uint256, CWalletTx) & item : mapWallet)
    {
        const uint256 &wtxid = item.first;
        CWalletTx &wtx = item.second;
        assert(wtx.GetHash() == wtxid);

        int nDepth = wtx.GetDepthInMainChain();

        if (!wtx.IsCoinBase() && (nDepth == 0 && !wtx.isAbandoned()))
        {
            mapSorted.insert(std::make_pair(wtx.nOrderPos, &wtx));
        }
    }

    // Try to add wallet transactions to memory pool
    for (std::pair<const int64_t, CWalletTx *> &item : mapSorted)
    {
        CWalletTx &wtx = *(item.second);

        wtx.AcceptToMemoryPool(false);
        SyncWithWallets(MakeTransactionRef(wtx), nullptr, -1);
    }
}

bool CWalletTx::RelayWalletTransaction()
{
    assert(pwallet->GetBroadcastTransactions());
    if (!IsCoinBase())
    {
        if (GetDepthInMainChain() == 0 && !isAbandoned() && InMempool())
        {
            LOGA("Relaying wtx %s\n", GetHash().ToString());
            RelayTransaction((CTransaction) * this);
            return true;
        }
    }
    return false;
}

set<uint256> CWalletTx::GetConflicts() const
{
    set<uint256> result;
    if (pwallet != nullptr)
    {
        uint256 myHash = GetHash();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }
    return result;
}

CAmount CWalletTx::GetDebit(const isminefilter &filter) const
{
    if (vin.empty())
        return 0;

    CAmount debit = 0;
    if (filter & ISMINE_SPENDABLE)
    {
        if (fDebitCached)
            debit += nDebitCached;
        else
        {
            nDebitCached = pwallet->GetDebit(*this, ISMINE_SPENDABLE);
            fDebitCached = true;
            debit += nDebitCached;
        }
    }
    if (filter & ISMINE_WATCH_ONLY)
    {
        if (fWatchDebitCached)
            debit += nWatchDebitCached;
        else
        {
            nWatchDebitCached = pwallet->GetDebit(*this, ISMINE_WATCH_ONLY);
            fWatchDebitCached = true;
            debit += nWatchDebitCached;
        }
    }
    return debit;
}

CAmount CWalletTx::GetCredit(const isminefilter &filter) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    int64_t credit = 0;
    if (filter & ISMINE_SPENDABLE)
    {
        // GetBalance can assume transactions in mapWallet won't change
        if (fCreditCached)
            credit += nCreditCached;
        else
        {
            nCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
            fCreditCached = true;
            credit += nCreditCached;
        }
    }
    if (filter & ISMINE_WATCH_ONLY)
    {
        if (fWatchCreditCached)
            credit += nWatchCreditCached;
        else
        {
            nWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
            fWatchCreditCached = true;
            credit += nWatchCreditCached;
        }
    }
    return credit;
}

CAmount CWalletTx::GetImmatureCredit(bool fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureCreditCached)
            return nImmatureCreditCached;
        nImmatureCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
        fImmatureCreditCached = true;
        return nImmatureCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableCredit(bool fUseCache) const
{
    if (pwallet == 0)
        return 0;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    if (fUseCache && fAvailableCreditCached)
        return nAvailableCreditCached;

    CAmount nCredit = 0;
    uint256 hashTx = GetHash();
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        if (!pwallet->IsSpent(hashTx, i))
        {
            const CTxOut &txout = vout[i];
            nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTx::GetAvailableCredit(false) : value out of range");
        }
    }

    nAvailableCreditCached = nCredit;
    fAvailableCreditCached = true;
    return nCredit;
}

CAmount CWalletTx::GetImmatureWatchOnlyCredit(const bool &fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureWatchCreditCached)
            return nImmatureWatchCreditCached;
        nImmatureWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
        fImmatureWatchCreditCached = true;
        return nImmatureWatchCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableWatchOnlyCredit(const bool &fUseCache) const
{
    if (pwallet == 0)
        return 0;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    if (fUseCache && fAvailableWatchCreditCached)
        return nAvailableWatchCreditCached;

    CAmount nCredit = 0;
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        if (!pwallet->IsSpent(GetHash(), i))
        {
            const CTxOut &txout = vout[i];
            nCredit += pwallet->GetCredit(txout, ISMINE_WATCH_ONLY);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTx::GetAvailableCredit(false) : value out of range");
        }
    }

    nAvailableWatchCreditCached = nCredit;
    fAvailableWatchCreditCached = true;
    return nCredit;
}

CAmount CWalletTx::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*this);
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTx::InMempool() const
{
    if (mempool.exists(GetHash()))
    {
        return true;
    }
    return false;
}

bool CWalletTx::IsTrusted() const
{
    // Quick answer in most cases
    if (!CheckFinalTx(*this))
        return false;
    int nDepth = GetDepthInMainChain();
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (!bSpendZeroConfChange || !IsFromMe(ISMINE_ALL)) // using wtx's cached debit
        return false;

    // Don't trust unconfirmed transactions from us unless they are in the mempool.
    if (!InMempool())
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const CTxIn &txin : vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx *parent = pwallet->GetWalletTx(txin.prevout.hash);
        if (parent == nullptr)
            return false;
        const CTxOut &parentOut = parent->vout[txin.prevout.n];
        if (pwallet->IsMine(parentOut) != ISMINE_SPENDABLE)
            return false;
    }
    return true;
}

std::vector<uint256> CWallet::ResendWalletTransactionsBefore(int64_t nTime)
{
    std::vector<uint256> result;

    LOCK(cs_wallet);
    // Sort them in chronological order
    multimap<unsigned int, CWalletTx *> mapSorted;
    for (PAIRTYPE(const uint256, CWalletTx) & item : mapWallet)
    {
        CWalletTx &wtx = item.second;
        // Don't rebroadcast if newer than nTime:
        if (wtx.nTimeReceived > nTime)
            continue;
        mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
    }
    for (PAIRTYPE(const unsigned int, CWalletTx *) & item : mapSorted)
    {
        CWalletTx &wtx = *item.second;
        if (wtx.RelayWalletTransaction())
            result.push_back(wtx.GetHash());
    }
    return result;
}

void CWallet::ResendWalletTransactions(int64_t nBestBlockTime)
{
    // Do this infrequently and randomly to avoid giving away
    // that these are our transactions.
    if (GetTime() < nNextResend || !fBroadcastTransactions)
        return;
    bool fFirst = (nNextResend == 0);
    nNextResend = GetTime() + GetRand(30 * 60);
    if (fFirst)
        return;

    // Only do it if there's been a new block since last time
    if (nBestBlockTime < nLastResend)
        return;
    nLastResend = GetTime();

    // Rebroadcast unconfirmed txes older than 5 minutes before the last
    // block was found:
    std::vector<uint256> relayed = ResendWalletTransactionsBefore(nBestBlockTime - 5 * 60);
    if (!relayed.empty())
        LOGA("%s: rebroadcast %u unconfirmed transactions\n", __func__, relayed.size());
}

/** @} */ // end of mapWallet


/** @defgroup Actions
 *
 * @{
 */


CAmount CWallet::GetBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx *pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableCredit(false);
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx *pcoin = &(*it).second;
            if (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0 && pcoin->InMempool())
                nTotal += pcoin->GetAvailableCredit(false);
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx *pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureCredit(false);
        }
    }
    return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx *pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableWatchOnlyCredit(false);
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx *pcoin = &(*it).second;
            if (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0 && pcoin->InMempool())
                nTotal += pcoin->GetAvailableWatchOnlyCredit(false);
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx *pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureWatchOnlyCredit(false);
        }
    }
    return nTotal;
}

void CWallet::AvailableCoins(vector<COutput> &vCoins,
    bool fOnlyConfirmed,
    const CCoinControl *coinControl,
    bool fIncludeZeroValue) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const uint256 &wtxid = it->first;
            const CWalletTx *pcoin = &(*it).second;

            if (!CheckFinalTx(*pcoin))
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 0)
                continue;

            // We should not consider coins which aren't at least in our mempool
            // It's possible for these to be conflicted via ancestors which we may never be able to detect
            if (nDepth == 0 && !pcoin->InMempool())
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                isminetype mine = IsMine(pcoin->vout[i]);
                if (!(IsSpent(wtxid, i)) && mine != ISMINE_NO && !IsLockedCoin((*it).first, i) &&
                    (pcoin->vout[i].nValue > 0 || fIncludeZeroValue) &&
                    (!coinControl || !coinControl->HasSelected() || coinControl->fAllowOtherInputs ||
                        coinControl->IsSelected((*it).first, i)))
                {
                    // The UTXO is available
                    COutput outpoint(pcoin, i, nDepth,
                        ((mine & ISMINE_SPENDABLE) != ISMINE_NO) || (coinControl && coinControl->fAllowWatchOnly &&
                                                                        (mine & ISMINE_WATCH_SOLVABLE) != ISMINE_NO));
                    vCoins.push_back(outpoint);
                    LOG(SELECTCOINS, "Freeze Available %s MINE=%s\n ", pcoin->vout[i].ToString(), mine);
                }
            }
        }
    }
}

static void ApproximateBestSubset(vector<pair<CAmount, pair<const CWalletTx *, unsigned int> > > vValue,
    const CAmount &nTotalLower,
    const CAmount &nTargetValue,
    vector<char> &vfBest,
    CAmount &nBest,
    int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    FastRandomContext insecure_rand;

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        CAmount nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                // The solver here uses a randomized algorithm,
                // the randomness serves no real security purpose but is just
                // needed to prevent degenerate behavior and it is important
                // that the rng is fast. We do not use a constant random sequence,
                // because there may be some privacy improvement by making
                // the selection random.
                if (nPass == 0 ? insecure_rand.rand32() & 1 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }

    // Reduces the approximate best subset by removing any inputs that are smaller than the surplus of nTotal beyond
    // nTargetValue.
    for (unsigned int i = 0; i < vValue.size(); i++)
    {
        if (vfBest[i] && (nBest - vValue[i].first) >= nTargetValue)
        {
            vfBest[i] = false;
            nBest -= vValue[i].first;
        }
    }
}

bool CWallet::SelectCoinsMinConf(const CAmount &nTargetValue,
    int nConfMine,
    int nConfTheirs,
    vector<COutput> vCoins,
    set<pair<const CWalletTx *, unsigned int> > &setCoinsRet,
    CAmount &nValueRet) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    pair<CAmount, pair<const CWalletTx *, unsigned int> > coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<CAmount>::max();
    coinLowestLarger.second.first = nullptr;
    vector<pair<CAmount, pair<const CWalletTx *, unsigned int> > > vValue;
    CAmount nTotalLower = 0;

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

    for (const COutput &output : vCoins)
    {
        if (!output.fSpendable)
            continue;

        const CWalletTx *pcoin = output.tx;

        if (output.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? nConfMine : nConfTheirs))
            continue;

        int i = output.i;
        CAmount n = pcoin->vout[i].nValue;

        pair<CAmount, pair<const CWalletTx *, unsigned int> > coin = make_pair(n, make_pair(pcoin, i));

        if (n == nTargetValue)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            return true;
        }
        else if (n < nTargetValue + MIN_CHANGE)
        {
            vValue.push_back(coin);
            nTotalLower += n;
        }
        else if (n < coinLowestLarger.first)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (unsigned int i = 0; i < vValue.size(); ++i)
        {
            setCoinsRet.insert(vValue[i].second);
            nValueRet += vValue[i].first;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (coinLowestLarger.second.first == nullptr)
            return false;
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    CAmount nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + MIN_CHANGE)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + MIN_CHANGE, vfBest, nBest);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first &&
        ((nBest != nTargetValue && nBest < nTargetValue + MIN_CHANGE) || coinLowestLarger.first <= nBest))
    {
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    }
    else
    {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
            {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
            }

        LOG(SELECTCOINS, "SelectCoins() best subset: ");
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
                LOG(SELECTCOINS, "%s ", FormatMoney(vValue[i].first));
        LOG(SELECTCOINS, "total %s\n", FormatMoney(nBest));
    }

    return true;
}

bool CWallet::SelectCoins(const CAmount &nTargetValue,
    set<pair<const CWalletTx *, unsigned int> > &setCoinsRet,
    CAmount &nValueRet,
    const CCoinControl *coinControl) const
{
    vector<COutput> vCoins;
    AvailableCoins(vCoins, true, coinControl);

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs)
    {
        for (const COutput &out : vCoins)
        {
            if (!out.fSpendable)
                continue;
            nValueRet += out.tx->vout[out.i].nValue;
            setCoinsRet.insert(make_pair(out.tx, out.i));
        }
        return (nValueRet >= nTargetValue);
    }

    // calculate value from preset inputs and store them
    set<pair<const CWalletTx *, uint32_t> > setPresetCoins;
    CAmount nValueFromPresetInputs = 0;

    std::vector<COutPoint> vPresetInputs;
    if (coinControl)
        coinControl->ListSelected(vPresetInputs);
    for (const COutPoint &outpoint : vPresetInputs)
    {
        map<uint256, CWalletTx>::const_iterator it = mapWallet.find(outpoint.hash);
        if (it != mapWallet.end())
        {
            const CWalletTx *pcoin = &it->second;
            // Clearly invalid input, fail
            if (pcoin->vout.size() <= outpoint.n)
                return false;
            nValueFromPresetInputs += pcoin->vout[outpoint.n].nValue;
            setPresetCoins.insert(make_pair(pcoin, outpoint.n));
        }
        else
            return false; // TODO: Allow non-wallet inputs
    }

    // remove preset inputs from vCoins
    for (vector<COutput>::iterator it = vCoins.begin();
         it != vCoins.end() && coinControl && coinControl->HasSelected();)
    {
        if (setPresetCoins.count(make_pair(it->tx, it->i)))
            it = vCoins.erase(it);
        else
            ++it;
    }

    bool res = nTargetValue <= nValueFromPresetInputs ||
               SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 1, 6, vCoins, setCoinsRet, nValueRet) ||
               SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 1, 1, vCoins, setCoinsRet, nValueRet) ||
               (bSpendZeroConfChange &&
                   SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 0, 1, vCoins, setCoinsRet, nValueRet));

    // because SelectCoinsMinConf clears the setCoinsRet, we now add the possible inputs to the coinset
    setCoinsRet.insert(setPresetCoins.begin(), setPresetCoins.end());

    // add preset inputs to the total value selected
    nValueRet += nValueFromPresetInputs;

    return res;
}

bool CWallet::FundTransaction(CMutableTransaction &tx,
    CAmount &nFeeRet,
    int &nChangePosRet,
    std::string &strFailReason,
    bool includeWatching)
{
    vector<CRecipient> vecSend;

    // Turn the txout set into a CRecipient vector
    for (const CTxOut &txOut : tx.vout)
    {
        CRecipient recipient = {txOut.scriptPubKey, txOut.nValue, false};
        vecSend.push_back(recipient);
    }

    CCoinControl coinControl;
    coinControl.fAllowOtherInputs = true;
    coinControl.fAllowWatchOnly = includeWatching;
    for (const CTxIn &txin : tx.vin)
    {
        coinControl.Select(txin.prevout);
    }

    CReserveKey reservekey(this);
    CWalletTx wtx;
    if (!CreateTransaction(vecSend, wtx, reservekey, nFeeRet, nChangePosRet, strFailReason, &coinControl, false))
        return false;

    if (nChangePosRet != -1)
    {
        tx.vout.insert(tx.vout.begin() + nChangePosRet, wtx.vout[nChangePosRet]);
        // we dont have the normal Create/Commit cycle, and dont want to risk reusing change,
        // so just remove the key from the keypool here.
        reservekey.KeepKey();
    }

    // Add new txins (keeping original txin scriptSig/order)
    for (const CTxIn &txin : wtx.vin)
    {
        bool found = false;
        for (const CTxIn &origTxIn : tx.vin)
        {
            if (txin.prevout.hash == origTxIn.prevout.hash && txin.prevout.n == origTxIn.prevout.n)
            {
                found = true;
                break;
            }
        }
        if (!found)
            tx.vin.push_back(txin);
    }

    if (tx.nLockTime == 0)
        tx.nLockTime = wtx.nLockTime;
    return true;
}

bool CWallet::CreateTransaction(const vector<CRecipient> &vecSend,
    CWalletTx &wtxNew,
    CReserveKey &reservekey,
    CAmount &nFeeRet,
    int &nChangePosRet,
    std::string &strFailReason,
    const CCoinControl *coinControl,
    bool sign)
{
    CAmount nValue = 0;
    unsigned int nSubtractFeeFromAmount = 0;
    bool involvesPublicLabel = false;
    for (const CRecipient &recipient : vecSend)
    {
        if (getLabelPublic(recipient.scriptPubKey) != "")
            involvesPublicLabel = true;
        if (nValue < 0 || recipient.nAmount < 0)
        {
            strFailReason = _("Transaction amounts must be positive");
            return false;
        }
        nValue += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount)
            nSubtractFeeFromAmount++;
    }
    if (vecSend.empty() || nValue < 0)
    {
        strFailReason = _("Transaction amounts must be positive");
        return false;
    }

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(this);
    CMutableTransaction txNew;

    // Discourage fee sniping.
    //
    // For a large miner the value of the transactions in the best block and
    // the mempool can exceed the cost of deliberately attempting to mine two
    // blocks to orphan the current best block. By setting nLockTime such that
    // only the next block can include the transaction, we discourage this
    // practice as the height restricted and limited blocksize gives miners
    // considering fee sniping fewer options for pulling off this attack.
    //
    // A simple way to think about this is from the wallet's point of view we
    // always want the blockchain to move forward. By setting nLockTime this
    // way we're basically making the statement that we only want this
    // transaction to appear in the next block; we don't want to potentially
    // encourage reorgs by allowing transactions to appear at lower heights
    // than the next block in forks of the best chain.
    //
    // Of course, the subsidy is high enough, and transaction volume low
    // enough, that fee sniping isn't a problem yet, but by implementing a fix
    // now we ensure code won't be written that makes assumptions about
    // nLockTime that preclude a fix later.
    txNew.nLockTime = chainActive.Height();

    // Secondly occasionally randomly pick a nLockTime even further back, so
    // that transactions that are delayed after signing for whatever reason,
    // e.g. high-latency mix networks and some CoinJoin implementations, have
    // better privacy.
    if (GetRandInt(10) == 0)
        txNew.nLockTime = std::max(0, (int)txNew.nLockTime - GetRandInt(100));

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);

    {
        LOCK2(cs_main, cs_wallet);
        {
            nFeeRet = 0;
            // Start with no fee and loop until there is enough fee
            while (true)
            {
                txNew.vin.clear();
                txNew.vout.clear();
                wtxNew.fFromMe = true;
                nChangePosRet = -1;
                bool fFirst = true;

                CAmount nValueToSelect = nValue;
                if (nSubtractFeeFromAmount == 0)
                    nValueToSelect += nFeeRet;
                double dPriority = 0;
                // vouts to the payees
                for (const CRecipient &recipient : vecSend)
                {
                    CTxOut txout(recipient.nAmount, recipient.scriptPubKey);

                    if (recipient.fSubtractFeeFromAmount)
                    {
                        // Subtract fee equally from each selected recipient
                        txout.nValue -= nFeeRet / nSubtractFeeFromAmount;

                        if (fFirst) // first receiver pays the remainder not divisible by output count
                        {
                            fFirst = false;
                            txout.nValue -= nFeeRet % nSubtractFeeFromAmount;
                        }
                    }

                    if (txout.IsDust())
                    {
                        if (recipient.fSubtractFeeFromAmount && nFeeRet > 0)
                        {
                            if (txout.nValue < 0)
                                strFailReason = _("The transaction amount is too small to pay the fee");
                            else
                                strFailReason =
                                    _("The transaction amount is too small to send after the fee has been deducted");
                        }
                        else
                            strFailReason = _("Transaction amount too small");
                        return false;
                    }
                    txNew.vout.push_back(txout);
                }

                // Choose coins to use
                set<pair<const CWalletTx *, unsigned int> > setCoins;
                CAmount nValueIn = 0;
                if (!SelectCoins(nValueToSelect, setCoins, nValueIn, coinControl))
                {
                    strFailReason = _("Insufficient funds");
                    return false;
                }
                for (PAIRTYPE(const CWalletTx *, unsigned int) pcoin : setCoins)
                {
                    CAmount nCredit = pcoin.first->vout[pcoin.second].nValue;
                    // The coin age after the next block (depth+1) is used instead of the current,
                    // reflecting an assumption the user would accept a bit more delay for
                    // a chance at a free transaction.
                    // But mempool inputs might still be in the mempool, so their age stays 0
                    int age = pcoin.first->GetDepthInMainChain();
                    assert(age >= 0);
                    if (age != 0)
                        age += 1;
                    dPriority += (double)nCredit * age;
                }

                const CAmount nChange = nValueIn - nValueToSelect;
                if (nChange > 0)
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptChange;

                    // coin control: send change to custom address
                    if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                        scriptChange = GetScriptForDestination(coinControl->destChange);

                    // no coin control: send change to newly generated address
                    else
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        bool ret;
                        ret = reservekey.GetReservedKey(vchPubKey);
                        if (!ret)
                        {
                            strFailReason = _("Keypool ran out, please call keypoolrefill first");
                            return false;
                        }

                        scriptChange = GetScriptForDestination(vchPubKey.GetID());
                    }

                    CTxOut newTxOut(nChange, scriptChange);

                    // We do not move dust-change to fees, because the sender would end up paying more than requested.
                    // This would be against the purpose of the all-inclusive feature.
                    // So instead we raise the change and deduct from the recipient.
                    if (nSubtractFeeFromAmount > 0 && newTxOut.IsDust())
                    {
                        CAmount nDust = newTxOut.GetDustThreshold() - newTxOut.nValue;
                        newTxOut.nValue += nDust; // raise change until no more dust
                        for (unsigned int i = 0; i < vecSend.size(); i++) // subtract from first recipient
                        {
                            if (vecSend[i].fSubtractFeeFromAmount)
                            {
                                txNew.vout[i].nValue -= nDust;
                                if (txNew.vout[i].IsDust())
                                {
                                    strFailReason = _(
                                        "The transaction amount is too small to send after the fee has been deducted");
                                    return false;
                                }
                                break;
                            }
                        }
                    }

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    if (newTxOut.IsDust())
                    {
                        nFeeRet += nChange;
                        reservekey.ReturnKey();
                    }
                    else
                    {
                        if (!involvesPublicLabel)
                        {
                            // Insert change txn at random position:
                            nChangePosRet = GetRandInt(txNew.vout.size() + 1);
                            vector<CTxOut>::iterator position = txNew.vout.begin() + nChangePosRet;
                            txNew.vout.insert(position, newTxOut);
                        }
                        else
                            // Insert change at end position because original txout order is critical for public label
                            txNew.vout.insert(txNew.vout.end(), newTxOut);
                    }
                }
                else
                    reservekey.ReturnKey();

                // Fill vin
                //
                // Note how the sequence number is set to max()-1 so that the
                // nLockTime set above actually works.
                for (const PAIRTYPE(const CWalletTx *, unsigned int) & coin : setCoins)
                {
                    txNew.vin.push_back(CTxIn(
                        coin.first->GetHash(), coin.second, CScript(), std::numeric_limits<unsigned int>::max() - 1));

                    // If the input is a Freeze CLTV lock-by-blocktime then update the txNew.nLockTime
                    CScriptNum nFreezeLockTime(0);
                    if (isFreezeCLTV(*this, coin.first->vout[coin.second].scriptPubKey, nFreezeLockTime))
                    {
                        if (nFreezeLockTime.getint64() > LOCKTIME_THRESHOLD)
                            txNew.nLockTime = chainActive.Tip()->GetMedianTimePast();
                    }
                }

                // Sign
                unsigned int sighashType = SIGHASH_ALL;
                if (IsUAHFforkActiveOnNextBlock(chainActive.Tip()->nHeight) && walletSignWithForkSig.value)
                {
                    sighashType |= SIGHASH_FORKID;
                }
                int nIn = 0;
                CTransaction txNewConst(txNew);
                for (const PAIRTYPE(const CWalletTx *, unsigned int) & coin : setCoins)
                {
                    bool signSuccess;
                    const CScript &scriptPubKey = coin.first->vout[coin.second].scriptPubKey;
                    CAmount amountIn = coin.first->vout[coin.second].nValue;
                    CScript &scriptSigRes = txNew.vin[nIn].scriptSig;
                    if (sign)
                    {
                        signSuccess =
                            ProduceSignature(TransactionSignatureCreator(this, &txNewConst, nIn, amountIn, sighashType),
                                scriptPubKey, scriptSigRes);
                    }
                    else
                        signSuccess = ProduceSignature(DummySignatureCreator(this), scriptPubKey, scriptSigRes);

                    if (!signSuccess)
                    {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    }
                    nIn++;
                }

                unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);

                // Remove scriptSigs if we used dummy signatures for fee calculation
                if (!sign)
                {
                    for (CTxIn &vin : txNew.vin)
                    {
                        vin.scriptSig = CScript();
                    }
                }

                // Embed the constructed transaction data in wtxNew.
                *static_cast<CTransaction *>(&wtxNew) = CTransaction(txNew);

                // Limit size
                if (nBytes >= MAX_STANDARD_TX_SIZE)
                {
                    strFailReason = _("Transaction too large");
                    return false;
                }

                dPriority = wtxNew.ComputePriority(dPriority, nBytes);

                // Can we complete this as a free transaction?
                if (fSendFreeTransactions && nBytes <= MAX_STANDARD_TX_SIZE &&
                    GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY))
                {
                    // Not enough fee: enough priority?
                    double dPriorityNeeded = mempool.estimateSmartPriority(nTxConfirmTarget);
                    // Require at least hard-coded AllowFree.
                    if (dPriority >= dPriorityNeeded && AllowFree(dPriority))
                        break;
                }
                if (fSendFreeTransactions && AreFreeTxnsDisallowed())
                {
                    strFailReason =
                        _("You can not send free transactions if you have configured a -limitfreerelay of zero");
                    return false;
                }


                CAmount nFeeNeeded = GetMinimumFee(nBytes, nTxConfirmTarget, mempool);
                if (coinControl && nFeeNeeded > 0 && coinControl->nMinimumTotalFee > nFeeNeeded)
                {
                    nFeeNeeded = coinControl->nMinimumTotalFee;
                }

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes))
                {
                    strFailReason = _("Transaction too large for fee policy");
                    return false;
                }

                if (nFeeRet >= nFeeNeeded)
                    break; // Done, enough fee included.

                // Include more fee and try again.
                nFeeRet = nFeeNeeded;
                continue;
            }
        }
    }

    return true;
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx &wtxNew, CReserveKey &reservekey)
{
    {
        LOCK2(cs_main, cs_wallet);
        LOGA("CommitTransaction:\n%s", wtxNew.ToString());

#if 1
        if (fBroadcastTransactions)
        {
            // Broadcast
            if (!wtxNew.AcceptToMemoryPool(AreFreeTxnsDisallowed()))
            {
                // This must not fail. The transaction has already been signed and recorded.
                LOGA("CommitTransaction(): Error: Transaction not valid\n");
                return false;
            }
        }
#endif

        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB *pwalletdb = fFileBacked ? new CWalletDB(strWalletFile, "r+") : nullptr;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew, false, pwalletdb);

            // Notify that old coins are spent
            set<CWalletTx *> setCoins;
            for (const CTxIn &txin : wtxNew.vin)
            {
                CWalletTx &coin = mapWallet[txin.prevout.hash];
                coin.BindWallet(this);
                NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Track how many getdata requests our transaction gets
        mapRequestCount[wtxNew.GetHash()] = 0;

        if (fBroadcastTransactions)
        {
#if 0
            // Broadcast
            if (!wtxNew.AcceptToMemoryPool(false))
            {
                // This must not fail. The transaction has already been signed and recorded.
                LOGA("CommitTransaction(): Error: Transaction not valid\n");
                return false;
            }
#else
            SyncWithWallets(MakeTransactionRef(wtxNew), nullptr, -1);
#endif
            wtxNew.RelayWalletTransaction();
        }
    }
    return true;
}

bool CWallet::AddAccountingEntry(const CAccountingEntry &acentry, CWalletDB &pwalletdb)
{
    if (!pwalletdb.WriteAccountingEntry_Backend(acentry))
        return false;

    laccentries.push_back(acentry);
    CAccountingEntry &entry = laccentries.back();
    wtxOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx *)0, &entry)));

    return true;
}

CAmount CWallet::GetRequiredFee(unsigned int nTxBytes)
{
    return std::max(minTxFee.GetFee(nTxBytes), ::minRelayTxFee.GetFee(nTxBytes));
}

CAmount CWallet::GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool &pool)
{
    // payTxFee is user-set "I want to pay this much"
    CAmount nFeeNeeded = payTxFee.GetFee(nTxBytes);
    // User didn't set: use -txconfirmtarget to estimate...
    if (nFeeNeeded == 0)
    {
        int estimateFoundTarget = nConfirmTarget;
        nFeeNeeded = pool.estimateSmartFee(nConfirmTarget, &estimateFoundTarget).GetFee(nTxBytes);
        // ... unless we don't have enough mempool data for estimatefee, then use fallbackFee
        if (nFeeNeeded == 0)
            nFeeNeeded = fallbackFee.GetFee(nTxBytes);
    }
    // prevent user from paying a fee below minRelayTxFee or minTxFee
    nFeeNeeded = std::max(nFeeNeeded, GetRequiredFee(nTxBytes));
    // But always obey the maximum
    if (nFeeNeeded > maxTxFee.value)
        nFeeNeeded = maxTxFee.value;
    return nFeeNeeded;
}


DBErrors CWallet::LoadWallet(bool &fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile, "cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    uiInterface.LoadWallet(this);

    return DB_LOAD_OK;
}

DBErrors CWallet::ZapSelectTx(vector<uint256> &vHashIn, vector<uint256> &vHashOut)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nZapSelectTxRet = CWalletDB(strWalletFile, "cr+").ZapSelectTx(this, vHashIn, vHashOut);
    if (nZapSelectTxRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapSelectTxRet != DB_LOAD_OK)
        return nZapSelectTxRet;

    MarkDirty();

    return DB_LOAD_OK;
}

DBErrors CWallet::ZapWalletTx(std::vector<CWalletTx> &vWtx)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nZapWalletTxRet = CWalletDB(strWalletFile, "cr+").ZapWalletTx(this, vWtx);
    if (nZapWalletTxRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapWalletTxRet != DB_LOAD_OK)
        return nZapWalletTxRet;

    return DB_LOAD_OK;
}


bool CWallet::SetAddressBook(const CTxDestination &address, const string &strName, const string &strPurpose)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    // double negative means it IS mine
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address, chainActive.Tip()) != ISMINE_NO,
        strPurpose, (fUpdated ? CT_UPDATED : CT_NEW));
    if (!fFileBacked)
        return false;

    if (!strPurpose.empty() && !CWalletDB(strWalletFile).WritePurpose(address, strPurpose))
    {
        return false;
    }

    return CWalletDB(strWalletFile).WriteName(address, strName);
}

bool CWallet::DelAddressBook(const CTxDestination &address)
{
    {
        LOCK(cs_wallet); // mapAddressBook

        if (fFileBacked)
        {
            // Delete destdata tuples associated with address.
            for (const std::pair<std::string, std::string> &item : mapAddressBook[address].destdata)
            {
                CWalletDB(strWalletFile).EraseDestData(address, item.first);
            }
        }
        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(
        this, address, "", ::IsMine(*this, address, chainActive.Tip()) != ISMINE_NO, "", CT_DELETED);

    if (!fFileBacked)
        return false;

    CWalletDB(strWalletFile).ErasePurpose(address);
    return CWalletDB(strWalletFile).EraseName(address);
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
    if (fFileBacked)
    {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    LOCK(cs_wallet);
    CWalletDB walletdb(strWalletFile);
    for (int64_t nIndex : setKeyPool)
    {
        walletdb.ErasePool(nIndex);
    }
    setKeyPool.clear();

    if (IsLocked())
        return false;

    int64_t nKeys = max(GetArg("-keypool", DEFAULT_KEYPOOL_SIZE), (int64_t)0);
    for (int i = 0; i < nKeys; i++)
    {
        int64_t nIndex = i + 1;
        walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
        setKeyPool.insert(nIndex);
    }
    LOGA("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    LOCK(cs_wallet);

    if (IsLocked())
        return false;

    CWalletDB walletdb(strWalletFile);

    // Top up key pool
    unsigned int nTargetSize;
    if (kpSize > 0)
        nTargetSize = kpSize;
    else
        nTargetSize = max(GetArg("-keypool", DEFAULT_KEYPOOL_SIZE), (int64_t)0);

    while (setKeyPool.size() < (nTargetSize + 1))
    {
        int64_t nEnd = 1;
        if (!setKeyPool.empty())
            nEnd = *(--setKeyPool.end()) + 1;
        if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
            throw runtime_error("TopUpKeyPool(): writing generated key failed");
        setKeyPool.insert(nEnd);
        LOGA("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t &nIndex, CKeyPool &keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked())
            TopUpKeyPool();

        // Get the oldest key
        if (setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());
        if (!walletdb.ReadPool(nIndex, keypool))
            throw runtime_error("ReserveKeyFromKeyPool(): read failed");
        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool(): unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        LOG(DBASE, "keypool reserve %d\n", nIndex);
    }
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked)
    {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LOG(DBASE, "keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    LOG(DBASE, "keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey &result)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1)
        {
            if (IsLocked())
                return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances()
{
    map<CTxDestination, CAmount> balances;

    {
        LOCK(cs_wallet);
        for (PAIRTYPE(uint256, CWalletTx) walletEntry : mapWallet)
        {
            CWalletTx *pcoin = &walletEntry.second;

            if (!CheckFinalTx(*pcoin) || !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->vout[i]))
                    continue;
                if (!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                CAmount n = IsSpent(walletEntry.first, i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

set<set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    set<set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    for (PAIRTYPE(uint256, CWalletTx) walletEntry : mapWallet)
    {
        CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            for (CTxIn txin : pcoin->vin)
            {
                CTxDestination address;
                if (!IsMine(txin)) /* If this input isn't mine, ignore it */
                    continue;
                if (!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
                for (CTxOut txout : pcoin->vout)
                {
                    if (IsChange(txout))
                    {
                        CTxDestination txoutAddr;
                        if (!ExtractDestination(txout.scriptPubKey, txoutAddr))
                            continue;
                        grouping.insert(txoutAddr);
                    }
                }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
        {
            if (IsMine(pcoin->vout[i]))
            {
                CTxDestination address;
                if (!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
        }
    }

    set<set<CTxDestination> *> uniqueGroupings; // a set of pointers to groups of addresses
    map<CTxDestination, set<CTxDestination> *> setmap; // map addresses to the unique group containing it
    for (set<CTxDestination> grouping2 : groupings)
    {
        // make a set of all the groups hit by this new group
        set<set<CTxDestination> *> hits;
        map<CTxDestination, set<CTxDestination> *>::iterator it;
        for (CTxDestination address : grouping2)
        {
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);
        }

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination> *merged = new set<CTxDestination>(grouping2);
        for (set<CTxDestination> *hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for (CTxDestination element : *merged)
        {
            setmap[element] = merged;
        }
    }

    set<set<CTxDestination> > ret;
    for (set<CTxDestination> *uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

std::set<CTxDestination> CWallet::GetAccountAddresses(const std::string &strAccount) const
{
    LOCK(cs_wallet);
    set<CTxDestination> result;
    for (const PAIRTYPE(CTxDestination, CAddressBookData) & item : mapAddressBook)
    {
        const CTxDestination &address = item.first;
        const string &strName = item.second.name;
        if (strName == strAccount)
            result.insert(address);
    }
    return result;
}

bool CReserveKey::GetReservedKey(CPubKey &pubkey)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else
        {
            return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID> &setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    for (const int64_t &id : setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw runtime_error("GetAllReserveKeyHashes(): read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes(): unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::UpdatedTransaction(const uint256 &hashTx)
{
    LOCK(cs_wallet);
    // Only notify UI if this transaction is in this wallet
    map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(hashTx);
    if (mi != mapWallet.end())
        NotifyTransactionChanged(this, hashTx, CT_UPDATED);
}

void CWallet::GetScriptForMining(boost::shared_ptr<CReserveScript> &script)
{
    boost::shared_ptr<CReserveKey> rKey(new CReserveKey(this));
    CPubKey pubkey;
    if (!rKey->GetReservedKey(pubkey))
        return;

    script = rKey;
    script->reserveScript = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
}

void CWallet::LockCoin(COutPoint &output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint &output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint> &vOutpts)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin(); it != setLockedCoins.end(); it++)
    {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}

/** @} */ // end of Actions

class CAffectedKeysVisitor : public boost::static_visitor<void>
{
private:
    const CKeyStore &keystore;
    std::vector<CKeyID> &vKeys;

public:
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn)
        : keystore(keystoreIn), vKeys(vKeysIn)
    {
    }

    void Process(const CScript &script)
    {
        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired))
        {
            for (const CTxDestination &dest : vDest)
            {
                boost::apply_visitor(*this, dest);
            }
        }
    }

    void operator()(const CKeyID &keyId)
    {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID &scriptId)
    {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CNoDestination &none) {}
};

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    // the tip can be reorganised; use a 144-block safety margin
    CBlockIndex *pindexMax = chainActive[std::max(0, chainActive.Height() - 144)];
    std::map<CKeyID, CBlockIndex *> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    for (const CKeyID &keyid : setKeys)
    {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++)
    {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = (*it).second;
        BlockMap::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && chainActive.Contains(blit->second))
        {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            for (const CTxOut &txout : wtx.vout)
            {
                // iterate over all their outputs
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                for (const CKeyID &keyid : vAffected)
                {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex *>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (std::map<CKeyID, CBlockIndex *>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end();
         it++)
        mapKeyBirth[it->first] = it->second->GetBlockTime() - 7200; // block times can be 2h off
}

bool CWallet::AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    if (boost::get<CNoDestination>(&dest))
        return false;

    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    if (!fFileBacked)
        return true;

    return CWalletDB(strWalletFile).WriteDestData(dest, key, value);
}

bool CWallet::EraseDestData(const CTxDestination &dest, const std::string &key)
{
    if (!mapAddressBook[dest].destdata.erase(key))
        return false;
    if (!fFileBacked)
        return true;

    return CWalletDB(strWalletFile).EraseDestData(dest, key);
}

bool CWallet::LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    return true;
}

bool CWallet::GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const
{
    std::map<CTxDestination, CAddressBookData>::const_iterator i = mapAddressBook.find(dest);
    if (i != mapAddressBook.end())
    {
        CAddressBookData::StringMap::const_iterator j = i->second.destdata.find(key);
        if (j != i->second.destdata.end())
        {
            if (value)
                *value = j->second;
            return true;
        }
    }
    return false;
}

bool CWallet::InitLoadWallet()
{
    std::string walletFile = GetArg("-wallet", DEFAULT_WALLET_DAT);

    // needed to restore wallet transaction meta data after -zapwallettxes
    std::vector<CWalletTx> vWtx;

    if (GetBoolArg("-zapwallettxes", false))
    {
        uiInterface.InitMessage(_("Zapping all transactions from wallet..."));

        CWallet *tempWallet = new CWallet(walletFile);
        DBErrors nZapWalletRet = tempWallet->ZapWalletTx(vWtx);
        if (nZapWalletRet != DB_LOAD_OK)
        {
            return InitError(strprintf(_("Error loading %s: Wallet corrupted"), walletFile));
        }

        delete tempWallet;
        tempWallet = nullptr;
    }

    uiInterface.InitMessage(_("Loading wallet..."));

    int64_t nStart = GetTimeMillis();
    bool fFirstRun = true;
    CWallet *walletInstance = new CWallet(walletFile);
    DBErrors nLoadWalletRet = walletInstance->LoadWallet(fFirstRun);
    if (nLoadWalletRet != DB_LOAD_OK)
    {
        if (nLoadWalletRet == DB_CORRUPT)
            return InitError(strprintf(_("Error loading %s: Wallet corrupted"), walletFile));
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
        {
            InitWarning(strprintf(_("Error reading %s! All keys read correctly, but transaction data"
                                    " or address book entries might be missing or incorrect."),
                walletFile));
        }
        else if (nLoadWalletRet == DB_TOO_NEW)
            return InitError(
                strprintf(_("Error loading %s: Wallet requires newer version of %s"), walletFile, _(PACKAGE_NAME)));
        else if (nLoadWalletRet == DB_NEED_REWRITE)
        {
            return InitError(strprintf(_("Wallet needed to be rewritten: restart %s to complete"), _(PACKAGE_NAME)));
        }
        else
            return InitError(strprintf(_("Error loading %s"), walletFile));
    }

    if (GetBoolArg("-upgradewallet", fFirstRun))
    {
        int nMaxVersion = GetArg("-upgradewallet", 0);
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            LOGA("Performing wallet upgrade to %i\n", FEATURE_LATEST);
            nMaxVersion = CLIENT_VERSION;
            walletInstance->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        }
        else
            LOGA("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < walletInstance->GetVersion())
        {
            return InitError(_("Cannot downgrade wallet"));
        }
        walletInstance->SetMaxVersion(nMaxVersion);
    }

    if (fFirstRun)
    {
        // Create new keyUser and set as default key
        RandAddSeedPerfmon();

        if (GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET) && !walletInstance->IsHDEnabled())
        {
            // generate a new master key
            CKey key;
            CPubKey masterPubKey = walletInstance->GenerateNewHDMasterKey();
            if (!walletInstance->SetHDMasterKey(masterPubKey))
                throw std::runtime_error("CWallet::GenerateNewKey(): Storing master key failed");

            // ensure this wallet.dat can only be opened by clients supporting HD
            walletInstance->SetMinVersion(FEATURE_HD);
        }
        CPubKey newDefaultKey;
        if (walletInstance->GetKeyFromPool(newDefaultKey))
        {
            walletInstance->SetDefaultKey(newDefaultKey);
            if (!walletInstance->SetAddressBook(walletInstance->vchDefaultKey.GetID(), "", "receive"))
                return InitError(_("Cannot write default address") += "\n");
        }

        walletInstance->SetBestChain(chainActive.GetLocator());
    }
    else if (mapArgs.count("-usehd"))
    {
        bool useHD = GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET);
        if (walletInstance->IsHDEnabled() && !useHD)
            return InitError(
                strprintf(_("Error loading %s: You can't disable HD on a already existing HD wallet"), walletFile));
        if (!walletInstance->IsHDEnabled() && useHD)
            return InitError(
                strprintf(_("Error loading %s: You can't enable HD on a already existing non-HD wallet"), walletFile));
    }

    LOGA(" wallet      %15dms\n", GetTimeMillis() - nStart);

    RegisterValidationInterface(walletInstance);

    CBlockIndex *pindexRescan = nullptr;
    if (GetBoolArg("-rescan", false))
        pindexRescan = chainActive.Genesis();
    else
    {
        CWalletDB walletdb(walletFile);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = FindForkInGlobalIndex(chainActive, locator);
        else
            pindexRescan = chainActive.Genesis();
    }
    if (chainActive.Tip() && chainActive.Tip() != pindexRescan)
    {
        // We can't rescan beyond non-pruned blocks, stop and throw an error
        // this might happen if a user uses a old wallet within a pruned node
        // or if he ran -disablewallet for a longer time, then decided to re-enable
        if (fPruneMode)
        {
            CBlockIndex *block = chainActive.Tip();
            while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA) && block->pprev->nTx > 0 &&
                   pindexRescan != block)
                block = block->pprev;

            if (pindexRescan != block)
                return InitError(_("Prune: last wallet synchronisation goes beyond pruned data. You need to -reindex "
                                   "(download the whole blockchain again in case of pruned node)"));
        }

        uiInterface.InitMessage(_("Rescanning..."));
        LOGA("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight,
            pindexRescan->nHeight);
        nStart = GetTimeMillis();
        walletInstance->ScanForWalletTransactions(pindexRescan, true);
        LOGA(" rescan      %15dms\n", GetTimeMillis() - nStart);
        walletInstance->SetBestChain(chainActive.GetLocator());
        nWalletDBUpdated++;

        // Restore wallet transaction metadata after -zapwallettxes=1
        if (GetBoolArg("-zapwallettxes", false) && GetArg("-zapwallettxes", "1") != "2")
        {
            CWalletDB walletdb(walletFile);

            for (const CWalletTx &wtxOld : vWtx)
            {
                uint256 hash = wtxOld.GetHash();
                std::map<uint256, CWalletTx>::iterator mi = walletInstance->mapWallet.find(hash);
                if (mi != walletInstance->mapWallet.end())
                {
                    const CWalletTx *copyFrom = &wtxOld;
                    CWalletTx *copyTo = &mi->second;
                    copyTo->mapValue = copyFrom->mapValue;
                    copyTo->vOrderForm = copyFrom->vOrderForm;
                    copyTo->nTimeReceived = copyFrom->nTimeReceived;
                    copyTo->nTimeSmart = copyFrom->nTimeSmart;
                    copyTo->fFromMe = copyFrom->fFromMe;
                    copyTo->strFromAccount = copyFrom->strFromAccount;
                    copyTo->nOrderPos = copyFrom->nOrderPos;
                    copyTo->WriteToDisk(&walletdb);
                }
            }
        }
    }
    walletInstance->SetBroadcastTransactions(GetBoolArg("-walletbroadcast", DEFAULT_WALLETBROADCAST));

    pwalletMain = walletInstance;
    return true;
}

bool CWallet::ParameterInteraction()
{
    if (mapArgs.count("-mintxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-mintxfee"], n) && n > 0)
            CWallet::minTxFee = CFeeRate(n);
        else
            return InitError(AmountErrMsg("mintxfee", mapArgs["-mintxfee"]));
    }
    if (mapArgs.count("-fallbackfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(mapArgs["-fallbackfee"], nFeePerK))
            return InitError(strprintf(_("Invalid amount for -fallbackfee=<amount>: '%s'"), mapArgs["-fallbackfee"]));
        if (nFeePerK > HIGH_TX_FEE_PER_KB)
            InitWarning(_("-fallbackfee is set very high! This is the transaction fee you may pay when fee estimates "
                          "are not available."));
        CWallet::fallbackFee = CFeeRate(nFeePerK);
    }
    if (mapArgs.count("-paytxfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(mapArgs["-paytxfee"], nFeePerK))
            return InitError(AmountErrMsg("paytxfee", mapArgs["-paytxfee"]));
        if (nFeePerK > HIGH_TX_FEE_PER_KB)
            InitWarning(
                _("-paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
        payTxFee = CFeeRate(nFeePerK, 1000);
        if (payTxFee < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                mapArgs["-paytxfee"], ::minRelayTxFee.ToString()));
        }
    }
    if (mapArgs.count("-maxtxfee"))
    {
        CAmount nMaxFee = 0;
        if (!ParseMoney(mapArgs["-maxtxfee"], nMaxFee))
            return InitError(AmountErrMsg("maxtxfee", mapArgs["-maxtxfee"]));
        if (nMaxFee > HIGH_MAX_TX_FEE)
            InitWarning(_("-maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        maxTxFee.value = nMaxFee;
        if (CFeeRate(maxTxFee.value, 1000) < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay "
                                         "fee of %s to prevent stuck transactions)"),
                mapArgs["-maxtxfee"], ::minRelayTxFee.ToString()));
        }
    }
    nTxConfirmTarget = GetArg("-txconfirmtarget", DEFAULT_TX_CONFIRM_TARGET);
    bSpendZeroConfChange = GetBoolArg("-spendzeroconfchange", DEFAULT_SPEND_ZEROCONF_CHANGE);
    fSendFreeTransactions = GetBoolArg("-sendfreetransactions", DEFAULT_SEND_FREE_TRANSACTIONS);

    return true;
}

CKeyPool::CKeyPool() { nTime = GetTime(); }
CKeyPool::CKeyPool(const CPubKey &vchPubKeyIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
}

CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

int CMerkleTx::SetMerkleBranch(const CBlock &block, int txIdx)
{
    AssertLockHeld(cs_main);
    // if a bad txIdx is passed, then in release builds set the tx index to "I don't know". In debug builds assert.
    DbgAssert(txIdx >= -1, txIdx = -1);
    CBlock blockTmp;

    // Update the tx's hashBlock
    hashBlock = block.GetHash();

    if (txIdx != -1)
    {
        nIndex = txIdx;
    }
    else
    {
        // Locate the transaction
        nIndex = block.find(((CTransactionRef) this)->GetHash());
        if (nIndex == -1)
        {
            LOGA("ERROR: SetMerkleBranch(): couldn't find tx in block\n");
            return 0;
        }
    }

    // Is the tx in a block that's in the main chain
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    const CBlockIndex *pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(const CBlockIndex *&pindexRet) const
{
    if (hashUnset())
        return 0;

    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex *pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    pindexRet = pindex;
    return ((nIndex == -1) ? (-1) : 1) * (chainActive.Height() - pindex->nHeight + 1);
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!IsCoinBase())
        return 0;
    return max(0, (COINBASE_MATURITY + 1) - GetDepthInMainChain());
}


bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectAbsurdFee)
{
    CValidationState state;
    return ::AcceptToMemoryPool(
        mempool, state, MakeTransactionRef(*this), fLimitFree, nullptr, false, fRejectAbsurdFee);
}


void ThreadRescan()
{
    pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
    pwalletMain->ReacceptWalletTransactions();
    pwalletMain->Flush();
    statusStrings.Clear("rescanning");
}

void StartWalletRescanThread()
{
    statusStrings.Set("rescanning");
    boost::thread rescanThread(boost::bind(&TraceThread<void (*)()>, "rescan", &ThreadRescan));
    rescanThread.detach();
}
