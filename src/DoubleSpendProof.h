// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_DOUBLESPENDPROOF_H
#define BITCOIN_DOUBLESPENDPROOF_H

#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <txmempool.h>
#include <uint256.h>


class DoubleSpendProof
{
public:
    //! limit for the size of a `pushData` vector below
    static constexpr size_t MaxPushDataSize = MAX_SCRIPT_ELEMENT_SIZE;

    /** Creates an empty, invalid object */
    DoubleSpendProof();

    /** Create a proof object, given two conflicting transactions */
    static DoubleSpendProof create(const CTransaction &tx1, const CTransaction &tx2);

    /** Returns true if this object is invalid, i.e. does not represent a double spend proof */
    bool isEmpty() const;

    /** Return codes for the 'validate' function */
    enum Validity
    {
        Valid, //? Double spend proof is valid
        MissingTransaction, //? We cannot determine the validity of this proof because we don't have one of the spends
        MissingUTXO, //? We cannot determine the validity of this proof because the prevout is not available
        Invalid //? This object does not contain a valid doublespend proof
    };

    /** Returns whether this doublespend proof is valid, or why its validity cannot be determined.
     *  pool.cs_txmempool must be held.
     */
    Validity validate(const CTxMemPool &pool, const CTransactionRef ptx = nullptr) const;

    /** Returns the hash of the input transaction (UTXO) that is being doublespent */
    const uint256 &prevTxId() const { return m_prevTxId; }
    /** Returns the index of the output that is being doublespent */
    int32_t prevOutIndex() const { return m_prevOutIndex; }
    /** get the hash of this doublespend proof */
    uint256 GetHash() const;

    /** This struction tracks information about each doublespend transaction */
    struct Spender
    {
        uint32_t txVersion = 0, outSequence = 0, lockTime = 0;
        uint256 hashPrevOutputs, hashSequence, hashOutputs;
        std::vector<std::vector<uint8_t> > pushData;
    };

    const Spender &spender1() const { return m_spender1; }
    const Spender &spender2() const { return m_spender2; }
    // old fashioned serialization.
    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(m_prevTxId);
        READWRITE(m_prevOutIndex);

        READWRITE(m_spender1.txVersion);
        READWRITE(m_spender1.outSequence);
        READWRITE(m_spender1.lockTime);
        READWRITE(m_spender1.hashPrevOutputs);
        READWRITE(m_spender1.hashSequence);
        READWRITE(m_spender1.hashOutputs);
        READWRITE(m_spender1.pushData);

        READWRITE(m_spender2.txVersion);
        READWRITE(m_spender2.outSequence);
        READWRITE(m_spender2.lockTime);
        READWRITE(m_spender2.hashPrevOutputs);
        READWRITE(m_spender2.hashSequence);
        READWRITE(m_spender2.hashOutputs);
        READWRITE(m_spender2.pushData);

        // Sanitize and check limits for both pushData vectors above
        for (auto *pushData : {&m_spender1.pushData, &m_spender2.pushData})
        {
            // Enforce pushData.size() <= 1 predicate
            if (pushData->size() > 1)
            {
                if (ser_action.ForRead())
                {
                    // Unserializing from network:
                    //   Tolerate unknown data and just discard what we don't understand
                    pushData->resize(1);
                }
                else
                {
                    // We are serializing an internally generated DSProof:
                    //   ->size() > 1 is a programming error; throw so that calling code fails
                    throw std::ios_base::failure("DSProof contained more than 1 pushData");
                }
            }
            // Enforce script data must be within size limits
            if (!pushData->empty() && pushData->front().size() > MaxPushDataSize)
                throw std::ios_base::failure("DSProof script size limit exceeded");
        }
    }

private:
    uint256 m_prevTxId;
    int32_t m_prevOutIndex = -1;

    Spender m_spender1, m_spender2;
};

/** Send notifcation of the availability of a doublespend to all connected nodes */
void broadcastDspInv(const CTransactionRef &dspTx,
    const uint256 &hash,
    CTxMemPool::setEntries *setDescendants = nullptr);

#endif
