// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "DoubleSpendProof.h"
#include "hashwrapper.h"
#include "main.h"
#include "pubkey.h"
#include "script/interpreter.h"
#include "txmempool.h"

#include <stdexcept>

namespace
{
enum ScriptType
{
    P2PKH
};

void getP2PKHSignature(const CScript &script, std::vector<uint8_t> &vchRet)
{
    auto scriptIter = script.begin();
    opcodetype type;
    script.GetOp(scriptIter, type, vchRet);
}

void hashTx(DoubleSpendProof::Spender &spender, const CTransaction &tx, int inputIndex)
{
    DbgAssert(!spender.pushData.empty(), return );
    DbgAssert(!spender.pushData.front().empty(), return );
    auto hashType = spender.pushData.front().back();
    if (!(hashType & SIGHASH_ANYONECANPAY))
    {
        CHashWriter ss(SER_GETHASH, 0);
        for (size_t n = 0; n < tx.vin.size(); ++n)
        {
            ss << tx.vin[n].prevout;
        }
        spender.hashPrevOutputs = ss.GetHash();
    }
    if (!(hashType & SIGHASH_ANYONECANPAY) && (hashType & 0x1f) != SIGHASH_SINGLE && (hashType & 0x1f) != SIGHASH_NONE)
    {
        CHashWriter ss(SER_GETHASH, 0);
        for (size_t n = 0; n < tx.vin.size(); ++n)
        {
            ss << tx.vin[n].nSequence;
        }
        spender.hashSequence = ss.GetHash();
    }
    if ((hashType & 0x1f) != SIGHASH_SINGLE && (hashType & 0x1f) != SIGHASH_NONE)
    {
        CHashWriter ss(SER_GETHASH, 0);
        for (size_t n = 0; n < tx.vout.size(); ++n)
        {
            ss << tx.vout[n];
        }
        spender.hashOutputs = ss.GetHash();
    }
    else if ((hashType & 0x1f) == SIGHASH_SINGLE && (size_t)inputIndex < tx.vout.size())
    {
        CHashWriter ss(SER_GETHASH, 0);
        ss << tx.vout[inputIndex];
        spender.hashOutputs = ss.GetHash();
    }
}

class DSPSignatureChecker : public BaseSignatureChecker
{
public:
    DSPSignatureChecker(const DoubleSpendProof *proof, const DoubleSpendProof::Spender &spender, int64_t amount)
        : m_proof(proof), m_spender(spender), m_amount(amount)
    {
    }

    bool CheckSig(const std::vector<uint8_t> &vchSigIn,
        const std::vector<uint8_t> &vchPubKey,
        const CScript &scriptCode) const override
    {
        CPubKey pubkey(vchPubKey);
        if (!pubkey.IsValid())
            return false;

        std::vector<uint8_t> vchSig(vchSigIn);
        if (vchSig.empty())
            return false;
        vchSig.pop_back(); // drop the hashtype byte tacked on to the end of the signature

        CHashWriter ss(SER_GETHASH, 0);
        ss << m_spender.txVersion << m_spender.hashPrevOutputs << m_spender.hashSequence;
        ss << COutPoint(m_proof->prevTxId(), m_proof->prevOutIndex());
        ss << static_cast<const CScriptBase &>(scriptCode);
        ss << m_amount << m_spender.outSequence << m_spender.hashOutputs;
        ss << m_spender.lockTime << (int)m_spender.pushData.front().back();
        const uint256 sighash = ss.GetHash();

        if (vchSig.size() == 64)
            return pubkey.VerifySchnorr(sighash, vchSig);
        return pubkey.VerifyECDSA(sighash, vchSig);
    }
    bool CheckLockTime(const CScriptNum &) const override { return true; }
    bool CheckSequence(const CScriptNum &) const override { return true; }
    const DoubleSpendProof *m_proof;
    const DoubleSpendProof::Spender &m_spender;
    const int64_t m_amount;
};
}

