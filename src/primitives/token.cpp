// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/token.h>
#include <streams.h>
#include <tinyformat.h>
#include <utilstrencodings.h>

#include <cassert>

namespace token {

#define TOKEN_IMPLEMENT_SER_EXC(name) \
    /* virtual */ name::~name() {}

// We need to add these here to prevent vtable linker warnings on some platforms.
TOKEN_IMPLEMENT_SER_EXC(AmountOutOfRangeError);
TOKEN_IMPLEMENT_SER_EXC(InvalidBitfieldError);
TOKEN_IMPLEMENT_SER_EXC(AmountMustNotBeZeroError);
TOKEN_IMPLEMENT_SER_EXC(CommitmentMustNotBeEmptyError);

#undef TOKEN_IMPLEMENT_SER_EXC

std::string OutputData::ToString(bool fVerbose) const {
    std::string idHex = id.ToString();
    std::string commitmentHex = HexStr(commitment.begin(), commitment.end());
    if (!fVerbose) {
        idHex = idHex.substr(0, 30);
        commitmentHex = commitmentHex.substr(0, 30);
    }
    return strprintf("token::OutputData(id=%s, bitfield=%x, amount=%i, commitment=%s)",
                     idHex, bitfield, amount.getint64(), commitmentHex);
}

void WrapScriptPubKey(WrappedScriptPubKey &wspk, const OutputDataPtr &tokenData, const CScript &scriptPubKey,
                      int nVersion) {
    if (tokenData) {
        wspk.clear();
        GenericVectorWriter vw(SER_NETWORK, nVersion, wspk, 0);
        vw << static_cast<uint8_t>(PREFIX_BYTE);
        vw << *tokenData;
        vw << Span{scriptPubKey};

    } else {
        // no token data, the WrappedScriptPubKey just contains the entire scriptPubKey bytes
        wspk.assign(scriptPubKey.begin(), scriptPubKey.end());
    }
}

void UnwrapScriptPubKey(const WrappedScriptPubKey &wspk, OutputDataPtr &tokenDataOut, CScript &scriptPubKeyOut,
                        int nVersion, bool throwIfUnparseableTokenData) {
    ssize_t token_data_size = 0;
    if (!wspk.empty() && wspk.front() == PREFIX_BYTE) {
        // Token data prefix encountered, so we deserialize the beginning of the CScript bytes as
        // OutputData. The format is: PFX_OUTPUT token_data real_script
        try {

            GenericVectorReader vr(SER_NETWORK, nVersion, wspk, 0);
            uint8_t prefix_byte;
            vr >> prefix_byte; // eat the prefix byte
            if (!tokenDataOut) tokenDataOut.emplace();
            vr >> *tokenDataOut; // deserialize the token_data
            // tally up the size of the bytes we just deserialized
            token_data_size = static_cast<ssize_t>(wspk.size()) - static_cast<ssize_t>(vr.size());
            assert(token_data_size > 0 && token_data_size <= static_cast<ssize_t>(wspk.size())); // sanity check
        } catch (const std::ios_base::failure &e) {
            last_unwrap_exception = e; // save this value for (some) tests
            if (throwIfUnparseableTokenData) {
                // for other tests, bubble exception out
                throw;
            }
            // Non-tests:
            //
            // Tolerate failure to deserialize stuff that has the PREFIX_BYTE but is badly formatted,
            // so that we don't fork ourselves off the network.
            //
            // We will fall-through to code at the end of the function which will just assign the
            // entire wspk blob to scriptPubKeyOut
            token_data_size = 0;
            tokenDataOut.reset();
        }
    } else {
        tokenDataOut.reset();
    }
    // grab the real script which is all the leftover bytes
    scriptPubKeyOut.assign(wspk.begin() + token_data_size, wspk.end());
}

thread_local std::optional<std::ios_base::failure> last_unwrap_exception;

} // namespace token
