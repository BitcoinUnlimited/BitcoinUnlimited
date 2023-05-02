// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include "amount.h"
#include "primitives/token.h"
#include "script/script.h"
#include "serialize.h"
#include "tweak.h"
#include "uint256.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

extern CTweak<unsigned int> nDustThreshold;

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    uint32_t n;

    COutPoint() { SetNull(); }
    COutPoint(uint256 hashIn, uint32_t nIn)
    {
        hash = hashIn;
        n = nIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull()
    {
        hash.SetNull();
        n = (uint32_t)-1;
    }
    bool IsNull() const { return (hash.IsNull() && n == (uint32_t)-1); }
    friend bool operator<(const COutPoint &a, const COutPoint &b)
    {
        return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
    }

    friend bool operator==(const COutPoint &a, const COutPoint &b) { return (a.hash == b.hash && a.n == b.n); }
    friend bool operator!=(const COutPoint &a, const COutPoint &b) { return !(a == b); }
    std::string ToString(bool fVerbose = false) const;
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;

    /* Setting nSequence to this value for every input in a transaction
     * disables nLockTime. */
    static const uint32_t SEQUENCE_FINAL = 0xffffffff;

    /* Below flags apply in the context of BIP 68*/
    /* If this flag set, CTxIn::nSequence is NOT interpreted as a
     * relative lock-time. */
    static const uint32_t SEQUENCE_LOCKTIME_DISABLE_FLAG = (1U << 31);

    /* If CTxIn::nSequence encodes a relative lock-time and this flag
     * is set, the relative lock-time has units of 512 seconds,
     * otherwise it specifies blocks with a granularity of 1. */
    static const uint32_t SEQUENCE_LOCKTIME_TYPE_FLAG = (1 << 22);

    /* If CTxIn::nSequence encodes a relative lock-time, this mask is
     * applied to extract that lock-time from the sequence field. */
    static const uint32_t SEQUENCE_LOCKTIME_MASK = 0x0000ffff;

    /* In order to use the same number of bits to encode roughly the
     * same wall-clock duration, and because blocks are naturally
     * limited to occur every 600s on average, the minimum granularity
     * for time-based relative lock-time is fixed at 512 seconds.
     * Converting from CTxIn::nSequence to seconds is performed by
     * multiplying by 512 = 2^9, or equivalently shifting up by
     * 9 bits. */
    static const int SEQUENCE_LOCKTIME_GRANULARITY = 9;

    CTxIn() { nSequence = SEQUENCE_FINAL; }
    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn = CScript(), uint32_t nSequenceIn = SEQUENCE_FINAL);
    CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn = CScript(), uint32_t nSequenceIn = SEQUENCE_FINAL);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(prevout);
        READWRITE(*(CScriptBase *)(&scriptSig));
        READWRITE(nSequence);
    }

    friend bool operator==(const CTxIn &a, const CTxIn &b)
    {
        return (a.prevout == b.prevout && a.scriptSig == b.scriptSig && a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn &a, const CTxIn &b) { return !(a == b); }
    std::string ToString(bool fVerbose = false) const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;
    token::OutputDataPtr tokenDataPtr; ///< may be null (indicates no token data for this output)

    CTxOut() { SetNull(); }
    CTxOut(const CAmount &nValueIn, CScript scriptPubKeyIn, const token::OutputDataPtr &tokenDataIn = {})
        : nValue(nValueIn), scriptPubKey(scriptPubKeyIn), tokenDataPtr(tokenDataIn)
    {
    }

    CTxOut(CAmount nValueIn, const CScript &scriptPubKeyIn, token::OutputDataPtr &&tokenDataIn)
        : nValue(nValueIn), scriptPubKey(scriptPubKeyIn), tokenDataPtr(std::move(tokenDataIn))
    {
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(nValue);
        if (!ser_action.ForRead() && !this->tokenDataPtr)
        {
            // fast-path for writing with no token data, just write out the scriptPubKey directly
            READWRITE(*(CScriptBase *)(&scriptPubKey));
        }
        else
        {
            token::WrappedScriptPubKey wspk;
            SER_WRITE(token::WrapScriptPubKey(wspk, this->tokenDataPtr, this->scriptPubKey, s.GetVersion()));
            READWRITE(wspk);
            SER_READ(token::UnwrapScriptPubKey(wspk, this->tokenDataPtr, this->scriptPubKey, s.GetVersion()));
        }
    }

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
        tokenDataPtr.reset();
    }

    bool IsNull() const { return (nValue == -1); }

    bool HasUnparseableTokenData() const
    {
        return !tokenDataPtr && !scriptPubKey.empty() && scriptPubKey[0] == token::PREFIX_BYTE;
    }

    uint256 GetHash() const;

    CAmount GetDustThreshold() const
    {
        if (scriptPubKey.IsUnspendable())
            return (CAmount)0;

        return (CAmount)nDustThreshold.Value();
    }
    bool IsDust() const { return (nValue < GetDustThreshold()); }
    friend bool operator==(const CTxOut &a, const CTxOut &b)
    {
        return (a.nValue == b.nValue && a.scriptPubKey == b.scriptPubKey && a.tokenDataPtr == b.tokenDataPtr);
    }

    friend bool operator!=(const CTxOut &a, const CTxOut &b) { return !(a == b); }
    std::string ToString(bool fVerbose = false) const;
};

struct CMutableTransaction;

/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
private:
    /** Memory only. */
    const uint256 hash;
    void UpdateHash() const;
    mutable std::atomic<size_t> nTxSize; // Serialized transaction size in bytes.


