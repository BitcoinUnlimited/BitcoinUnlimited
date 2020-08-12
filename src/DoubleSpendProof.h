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
    DoubleSpendProof();

    static DoubleSpendProof create(const CTransaction &tx1, const CTransaction &tx2);

    bool isEmpty() const;

    enum Validity
    {
        Valid,
        MissingTransaction,
        MissingUTXO,
        Invalid
    };

    Validity validate(const CTxMemPool &pool, const CTransactionRef ptx = nullptr) const;

    uint256 prevTxId() const;
    int prevOutIndex() const;

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
    uint256 GetHash() const;

private:
    uint256 m_prevTxId;
    int32_t m_prevOutIndex = -1;

    Spender m_spender1, m_spender2;
};

void broadcastDspInv(const CTransactionRef &dspTx,
    const uint256 &hash,
    CTxMemPool::setEntries *setDescendants = nullptr);

#endif
