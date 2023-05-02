// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COMPRESSOR_H
#define BITCOIN_COMPRESSOR_H

#include "primitives/transaction.h"
#include "script/script.h"
#include "serialize.h"

class CKeyID;
class CPubKey;
class ScriptID;

/** Compact serializer for scripts.
 *
 *  It detects common cases and encodes them much more efficiently.
 *  3 special cases are defined:
 *  * Pay to pubkey hash (encoded as 21 bytes)
 *  * Pay to script hash (encoded as 21 bytes)
 *  * Pay to pubkey starting with 0x02, 0x03 or 0x04 (encoded as 33 bytes)
 *
 *  Other scripts up to 121 bytes require 1 byte + script length. Above
 *  that, scripts up to 16505 bytes require 2 bytes + script length.
 */
class CScriptCompressor
{
private:
    /**
     * make this static for now (there are only 6 special scripts defined)
     * this can potentially be extended together with a new nVersion for
     * transactions, in which case this value becomes dependent on nVersion
     * and nHeight of the enclosing transaction.
     */
    static const unsigned int nSpecialScripts = 6;

    CScript &script;

protected:
    /**
     * These check for scripts for which a special case with a shorter encoding is defined.
     * They are implemented separately from the CScript test, as these test for exact byte
     * sequence correspondences, and are more strict. For example, IsToPubKey also verifies
     * whether the public key is valid (as invalid ones cannot be represented in compressed
     * form).
     */
    bool IsToKeyID(CKeyID &hash) const;
    bool IsToScriptID(ScriptID &hash) const;
    bool IsToPubKey(CPubKey &pubkey) const;

    bool Compress(std::vector<unsigned char> &out) const;
    unsigned int GetSpecialSize(unsigned int nSize) const;
    bool Decompress(unsigned int nSize, const std::vector<unsigned char> &out);

public:
    CScriptCompressor(CScript &scriptIn) : script(scriptIn) {}
    template <typename Stream>
    void Serialize(Stream &s) const
    {
        std::vector<unsigned char> compr;
        if (Compress(compr))
        {
            s << CFlatData(compr);
            return;
        }
        unsigned int nSize = script.size() + nSpecialScripts;
        s << VARINT(nSize);
        s << CFlatData(script);
    }

    template <typename Stream>
    static void SerializeWrapped(Stream &s, const token::WrappedScriptPubKey &wspk)
    {
        CScript tmp(wspk.data(), wspk.data() + wspk.size());
        s << CScriptCompressor(tmp);
    }

    template <typename Stream>
    void Unserialize(Stream &s)
    {
        static constexpr unsigned int MAX_VECTOR_ALLOCATE = 5000000;
        unsigned int nSize = 0;
        s >> VARINT(nSize);
        if (nSize < nSpecialScripts)
        {
            std::vector<unsigned char> vch(GetSpecialSize(nSize), 0x00);
            s >> REF(CFlatData(vch));
            Decompress(nSize, vch);
            return;
        }
        nSize -= nSpecialScripts;
        script.resize(0);
        for (unsigned pos = 0, chunk; pos < nSize; pos += chunk)
        {
            // Read-in 5MB at a time to prevent over-allocation on bad/garbled data. This algorithm is similar to
            // the one unsed in Unserialize_vector in serialize.h.
            chunk = std::min(nSize - pos, MAX_VECTOR_ALLOCATE);
            script.resize_uninitialized(pos + chunk);
            s >> REF(Span{script.data() + pos, chunk});
        }
    }

    template <typename Stream>
    static void UnserializeWrapped(Stream &s, token::WrappedScriptPubKey &wspk)
    {
        CScript tmp;
        s >> REF(CScriptCompressor(tmp));
        wspk.assign(tmp.begin(), tmp.end());
    }
};

/** wrapper for CTxOut that provides a more compact serialization */
class CTxOutCompressor
{
private:
    CTxOut &txout;

public:
    static uint64_t CompressAmount(uint64_t nAmount);
    static uint64_t DecompressAmount(uint64_t nAmount);

    CTxOutCompressor(CTxOut &txoutIn) : txout(txoutIn) {}
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        if (!ser_action.ForRead())
        {
            uint64_t nVal = CompressAmount(txout.nValue);
            READWRITE(VARINT(nVal));
        }
        else
        {
            uint64_t nVal = 0;
            READWRITE(VARINT(nVal));
            txout.nValue = DecompressAmount(nVal);
        }

        token::WrappedScriptPubKey wspk;
        SER_WRITE(token::WrapScriptPubKey(wspk, txout.tokenDataPtr, txout.scriptPubKey, s.GetVersion()));

        SER_READ(CScriptCompressor::UnserializeWrapped(s, wspk));
        SER_WRITE(CScriptCompressor::SerializeWrapped(s, wspk));

        SER_READ(token::UnwrapScriptPubKey(wspk, txout.tokenDataPtr, txout.scriptPubKey, s.GetVersion()));

        if (txout.scriptPubKey.size() > MAX_SCRIPT_SIZE)
        {
            // Overly long script, replace with a short invalid one
            // - This logic originally lived in ScriptCompression::Unserialize but was moved here
            // - Note the expression below is explicitly an assignment and not a `.resize(1, OP_RETURN)` so as to
            //   force the release of >10KB of memory obj.scriptPubKey has allocated.
            SER_READ(txout.scriptPubKey = CScript() << OP_RETURN);
        }
    }
};

#endif // BITCOIN_COMPRESSOR_H