// static
DoubleSpendProof DoubleSpendProof::create(const CTransaction &t1, const CTransaction &t2)
{
    if (t1.GetHash() == t2.GetHash())
        throw std::runtime_error("Can not create dsproof from identical transactions");

    DoubleSpendProof answer;
    Spender &s1 = answer.m_spender1;
    Spender &s2 = answer.m_spender2;

    size_t inputIndex1 = 0;
    size_t inputIndex2 = 0;
    for (; inputIndex1 < t1.vin.size(); ++inputIndex1)
    {
        const CTxIn &in1 = t1.vin.at(inputIndex1);
        for (inputIndex2 = 0; inputIndex2 < t2.vin.size(); ++inputIndex2)
        {
            const CTxIn &in2 = t2.vin.at(inputIndex2);
            if (in1.prevout == in2.prevout)
            {
                answer.m_prevOutIndex = in1.prevout.n;
                answer.m_prevTxId = in1.prevout.hash;

                s1.outSequence = in1.nSequence;
                s2.outSequence = in2.nSequence;

                // TODO pass in the mempool and find the prev-tx we spend
                // then we can determine what script type we are dealing with and
                // be smarter about finding the signature.
                // Assume p2pkh for now.

                s1.pushData.resize(1);
                getP2PKHSignature(in1.scriptSig, s1.pushData.front());
                s2.pushData.resize(1);
                getP2PKHSignature(in2.scriptSig, s2.pushData.front());

                assert(!s1.pushData.empty()); // we resized it
                assert(!s2.pushData.empty()); // we resized it
                if (s1.pushData.front().empty() || s2.pushData.front().empty())
                    throw std::runtime_error("scriptSig has no signature");
                auto hashType = s1.pushData.front().back();
                if (!(hashType & SIGHASH_FORKID))
                    throw std::runtime_error("Tx1 is not a Bitcoin Cash transaction");

                hashType = s2.pushData.front().back();
                if (!(hashType & SIGHASH_FORKID))
                    throw std::runtime_error("Tx2 is not a Bitcoin Cash transaction");

                break;
            }
        }
    }

    if (answer.m_prevOutIndex == -1)
        throw std::runtime_error("Transactions do not double spend each other");

    s1.txVersion = t1.nVersion;
    s2.txVersion = t2.nVersion;
    s1.lockTime = t1.nLockTime;
    s2.lockTime = t2.nLockTime;

    hashTx(s1, t1, inputIndex1);
    hashTx(s2, t2, inputIndex2);

    // sort the spenders so the proof stays the same, independent of the order of tx seen first
    int diff = s1.hashOutputs.Compare(s2.hashOutputs);
    if (diff == 0)
        diff = s1.hashPrevOutputs.Compare(s2.hashPrevOutputs);
    if (diff > 0)
        std::swap(s1, s2);

    return answer;
}

