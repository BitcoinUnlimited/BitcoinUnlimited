// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SLP_TOKEN_H
#define BITCOIN_SLP_TOKEN_H

#include "script/script.h"
#include "serialize.h"

#include <stdint.h>
#include <vector>

enum SLP_TX_TYPE : uint8_t
{
    SLP_NULL = 0,
    SLP_GENESIS,
    SLP_MINT,
    SLP_SEND,
    SLP_COMMIT
};

class CSLPToken
{
    // using vectors for the variable length fields is not ideal, but it is the easiest way to do it
private:
    // SLP Fields
    uint16_t token_type; // 1-2 bytes
    SLP_TX_TYPE tx_type; // 4-6 bytes

    // NOT IN GENESIS
    std::vector<uint8_t> token_id; // 32 bytes, token genesis transaction hash

    // NOT IN SEND
    uint8_t mint_baton_vout; //(0 bytes or 1 byte between 0x02-0xff), if 0 there is no mint baton

    // ONLY IN SEND
    std::vector<uint64_t> token_output_quantitys; // this can be a max size of 19

    // ONLY IN GENESIS
    std::vector<uint8_t> token_ticker; // (0 or more bytes, suggested utf-8)
    std::vector<uint8_t> token_name; // (0 or more bytes, suggested utf-8)
    std::vector<uint8_t> token_document_url; // (0 or more bytes, suggested ascii)
    std::vector<uint8_t> token_document_hash; // 32 bytes
    uint8_t decimals;
    uint64_t initial_token_mint_quantity;
    // ONLY IN SEND
    uint64_t additional_token_quantity;

public:
    // UTXO Fields
    int nHeight;

private:
    bool DetermineDynamicSize(const CScript &scriptPubKeyIn, size_t &index, uint32_t &return_size);
    SLP_TX_TYPE ParseType(const CScript &scriptPubKeyIn, size_t &index, const uint32_t &tx_type_size);
    uint8_t ParseBytesGenesis(const CScript &scriptPubKeyIn, size_t &index);
    uint8_t ParseBytesMint(const CScript &scriptPubKeyIn, size_t &index);
    uint8_t ParseBytesSend(const CScript &scriptPubKeyIn, size_t &index);
    uint8_t ParseBytesCommit(const CScript &scriptPubKeyIn, size_t &index);
    uint8_t _ParseBytes(const CScript &scriptPubKeyIn);

public:
    CSLPToken(const int &nHeightIn) { nHeight = nHeightIn; }
    CSLPToken() { SetNull(); }
    void SetNull()
    {
        token_type = 0;
        tx_type = SLP_NULL;
        token_id.clear();
        mint_baton_vout = 0;
        token_output_quantitys = std::vector<uint64_t>(19, 0); // prefill with 19 0's
        token_ticker.clear();
        token_name.clear();
        token_document_url.clear();
        token_document_hash.clear();
        decimals = 0;
        initial_token_mint_quantity = 0;
        additional_token_quantity = 0;
        nHeight = 0;
    }

    ADD_SERIALIZE_METHODS;
    // this is not that great, it might be better to make a token header
    // and then different types that extend the header to avoid always having all fields
    // in 1 token class like this
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        uint8_t tx_type_ser;
        if (!ser_action.ForRead()) // Write
        {
            tx_type_ser = (uint8_t)tx_type;
        }
        READWRITE(token_type);
        READWRITE(tx_type_ser);
        READWRITE(token_id);
        READWRITE(mint_baton_vout);
        READWRITE(token_output_quantitys);
        READWRITE(token_ticker);
        READWRITE(token_name);
        READWRITE(token_document_url);
        READWRITE(token_document_hash);
        READWRITE(decimals);
        READWRITE(initial_token_mint_quantity);
        READWRITE(additional_token_quantity);
        if (ser_action.ForRead())
        {
            tx_type = (SLP_TX_TYPE)tx_type_ser;
        }
    }

    uint8_t ParseBytes(const CScript &scriptPubKeyIn)
    {
        uint8_t result = _ParseBytes(scriptPubKeyIn);
        if (result != 0)
        {
            SetNull();
        }
        return result;
    }

    SLP_TX_TYPE GetType() { return tx_type; }
    // TODO determine how to count dynamic memory? maybe just return op return max size?
    size_t DynamicMemoryUsage() const { return 223; }
    bool IsSpent() { return tx_type == SLP_NULL; }
    void Spend() { SetNull(); }
    uint64_t GetOutputAmount();
    uint64_t GetOutputAmountAt(uint32_t n);
    uint32_t GetBatonOut();
};

#endif
