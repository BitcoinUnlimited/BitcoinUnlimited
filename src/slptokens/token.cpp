// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "token.h"
#include "cash_protocols.h"

bool CSLPToken::DetermineDynamicSize(const CScript &scriptPubKeyIn, size_t &index, uint32_t &return_size)
{
    return_size = 0;
    uint8_t size = scriptPubKeyIn[index];
    index++;
    // no empty-push opcode or 1-byte literal push opcodes allowed
    if (size == 0x00 || (size >= 0x4f && size <= 0x60))
    {
        return_size = 2;
        return false;
    }
    if (size == 0x4c) // OP_PUSHDATA1 - 0x4c 0x04 [chunk]
    {
        uint8_t push_size = scriptPubKeyIn[index];
        index++;
        return_size = (uint32_t)push_size;
        return true;
    }
    else if (size == 0x4d) // OP_PUSHDATA2 - 0x4d 0x04 0x00 [chunk]
    {
        uint16_t push_size = ReadLE16(&scriptPubKeyIn[index]);
        index = index + 2;
        return_size = (uint32_t)push_size;
        return true;
    }
    else if (size == 0x4e) // OP_PUSHDATA4 - 0x4e 0x04 0x00 0x00 0x00 [chunk]
    {
        uint32_t push_size = ReadLE32(&scriptPubKeyIn[index]);
        index = index + 4;
        return_size = push_size;
        return true;
    }
    return_size = (uint32_t)size;
    return true;
}

SLP_TX_TYPE CSLPToken::ParseType(const CScript &scriptPubKeyIn, size_t &index, const uint32_t &tx_type_size)
{
    // we can assume some types purely based off size
    // GENESIS = 0x47454e45534953, it is the only type with length 7
    if (tx_type_size == 7)
    {
        index = index + tx_type_size;
        return SLP_GENESIS;
    }
    // COMMIT = 0x434f4d4d4954, it is the only type with length 6
    if (tx_type_size == 6)
    {
        index = index + tx_type_size;
        return SLP_COMMIT;
    }
    if (tx_type_size == 4)
    {
        uint8_t *tx_type_bytes = (uint8_t *)calloc(tx_type_size, sizeof(uint8_t));
        // memcpy instead of ReadLE32 because it is ascii
        memcpy(tx_type_bytes, &scriptPubKeyIn[index], 4);
        // MINT = 0x4d494e54
        if (tx_type_bytes[0] == 0x4d && tx_type_bytes[1] == 0x49 && tx_type_bytes[2] == 0x4e &&
            tx_type_bytes[3] == 0x54)
        {
            index = index + tx_type_size;
            return SLP_MINT;
        }
        // SEND = 0x53454e44
        if (tx_type_bytes[0] == 0x53 && tx_type_bytes[1] == 0x45 && tx_type_bytes[2] == 0x4e &&
            tx_type_bytes[3] == 0x44)
        {
            index = index + tx_type_size;
            return SLP_SEND;
        }
    }
    index = index + tx_type_size;
    return SLP_NULL;
}

/**
 * <token_ticker> (0 to ∞ bytes, suggested utf-8)
 * <token_name> (0 to ∞ bytes, suggested utf-8)
 * <token_document_url> (0 to ∞ bytes, suggested ascii)
 * <token_document_hash> (0 bytes or 32 bytes)
 * <decimals> (1 byte in range 0x00-0x09)
 * <mint_baton_vout> (0 bytes, or 1 byte in range 0x02-0xff)
 * <initial_token_mint_quantity> (8 byte integer)
 */