DoubleSpendProof::DoubleSpendProof() {}
bool DoubleSpendProof::isEmpty() const { return m_prevOutIndex == -1 || m_prevTxId.IsNull(); }
DoubleSpendProof::Validity DoubleSpendProof::validate(const CTxMemPool &pool, const CTransactionRef ptx) const
{
    AssertLockHeld(pool.cs_txmempool);

    if (m_prevTxId.IsNull() || m_prevOutIndex < 0)
        return Invalid;
    if (m_spender1.pushData.empty() || m_spender1.pushData.front().empty() || m_spender2.pushData.empty() ||
        m_spender2.pushData.front().empty())
        return Invalid;

    // check if ordering is proper. By convention, the first tx must have the smaller hash.
    int diff = m_spender1.hashOutputs.Compare(m_spender2.hashOutputs);
    if (diff == 0)
        diff = m_spender1.hashPrevOutputs.Compare(m_spender2.hashPrevOutputs);
    if (diff > 0)
        return Invalid;

    // Get the previous output we are spending.
    int64_t amount;
    CScript prevOutScript;
    auto prevTx = pool._get(m_prevTxId);
    if (prevTx.get())
    {
        if (prevTx->vout.size() <= (size_t)m_prevOutIndex)
            return Invalid;

        auto output = prevTx->vout.at(m_prevOutIndex);
        amount = output.nValue;
        prevOutScript = output.scriptPubKey;
    }
    else // tx is not found in our mempool, look in the UTXO.
    {
        Coin coin;
        if (!pcoinsTip->GetCoin({m_prevTxId, (uint32_t)m_prevOutIndex}, coin))
        {
            /* if the output we spend is missing then either the tx just got mined
             * or, more likely, our mempool just doesn't have it.
             */
            return MissingUTXO;
        }
        amount = coin.out.nValue;
        prevOutScript = coin.out.scriptPubKey;
    }

    /*
     * Find the matching transaction spending this. Possibly identical to one
     * of the sides of this DSP.
     * We need this because we want the public key that it contains.
     */
    CTransaction tx;
    if (ptx == nullptr)
    {
        auto it = pool.mapNextTx.find({m_prevTxId, (uint32_t)m_prevOutIndex});
        if (it == pool.mapNextTx.end())
        {
            return MissingTransaction;
        }
        tx = *(it->second.ptx);
    }
    else
        tx = *ptx;

    /*
     * TomZ: At this point (2019-07) we only support P2PKH payments.
     *
     * Since we have an actually spending tx, we could trivially support various other
     * types of scripts because all we need to do is replace the signature from our 'tx'
     * with the one that comes from the DSP.
     */
    ScriptType scriptType = P2PKH; // FUTURE: look at prevTx to find out script-type

    std::vector<uint8_t> pubkey;
    for (size_t i = 0; i < tx.vin.size(); ++i)
    {
        if (tx.vin[i].prevout.n == (size_t)m_prevOutIndex && tx.vin[i].prevout.hash == m_prevTxId)
        {
            // Found the input script we need!
            CScript inScript = tx.vin[i].scriptSig;
            if (inScript.IsPayToScriptHash())
                return Invalid;

            auto scriptIter = inScript.begin();
            opcodetype type;
            if (!inScript.GetOp(scriptIter, type)) // P2PKH: first signature
                return Invalid;
            if (!inScript.GetOp(scriptIter, type, pubkey)) // then pubkey
                return Invalid;
            break;
        }
    }

    if (pubkey.empty())
        return Invalid;

    CScript inScript;
    if (scriptType == P2PKH)
    {
        inScript << m_spender1.pushData.front();
        inScript << pubkey;
    }
    DSPSignatureChecker checker1(this, m_spender1, amount);
    ScriptError_t error;
    if (!VerifyScript(inScript, prevOutScript, 0 /*flags*/, MAX_OPS_PER_SCRIPT, checker1, &error))
    {
        LOG(DSPROOF, "DoubleSpendProof failed validating first tx due to %s\n", ScriptErrorString(error));
        return Invalid;
    }

    inScript.clear();
    if (scriptType == P2PKH)
    {
        inScript << m_spender2.pushData.front();
        inScript << pubkey;
    }
    DSPSignatureChecker checker2(this, m_spender2, amount);
    if (!VerifyScript(inScript, prevOutScript, 0 /*flags*/, MAX_OPS_PER_SCRIPT, checker2, &error))
    {
        LOG(DSPROOF, "DoubleSpendProof failed validating second tx due to %s\n", ScriptErrorString(error));
        return Invalid;
    }
    return Valid;
}

void broadcastDspInv(const CTransactionRef &dspTx, const uint256 &hash, CTxMemPool::setEntries *setDescendants)
{
    // send INV to all peers
    CInv inv(MSG_DOUBLESPENDPROOF, hash);
    LOG(DSPROOF, "Broadcasting dsproof INV: %s\n", hash.ToString());

    LOCK(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if (!pnode->fRelayTxes)
            continue;
        LOCK(pnode->cs_filter);
        if (pnode->pfilter)
        {
            if (setDescendants)
            {
                for (auto iter : *setDescendants)
                {
                    if (pnode->pfilter->IsRelevantAndUpdate(iter->GetSharedTx()))
                        pnode->PushInventory(inv);
                }
            }
            // For nodes that we sent this Tx before, send a proof.
            else if (pnode->pfilter->IsRelevantAndUpdate(dspTx))
                pnode->PushInventory(inv);
        }
        else
        {
            pnode->PushInventory(inv);
        }
    }
}

uint256 DoubleSpendProof::GetHash() const { return SerializeHash(*this); }
