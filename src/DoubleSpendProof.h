// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DOUBLESPENDPROOF_H
#define DOUBLESPENDPROOF_H

#include <uint256.h>
#include "serialize.h"
#include <deque>

class CTxMemPool;
class CTransaction;

class DoubleSpendProof
{
public:
    DoubleSpendProof();

    static DoubleSpendProof create(const CTransaction &tx1, const CTransaction &tx2);

    bool isEmpty() const;

    enum Validity {
        Valid,
        MissingTransaction,
        MissingUTXO,
        Invalid
    };

    Validity validate() const;

    uint256 prevTxId() const;
    int prevOutIndex() const;

    struct Spender {
        uint32_t txVersion = 0, outSequence = 0, lockTime = 0;
        uint256 hashPrevOutputs, hashSequence, hashOutputs;
        std::vector<std::vector<uint8_t>> pushData;
    };

    Spender firstSpender() const {
        return m_spender1;
    }
    Spender doubleSpender() const {
        return m_spender2;
    }

    // old fashioned serialization.
    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
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

    uint256 createHash() const;

private:
    uint256 m_prevTxId;
    int32_t m_prevOutIndex = -1;

    Spender m_spender1, m_spender2;
};

#endif