uint8_t CSLPToken::ParseBytesGenesis(const CScript &scriptPubKeyIn, size_t &index)
{
    uint32_t token_ticker_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, token_ticker_size))
    {
        return token_ticker_size;
    }
    token_ticker = std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + token_ticker_size]);
    index = index + token_ticker_size;
    if (index >= scriptPubKeyIn.size())
    {
        return 12;
    }

    uint32_t token_name_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, token_name_size))
    {
        return token_name_size;
    }
    token_name = std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + token_name_size]);
    index = index + token_name_size;
    if (index >= scriptPubKeyIn.size())
    {
        return 12;
    }

    uint32_t token_document_url_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, token_document_url_size))
    {
        return token_document_url_size;
    }
    token_document_url = std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + token_document_url_size]);
    index = index + token_document_url_size;
    if (index >= scriptPubKeyIn.size())
    {
        return 12;
    }


    uint32_t token_document_hash_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, token_document_hash_size))
    {
        return token_document_hash_size;
    }
    // hash size must be 32 bytes
    if (token_document_hash_size != 32 && token_document_hash_size != 0)
    {
        return 10;
    }
    token_document_hash =
        std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + token_document_hash_size]);
    index = index + token_document_hash_size;
    if (index >= scriptPubKeyIn.size())
    {
        return 12;
    }

    uint32_t decimals_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, decimals_size))
    {
        return decimals_size;
    }
    decimals = scriptPubKeyIn[index];
    if (decimals > 9)
    {
        return 6;
    }
    index++;
    if (index >= scriptPubKeyIn.size())
    {
        return 12;
    }

    // need to check if 0 bytes
    uint32_t mint_baton_vout_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, mint_baton_vout_size))
    {
        return mint_baton_vout_size;
    }
    if (mint_baton_vout_size != 1 && mint_baton_vout_size != 0)
    {
        // vout size must be 0 or 1 byte
        return 7;
    }
    // if it is 1 byte, check for valid value
    if (mint_baton_vout_size == 1)
    {
        mint_baton_vout = scriptPubKeyIn[index];
        if (mint_baton_vout < 2)
        {
            // only values 0x02-0xff are valid
            return 8;
        }
        index++;
    }
    uint32_t initial_token_mint_quantity_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, initial_token_mint_quantity_size))
    {
        return initial_token_mint_quantity_size;
    }
    if (initial_token_mint_quantity_size != 8)
    {
        // must be 8 bytes
        return 9;
    }
    initial_token_mint_quantity = ReadBE64(&scriptPubKeyIn[index]);
    index = index + 8;
    // check if we are correctly at the end of the script
    // we check for size because we should be at 1 byte past the end
    if (index != scriptPubKeyIn.size())
    {
        return 10;
    }
    return 0;
}

/**
 * <token_id> (32 bytes)
 * <mint_baton_vout> (0 bytes or 1 byte between 0x02-0xff)
 * <additional_token_quantity> (8 byte integer)
 */

uint8_t CSLPToken::ParseBytesMint(const CScript &scriptPubKeyIn, size_t &index)
{
    uint32_t token_id_length = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, token_id_length))
    {
        return token_id_length;
    }
    if (token_id_length != 32)
    {
        return 10;
    }
    token_id = std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + token_id_length]);
    index = index + token_id_length;
    // get the length of the baton vout, 1 or 0 bytes
    uint32_t mint_baton_vout_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, mint_baton_vout_size))
    {
        return mint_baton_vout_size;
    }
    if (mint_baton_vout_size != 1 && mint_baton_vout_size != 0)
    {
        // vout size must be 0 or 1 byte
        return 11;
    }
    // if it is 1 byte, check for valid value
    if (mint_baton_vout_size == 1)
    {
        mint_baton_vout = scriptPubKeyIn[index];
        if (mint_baton_vout < 2)
        {
            // only values 0x02-0xff are valid
            return 8;
        }
        index++;
    } // parse mint_baton_vout
    uint32_t token_quantity_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, token_quantity_size))
    {
        return token_quantity_size;
    }
    std::vector<uint8_t> vquantity =
        std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + token_quantity_size]);
    if (vquantity.size() != 8)
    {
        return 10;
    }
    index = index + 8;
    additional_token_quantity = ReadBE64(&vquantity[0]);
    // check if we are correctly at the end of the script
    // we check for size because we should be at 1 byte past the end
    if (index != scriptPubKeyIn.size())
    {
        return 12;
    }
    return 0;
}

/**
 * <token_id> (32 bytes)
 * <token_output_quantity1> (required, 8 byte integer)
 * <token_output_quantity2> (optional, 8 byte integer)
 * ...
 * <token_output_quantity19> (optional, 8 byte integer)
 */

uint8_t CSLPToken::ParseBytesSend(const CScript &scriptPubKeyIn, size_t &index)
{
    uint32_t token_id_length = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, token_id_length))
    {
        return token_id_length;
    }
    if (token_id_length != 32)
    {
        return 10;
    }
    token_id = std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + token_id_length]);
    index = index + token_id_length;

    size_t scriptsize = scriptPubKeyIn.size();
    size_t bytes_remaining = scriptsize - index;
    if (bytes_remaining % 9 != 0)
    {
        return 14;
    }
    size_t outputs = bytes_remaining / 9;
    if (outputs > 19)
    {
        return 21;
    }
    // get each output
    while (outputs > 0)
    {
        uint8_t output_length = scriptPubKeyIn[index];
        if (output_length != 8)
        {
            return 15;
        }
        index++;
        token_output_quantitys.push_back(ReadBE64(&scriptPubKeyIn[index]));
        index = index + 8;
        outputs--;
    }
    // check if we are correctly at the end of the script
    // we check for size because we should be at 1 byte past the end
    if (index != scriptPubKeyIn.size())
    {
        return 16;
    }
    return 0;
}