public:
    // Default transaction version.
    static constexpr int32_t CURRENT_VERSION = 1;

    // Note: These two values are used until Upgrade9 activates (May 2023),
    // after which time they will no longer be relevant since version
    // enforcement will be done by the consensus layer.
    static constexpr int32_t MIN_STANDARD_VERSION = 1, MAX_STANDARD_VERSION = 2;

    // Changing the default transaction version requires a two step process:
    // First adapting relay policy by bumping MAX_CONSENSUS_VERSION, and then
    // later date bumping the default CURRENT_VERSION at which point both
    // CURRENT_VERSION and MAX_CONSENSUS_VERSION will be equal.
    //
    // Note: These values are ignored until Upgrade9 (May 2023) is activated,
    // after which time versions outside the range [MIN_CONSENSUS_VERSION,
    // MAX_CONSENSUS_VERSION] are rejected by consensus.
    static constexpr int32_t MIN_CONSENSUS_VERSION = 1, MAX_CONSENSUS_VERSION = 2;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const int32_t nVersion;
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;
    const uint32_t nLockTime;

    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    CTransaction(const CMutableTransaction &tx);
    CTransaction(CMutableTransaction &&tx);

    CTransaction(const CTransaction &tx);
    CTransaction &operator=(const CTransaction &tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(*const_cast<int32_t *>(&this->nVersion));
        READWRITE(*const_cast<std::vector<CTxIn> *>(&vin));
        READWRITE(*const_cast<std::vector<CTxOut> *>(&vout));
        READWRITE(*const_cast<uint32_t *>(&nLockTime));
        if (ser_action.ForRead())
            UpdateHash();
    }

    template <typename Stream>
    CTransaction(deserialize_type, Stream &s) : CTransaction(CMutableTransaction(deserialize, s))
    {
    }

    bool IsNull() const { return vin.empty() && vout.empty(); }
    const uint256 &GetHash() const { return hash; }
    // True if only scriptSigs are different
    bool IsEquivalentTo(const CTransaction &tx) const;

    //* Return true if this transaction contains at least one OP_RETURN output.
    bool HasData() const;
    //* Return true if this transaction contains at least one OP_RETURN output, with the specified data ID
    // the data ID is defined as a 4 byte pushdata containing a little endian 4 byte integer.
    bool HasData(uint32_t dataID) const;

    // Return sum of txouts.
    CAmount GetValueOut() const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    // Compute priority, given priority of inputs and (optionally) tx size
    double ComputePriority(double dPriorityInputs, unsigned int nSize = 0) const;

    // Compute modified tx size for priority calculation (optionally given tx size)
    unsigned int CalculateModifiedSize(unsigned int nSize = 0) const;

    bool IsCoinBase() const { return (vin.size() == 1 && vin[0].prevout.IsNull()); }


    /// @return true if this transaction has any vouts with non-null token::OutputData
    bool HasTokenOutputs() const
    {
        return std::any_of(vout.begin(), vout.end(), [](const CTxOut &out) { return bool(out.tokenDataPtr); });
    }

    /// @return true if any vouts have scriptPubKey[0] == token::PREFIX_BYTE,
    /// and if the vout has tokenDataPtr == nullptr.  This indicates badly
    /// formatted and/or unparseable token data embedded in the scriptPubKey.
    /// Before token activation we allow such scriptPubKeys to appear in
    /// vouts, but after activation of native tokens such txns are rejected by
    /// consensus (see: CheckTxTokens() in consensus/tokens.cpp).
    bool HasOutputsWithUnparseableTokenData() const
    {
        return std::any_of(vout.begin(), vout.end(), [](const CTxOut &out) { return out.HasUnparseableTokenData(); });
    }


    friend bool operator==(const CTransaction &a, const CTransaction &b) { return a.hash == b.hash; }
    friend bool operator!=(const CTransaction &a, const CTransaction &b) { return a.hash != b.hash; }
    std::string ToString(bool fVerbose = false) const;

    // Return the size of the transaction in bytes.
    size_t GetTxSize() const;
};

/** A mutable version of CTransaction. */
struct CMutableTransaction
{
    int32_t nVersion;
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    uint32_t nLockTime;

    CMutableTransaction();
    CMutableTransaction(const CTransaction &tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(vin);
        READWRITE(vout);
        READWRITE(nLockTime);
    }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream &s)
    {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;

    /// Mutates this txn. Sorts the inputs according to BIP-69
    void SortInputsBip69();
    /// Mutates this txn. Sorts the outputs according to BIP-69
    void SortOutputsBip69();
    /// Convenience: Calls the above two functions.
    void SortBip69()
    {
        SortInputsBip69();
        SortOutputsBip69();
    }
};

/** Properties of a transaction that are discovered during tx evaluation */
class CTxProperties
{
public:
    uint64_t countWithAncestors = 0;
    uint64_t sizeWithAncestors = 0;
    uint64_t countWithDescendants = 0;
    uint64_t sizeWithDescendants = 0;
    CTxProperties() {}
    CTxProperties(uint64_t ancestorCount, uint64_t ancestorSize, uint64_t descendantCount, uint64_t descendantSize)
        : countWithAncestors(ancestorCount), sizeWithAncestors(ancestorSize), countWithDescendants(descendantCount),
          sizeWithDescendants(descendantSize)
    {
    }
};

typedef std::shared_ptr<const CTransaction> CTransactionRef;
static inline CTransactionRef MakeTransactionRef() { return std::make_shared<const CTransaction>(); }
template <typename Tx>
static inline CTransactionRef MakeTransactionRef(Tx &&txIn)
{
    return std::make_shared<const CTransaction>(std::forward<Tx>(txIn));
}
#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
