// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include "persistent_map.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

const uint32_t BIP_009_MASK = 0x20000000;
const uint32_t BASE_VERSION = 0x20000000;
const uint32_t FORK_BIT_2MB = 0x10000000; // Vote for 2MB fork
const bool DEFAULT_2MB_VOTE = false;

class CXThinBlock;
class CThinBlock;
class CompactBlock;
class CGrapheneBlock;

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    static const int32_t CURRENT_VERSION = BASE_VERSION;
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CBlockHeader() { SetNull(); }
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const { return (nBits == 0); }
    uint256 GetHash() const;

    int64_t GetBlockTime() const { return (int64_t)nTime; }
};

/*! Class used as the key in the block's internal transaction map to
  order it, supports < comparison as used by persistent_map. */
class CTransactionSlot
{
public:
    CTransactionSlot(const CTransactionRef _tx, const int _idx = -1) : tx(_tx), idx(_idx) {}
    bool operator<(const CTransactionSlot &other) const;
    bool operator==(const CTransactionSlot &other) const { return !(*this < other) && !(other < *this); }
    std::string ToString() const;

private:
    const CTransactionRef tx;
    int idx; // position. if negative, determined through hash
};


class CPersistentTransactionMap : public persistent_map<CTransactionSlot, const CTransaction>
{
public:
    template <typename Stream>
    inline void Serialize(Stream &os) const
    {
        WriteCompactSize(os, size());
        for (auto i : *this)
            i.second.Serialize(os);
    }

    // FIXME: This very unpersistent method is quite ugly and the Bitcoin stream system seems to be too
    // unflexible to avoid calling Unserialize(..) like this on a deserializable class and should be extended
    // to handle this and similar cases in a proper way.
    template <typename Stream>
    inline void Unserialize(Stream &is)
    {
        unsigned int nSize = ReadCompactSize(is);
        std::vector<std::pair<unsigned int, std::shared_ptr<CTransaction> > > v;
        v.reserve(nSize);

        bool needs_order = false;
        unsigned i;

        for (i = 0; i < nSize; i++)
        {
            std::shared_ptr<CTransaction> item = std::shared_ptr<CTransaction>(new CTransaction());
            item->Unserialize(is);
            needs_order |= v.size() > 1 && !(CTransactionSlot(v.back().second) < CTransactionSlot(item));
            v.emplace_back(std::pair<size_t, std::shared_ptr<CTransaction> >(i, item));
        }
        *this = CPersistentTransactionMap();

        /*! FIXME: Maybe do something better than random shuffling here? */
        std::random_shuffle(v.begin(), v.end());

        if (needs_order)
            for (i = 0; i < nSize; i++)
                *this = insert(CTransactionSlot(v[i].second, v[i].first), v[i].second);
        else
            for (i = 0; i < nSize; i++)
                *this = insert(CTransactionSlot(v[i].second, (v[i].first == 0 ? 0 : -1)), v[i].second);

        LOG(WB, "Deserialized block transaction tree needs_order: %d, max depth: %d, for size: %d", needs_order,
            max_depth(), v.size());
    }
    CPersistentTransactionMap &operator=(const persistent_map<CTransactionSlot, const CTransaction> &other)
    {
        *(persistent_map<CTransactionSlot, const CTransaction> *)this = other;
        return *this;
    }
};

//! Used to unpack the map iterator type over pairs to just the value (CTransactionRef)
class CPersistentMapBlockIterator
{
    typedef CPersistentTransactionMap::const_iterator parent_iterator;

public:
    typedef CTransactionRef value_type;
    typedef std::input_iterator_tag iterator_category;
    CPersistentMapBlockIterator(parent_iterator _iter) : iter(_iter) {}
    const CTransactionRef operator*() const { return iter.value_ptr(); }
    bool operator==(const CPersistentMapBlockIterator &other) const { return iter == other.iter; }
    bool operator!=(const CPersistentMapBlockIterator &other) const { return iter != other.iter; }
    CPersistentMapBlockIterator &operator++()
    { // prefix
        ++iter;
        return *this;
    }
    CPersistentMapBlockIterator operator++(int)
    { // postfix
        parent_iterator x = iter;
        ++iter;
        return CPersistentMapBlockIterator(x);
    }

private:
    parent_iterator iter;
};


class CBlock : public CBlockHeader
{
private:
    // memory only
    mutable uint64_t nBlockSize; // Serialized block size in bytes
protected:
    // network and disk
    CPersistentTransactionMap mtx;

public:
    // Xpress Validation: (memory only)
    //! Orphans, or Missing transactions that have been re-requested, are stored here.
    std::set<uint256> setUnVerifiedTxns;

    // Xpress Validation: (memory only)
    //! A flag which when true indicates that Xpress validation is enabled for this block.
    bool fXVal;

public:
    typedef CPersistentMapBlockIterator const_iterator;

