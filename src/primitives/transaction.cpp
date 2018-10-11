// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"

#include "hash.h"
#include "policy/policy.h"
#include "tinyformat.h"
#include "utilstrencodings.h"


std::string COutPoint::ToString() const { return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0, 10), n); }
CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != SEQUENCE_FINAL)
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CTxOut::CTxOut(const CAmount &nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

uint256 CTxOut::GetHash() const { return SerializeHash(*this); }
std::string CTxOut::ToString() const
{
    return strprintf(
        "CTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction &tx)
    : nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime)
{
}

uint256 CMutableTransaction::GetHash() const { return SerializeHash(*this); }
void CTransaction::UpdateHash() const { *const_cast<uint256 *>(&hash) = SerializeHash(*this); }
CTransaction::CTransaction() : nTxSize(0), nVersion(CTransaction::CURRENT_VERSION), vin(), vout(), nLockTime(0) {}
CTransaction::CTransaction(const CMutableTransaction &tx)
    : nTxSize(0), nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime)
{
    UpdateHash();
}

CTransaction::CTransaction(CMutableTransaction &&tx)
    : nTxSize(0), nVersion(tx.nVersion), vin(std::move(tx.vin)), vout(std::move(tx.vout)), nLockTime(tx.nLockTime)
{
    UpdateHash();
}

CTransaction::CTransaction(const CTransaction &tx)
    : nTxSize(tx.nTxSize.load()), nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime)
{
    UpdateHash();
};

CTransaction &CTransaction::operator=(const CTransaction &tx)
{
    nTxSize.store(tx.nTxSize);
    *const_cast<int *>(&nVersion) = tx.nVersion;
    *const_cast<std::vector<CTxIn> *>(&vin) = tx.vin;
    *const_cast<std::vector<CTxOut> *>(&vout) = tx.vout;
    *const_cast<unsigned int *>(&nLockTime) = tx.nLockTime;
    *const_cast<uint256 *>(&hash) = tx.hash;
    return *this;
}

bool CTransaction::IsEquivalentTo(const CTransaction &tx) const
{
    CMutableTransaction tx1 = *this;
    CMutableTransaction tx2 = tx;
    for (unsigned int i = 0; i < tx1.vin.size(); i++)
        tx1.vin[i].scriptSig = CScript();
    for (unsigned int i = 0; i < tx2.vin.size(); i++)
        tx2.vin[i].scriptSig = CScript();
    return CTransaction(tx1) == CTransaction(tx2);
}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (std::vector<CTxOut>::const_iterator it(vout.begin()); it != vout.end(); ++it)
    {
        nValueOut += it->nValue;
        if (!MoneyRange(it->nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
    }
    return nValueOut;
}

double CTransaction::ComputePriority(double dPriorityInputs, unsigned int nSize) const
{
    nSize = CalculateModifiedSize(nSize);
    if (nSize == 0)
        return 0.0;

    return dPriorityInputs / nSize;
}

unsigned int CTransaction::CalculateModifiedSize(unsigned int nSize) const
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later.
    if (nSize == 0)
        nSize = GetTxSize();
    for (std::vector<CTxIn>::const_iterator it(vin.begin()); it != vin.end(); ++it)
    {
        unsigned int offset = 41U + std::min(110U, (unsigned int)it->scriptSig.size());
        if (nSize > offset)
            nSize -= offset;
    }
    return nSize;
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        GetHash().ToString().substr(0, 10), nVersion, vin.size(), vout.size(), nLockTime);
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
    return str;
}

size_t CTransaction::GetTxSize() const
{
    if (nTxSize == 0)
        nTxSize = ::GetSerializeSize(*this, SER_NETWORK, CTransaction::CURRENT_VERSION);
    return nTxSize;
}
