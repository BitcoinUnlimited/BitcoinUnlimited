#ifndef INDEX_H
#define INDEX_H

#include "chain.h"

/** Used to marshal pointers into hashes for db storage. */
class CDbBlockIndex : public CBlockIndex
{
public:
    uint256 hashPrev;

    CDbBlockIndex() { hashPrev = uint256(); }
    explicit CDbBlockIndex(const CBlockIndex *pindex) : CBlockIndex(*pindex)
    {
        hashPrev = (pprev ? pprev->GetBlockHash() : uint256());
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(VARINT(nVersion));

        READWRITE(VARINT(nHeight));
        READWRITE(VARINT(nStatus));
        READWRITE(VARINT(nTx));
        READWRITE(storeFile);
        READWRITE(storeDb);
        if (nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO))
            READWRITE(nFile);
        if (nStatus & BLOCK_HAVE_DATA)
            READWRITE(nDataPos);
        if (nStatus & BLOCK_HAVE_UNDO)
            READWRITE(nUndoPos);

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    uint256 GetBlockHash() const
    {
        CBlockHeader block;
        block.nVersion = nVersion;
        block.hashPrevBlock = hashPrev;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime = nTime;
        block.nBits = nBits;
        block.nNonce = nNonce;
        return block.GetHash();
    }


    std::string ToString() const
    {
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString();
        str +=
            strprintf("\n                hashBlock=%s, hashPrev=%s)", GetBlockHash().ToString(), hashPrev.ToString());
        return str;
    }
};

#endif // INDEX_H