    // functions to access internal transaction data
    const_iterator begin() const { return const_iterator(mtx.begin()); }
    const_iterator begin_past_coinbase() const
    {
        const_iterator b = begin();
        ++b;
        return b;
    }
    const_iterator end() const { return const_iterator(mtx.end()); }
    const CTransactionRef coinbase() const
    {
        if (mtx.size())
            return *begin();
        else
            return nullptr;
    }
    uint64_t numTransactions() const { return mtx.size(); }
    bool empty() const { return numTransactions() == 0; }
    void add(const CTransactionRef &txnref) { mtx = mtx.insert(CTransactionSlot(txnref, mtx.size()), txnref); }
    void setCoinbase(const CTransactionRef &txnref) { mtx = mtx.insert(CTransactionSlot(txnref, 0), txnref); }
    // sort block to be LTOR (leaves coinbase alone)
    void sortLTOR(const bool no_dups = false);
    const CTransactionRef by_pos(size_t index) const { return mtx.by_rank(index).value_ptr(); }
    // memory only
    // 0.11: mutable std::vector<uint256> vMerkleTree;
    mutable bool fChecked;
    mutable bool fExcessive; // Is the block "excessive"

    CBlock() { SetNull(); }
    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *((CBlockHeader *)this) = header;
    }

    static bool VersionKnown(int32_t nVersion, int32_t voteBits)
    {
        if (nVersion >= 1 && nVersion <= 4)
            return true;
        // BIP009 / versionbits:
        if (nVersion & BIP_009_MASK)
        {
            uint32_t v = nVersion & ~BIP_009_MASK;
            if ((v & ~voteBits) == 0)
                return true;
        }
        return false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(*(CBlockHeader *)this);
        READWRITE(mtx);
    }

    /*
    template <typename Stream>
    void Serialize(Stream &s) const
    {
        (CBlockHeader*)(this) -> Serialize(s);
        Serialize(s, mtx);
    }

    template <typename Stream>
    void Unserialize(Stream &s)
    {
        (CBlockHeader*)(this) -> Unserialize(s);
        Unserialize(s, mtx);
        }*/


    uint64_t GetHeight() const // Returns the block's height as specified in its coinbase transaction
    {
        if (coinbase() == nullptr)
            return 0;
        const CScript &sig = coinbase()->vin[0].scriptSig;
        int numlen = sig[0];
        if (numlen == OP_0)
            return 0;
        if ((numlen >= OP_1) && (numlen <= OP_16))
            return numlen - OP_1 + 1;
        std::vector<unsigned char> heightScript(numlen);
        copy(sig.begin() + 1, sig.begin() + 1 + numlen, heightScript.begin());
        CScriptNum coinbaseHeight(heightScript, false, numlen);
        return coinbaseHeight.getint();
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        mtx = CPersistentTransactionMap();
        fChecked = false;
        fExcessive = false;
        fXVal = false;
        nBlockSize = 0;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion = nVersion;
        block.hashPrevBlock = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime = nTime;
        block.nBits = nBits;
        block.nNonce = nNonce;
        return block;
    }

    std::string ToString() const;

    // Return the serialized block size in bytes. This is only done once and then the result stored
    // in nBlockSize for future reference, saving unncessary and expensive serializations.
    uint64_t GetBlockSize() const;

    size_t RecursiveDynamicUsage() const;


    //! Maximum depth of underlying binary tree to store transaction set
    size_t treeMaxDepth() const { return mtx.max_depth(); }
};

/**
 * Used for thin type blocks that we want to reconstruct into a full block. All the data
 * necessary to recreate the block are held within the thinrelay objects which are subsequently
 * stored within this class as smart pointers.
 */
class CBlockThinRelay : public CBlock
{
public:
    //! thinrelay block types: (memory only)
    std::shared_ptr<CThinBlock> thinblock;
    std::shared_ptr<CXThinBlock> xthinblock;
    std::shared_ptr<CompactBlock> cmpctblock;
    std::shared_ptr<CGrapheneBlock> grapheneblock;

    //! Track the current block size during reconstruction: (memory only)
    uint64_t nCurrentBlockSize;

    CBlockThinRelay() { SetNull(); }
    ~CBlockThinRelay() { SetNull(); }
    void SetNull()
    {
        CBlock::SetNull();
        nCurrentBlockSize = 0;
        thinblock.reset();
        xthinblock.reset();
        cmpctblock.reset();
        grapheneblock.reset();
    }
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}
    CBlockLocator(const std::vector<uint256> &vHaveIn) { vHave = vHaveIn; }
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull() { vHave.clear(); }
    bool IsNull() const { return vHave.empty(); }
};

typedef std::shared_ptr<CBlock> CBlockRef;
typedef std::shared_ptr<const CBlock> ConstCBlockRef;

static inline CBlockRef MakeBlockRef() { return std::make_shared<CBlock>(); }
template <typename Blk>
static inline CBlockRef MakeBlockRef(Blk &&blkIn)
{
    return std::make_shared<CBlock>(std::forward<Blk>(blkIn));
}

#endif // BITCOIN_PRIMITIVES_BLOCK_H