uint8_t CSLPToken::ParseBytesCommit(const CScript &scriptPubKeyIn, size_t &index)
{
    // this is TBD, hasnt been implemented in the spec yet
    return 17;
}

uint8_t CSLPToken::_ParseBytes(const CScript &scriptPubKeyIn)
{
    size_t index = 0;
    if (scriptPubKeyIn[index] != OP_RETURN)
    {
        // not an op return
        return 1;
    }
    index++;

    uint32_t lokad_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, lokad_size))
    {
        return lokad_size;
    }
    if (lokad_size != 4)
    {
        return 2;
    }
    // check the protocol
    std::vector<uint8_t> lokad = std::vector<uint8_t>(&scriptPubKeyIn[index], &scriptPubKeyIn[index + lokad_size]);
    if (ReadLE32(&lokad[0]) != CASH_PROTOCOLS::SLP)
    {
        // not SLP cash protocol
        return 3;
    }
    index = index + lokad_size;
    // get the size of the token type
    uint8_t token_type_size = scriptPubKeyIn[index]; // can be 1 or 2 bytes
    index++;
    // read LE token_type_size bytes and put it into token_type
    if (token_type_size == 1)
    {
        token_type = 0; // << 16
        token_type = token_type | (uint8_t)scriptPubKeyIn[index];
    }
    else if (token_type_size == 2)
    {
        token_type = 0; // << 16
        token_type = (uint8_t)scriptPubKeyIn[index] << 8 | (uint8_t)scriptPubKeyIn[index + 1];
    }
    else
    {
        return 4;
    }
    // only token type 1 is valid right now
    if (token_type != 1)
    {
        return 5;
    }
    index = index + token_type_size;
    // get the size of the tx type
    // can be 4 to 7 bytes
    uint32_t tx_type_size = 0;
    if (!DetermineDynamicSize(scriptPubKeyIn, index, tx_type_size))
    {
        return tx_type_size;
    }
    // parse the tx type
    tx_type = ParseType(scriptPubKeyIn, index, tx_type_size);
    // parse the rest of the message
    if (tx_type == SLP_SEND)
    {
        return ParseBytesSend(scriptPubKeyIn, index);
    }
    else if (tx_type == SLP_MINT)
    {
        return ParseBytesMint(scriptPubKeyIn, index);
    }
    else if (tx_type == SLP_GENESIS)
    {
        return ParseBytesGenesis(scriptPubKeyIn, index);
    }
    else if (tx_type == SLP_COMMIT)
    {
        return ParseBytesCommit(scriptPubKeyIn, index);
    }
    // anything else (NULL TYPE) return an error
    return 5;
}


uint64_t CSLPToken::GetOutputAmount()
{
    if (tx_type == SLP_TX_TYPE::SLP_SEND)
    {
        uint64_t total_out = 0;
        for (auto &amount : token_output_quantitys)
        {
            total_out = total_out + amount;
        }
        return total_out;
    }
    else if (tx_type == SLP_TX_TYPE::SLP_GENESIS)
    {
        return initial_token_mint_quantity;
    }
    else if (tx_type == SLP_TX_TYPE::SLP_MINT)
    {
        return additional_token_quantity;
    }
    return 0;
}

uint64_t CSLPToken::GetOutputAmountAt(uint32_t n)
{
    // check that n is a valid value
    if (n > token_output_quantitys.size() - 1)
    {
        return 0;
    }

    // get the value
    if (tx_type == SLP_TX_TYPE::SLP_SEND)
    {
        return token_output_quantitys[n];
    }
    else if (n != 1)
    {
        // genesis and mint transactions only put new tokens in output 1
        // so if it isnt output 0 then we return 0
        return 0;
    }
    else if (tx_type == SLP_TX_TYPE::SLP_GENESIS)
    {
        return initial_token_mint_quantity;
    }
    else if (tx_type == SLP_TX_TYPE::SLP_MINT)
    {
        return additional_token_quantity;
    }
    return 0;
}

uint32_t CSLPToken::GetBatonOut()
{
    // for non mint transactions this value will be 0, which is an invalid entry
    // (token wouldnt parse) so it is fine to return it as a result
    return mint_baton_vout;
}
