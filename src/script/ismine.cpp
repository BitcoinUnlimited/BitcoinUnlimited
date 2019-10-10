// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ismine.h"

#include "core_io.h" // freeze debug only

#include "key.h"
#include "keystore.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/standard.h"


using namespace std;

typedef vector<unsigned char> valtype;

unsigned int HaveKeys(const vector<valtype> &pubkeys, const CKeyStore &keystore)
{
    unsigned int nResult = 0;
    for (const valtype &pubkey : pubkeys)
    {
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (keystore.HaveKey(keyID))
            ++nResult;
    }
    return nResult;
}

/*
 * Return the OP_RETURN data associated with a script.
 * Called from AddressTableModel::labelForAddress()
 *
 */
std::string getLabelPublic(const CScript &scriptPubKey)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    if (Solver(scriptPubKey, whichType, vSolutions))
    {
        if (whichType == TX_LABELPUBLIC)
        {
            CScript labelPublic(vSolutions[1]);

            valtype data;
            opcodetype opcode;
            CScript::const_iterator s = labelPublic.begin();
            labelPublic.GetOp(s, opcode, data);

            return std::string(data.begin(), data.end());
        }
    }

    return "";
}


bool isFreezeCLTV(const CKeyStore &keystore, const CScript &scriptPubKey, CScriptNum &nFreezeLockTime)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    if (Solver(scriptPubKey, whichType, vSolutions))
    {
        if (whichType == TX_SCRIPTHASH)
        {
            CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
            CScript subscript;
            if (keystore.GetCScript(scriptID, subscript))
                Solver(subscript, whichType, vSolutions);
        }

        if (whichType == TX_CLTV)
        {
            CScriptNum sn(vSolutions[0], true, 5);
            nFreezeLockTime = sn;
            return true;
        }
    }
    return false;
}


static isminetype IsMine(const CKeyStore &keystore,
    const CScript &scriptPubKey,
    CBlockIndex *bestBlock,
    bool alreadyLocked)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
    {
        if (keystore.HaveWatchOnly(scriptPubKey))
            return ISMINE_WATCH_UNSOLVABLE;
        return ISMINE_NO;
    }

    CKeyID keyID;
    switch (whichType)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
    case TX_LABELPUBLIC:
        break;
    case TX_PUBKEY:
    {
        keyID = CPubKey(vSolutions[0]).GetID();
        bool haveKey = alreadyLocked ? keystore._HaveKey(keyID) : keystore.HaveKey(keyID);
        if (haveKey)
            return ISMINE_SPENDABLE;
    }
    break;
    case TX_PUBKEYHASH:
    {
        keyID = CKeyID(uint160(vSolutions[0]));
        bool haveKey = alreadyLocked ? keystore._HaveKey(keyID) : keystore.HaveKey(keyID);
        if (haveKey)
            return ISMINE_SPENDABLE;
    }
    break;
    case TX_SCRIPTHASH:
    {
        CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
        CScript subscript;
        if (keystore.GetCScript(scriptID, subscript))
        {
            isminetype ret = IsMine(keystore, subscript, bestBlock);
            // FIXME do not always log, use a  specific debug category or create one if no others fit
            LOGA("Freeze SUBSCRIPT = %s! **** MINE=%d  *****  \n", ::ScriptToAsmStr(subscript), ret);
            // if (ret == ISMINE_SPENDABLE) TODO Don't understand why this line was required. Had to comment it so all
            // minetypes in subscripts (eg CLTV) are recognizable
            return ret;
        }
        break;
    }
    case TX_MULTISIG:
    {
        // Only consider transactions "mine" if we own ALL the
        // keys involved. Multi-signature transactions that are
        // partially owned (somebody else has a key that can spend
        // them) enable spend-out-from-under-you attacks, especially
        // in shared-wallet situations.
        vector<valtype> keys(vSolutions.begin() + 1, vSolutions.begin() + vSolutions.size() - 1);
        if (HaveKeys(keys, keystore) == keys.size())
            return ISMINE_SPENDABLE;
        break;
    }
    case TX_CLTV:
    {
        keyID = CPubKey(vSolutions[1]).GetID();
        bool haveKey = alreadyLocked ? keystore._HaveKey(keyID) : keystore.HaveKey(keyID);
        if (haveKey)
        {
            CScriptNum nFreezeLockTime(vSolutions[0], true, 5);

            // FIXME do not always log, use a  specific debug category or create one if no others fit
            LOGA("Found Freeze Have Key. nFreezeLockTime=%d. BestBlockHeight=%d \n", nFreezeLockTime.getint64(),
                bestBlock->nHeight);
            if (nFreezeLockTime < LOCKTIME_THRESHOLD)
            {
                // locktime is a block
                if (nFreezeLockTime > bestBlock->nHeight)
                    return ISMINE_WATCH_SOLVABLE;
                else
                    return ISMINE_SPENDABLE;
            }
            else
            {
                // locktime is a time
                if (nFreezeLockTime > bestBlock->GetMedianTimePast())
                    return ISMINE_WATCH_SOLVABLE;
                else
                    return ISMINE_SPENDABLE;
            }
        }
        else
        {
            LOGA("Found Freeze DONT HAVE KEY!! \n");
            return ISMINE_NO;
        }
    }
    } // switch

    if (keystore.HaveWatchOnly(scriptPubKey))
    {
        // TODO: This could be optimized some by doing some work after the above solver
        CScript scriptSig;
        return ProduceSignature(DummySignatureCreator(&keystore), scriptPubKey, scriptSig) ? ISMINE_WATCH_SOLVABLE :
                                                                                             ISMINE_WATCH_UNSOLVABLE;
    }
    return ISMINE_NO;
}


isminetype IsMine(const CKeyStore &keystore, const CTxDestination &dest, CBlockIndex *bestBlock)
{
    CScript script = GetScriptForDestination(dest);
    return IsMine(keystore, script, bestBlock);
}

isminetype IsMine(const CKeyStore &keystore, const CScript &scriptPubKey, CBlockIndex *bestBlock)
{
    return IsMine(keystore, scriptPubKey, bestBlock, false);
}

isminetype _IsMine(const CKeyStore &keystore, const CScript &scriptPubKey, CBlockIndex *bestBlock)
{
    AssertLockHeld(keystore.cs_KeyStore);
    return IsMine(keystore, scriptPubKey, bestBlock, true);
}
