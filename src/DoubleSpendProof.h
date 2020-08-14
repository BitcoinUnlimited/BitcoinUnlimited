// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DOUBLESPENDPROOF_H
#define DOUBLESPENDPROOF_H

#include "primitives/transaction.h"
#include "serialize.h"
#include "txmempool.h"
#include <deque>
#include <uint256.h>


class DoubleSpendProof
{
public:
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
    uint256 prevTxId() const;

    /** Returns the index of the output that is being doublespent */
    int prevOutIndex() const;

    /** get the hash of this doublespend proof */
    uint256 GetHash() const;

    /** This struction tracks information about each doublespend transaction */
    struct Spender
    {
        uint32_t txVersion = 0, outSequence = 0, lockTime = 0;
        uint256 hashPrevOutputs, hashSequence, hashOutputs;
        std::vector<std::vector<uint8_t> > pushData;
    };

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
