// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tokens.h>

#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <tinyformat.h>
#include "script/interpreter.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <tuple>

namespace {

template <typename K, typename V> using Map = std::map<K, V>;
template <typename K> using Set = std::set<K>;

bool CheckTokenData(const token::OutputDataPtr &pdata, CValidationState &state) {
    if (!pdata) return true;
    if ( ! pdata->IsValidBitfield()) {
        // token has disallowed bitfield byte -- this should have been caught by unserialize but check left in
        // here for belt-and-suspenders
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-bad-bitfield", false,
                         strprintf("%s: token %s has a bad bitfield byte", __func__, pdata->ToString()));
    }
    // Check token amount is sane (must not be negative; FT-only tokens must have amount > 0, bitfield must match data)
    {
        const int64_t amt = pdata->GetAmount().getint64();
        if (amt < 0) {
            // should have been caught by deserialization code, but we keep this check in here anyway
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-amount-negative", false,
                             strprintf("%s: token %s amount negative (%d)", __func__, pdata->ToString(), amt));
        }
        if (amt == 0 && pdata->IsFungibleOnly()) {
            // this should have been caught earlier but it is illegal to spend 0 fungible tokens!
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-non-nft-amount-zero", false,
                             strprintf("%s: token %s non-nft amount is zero", __func__, pdata->ToString()));
        }
        if (bool(amt != 0) != pdata->HasAmount()) {
            // Token amount and its bitfield must not mismatch
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-amount-bitfield-mismatch", false,
                             strprintf("%s: token %s amount is non-zero but bitfield declares no amount",
                                       __func__, pdata->ToString()));
        }
    }
    // Check token commitment: bitfield must match data, plus no commitments allowed for fungible-only.
    const auto &commitment = pdata->GetCommitment();
    if (pdata->HasCommitmentLength() != !commitment.empty()) {
        // Token commitment and HasCommitmentLength bitfield mistmatch
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-commitment-bitfield-mismatch", false,
                         strprintf("%s: token %s commitment and its bitfield are inconsistent",
                                   __func__, pdata->ToString()));
    }
    if (pdata->IsFungibleOnly()) {
        if ( ! commitment.empty()) {
            // Token has no NFT (fungible only) but the commitment has data in it, this is disallowed.
            // Note: This branch cannot occur in the current codebase since bitfield-mismatch above should catch this
            //       case, however it is left in here for belt-and-suspenders.
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-fungible-with-commitment", false,
                             strprintf("%s: token %s is purely fungible with non-zero commitment", __func__,
                                       pdata->ToString()));
        }
        return true; // since this is a pure FT, the below check is not done (they are only for NFT)
    }
    if (commitment.size() > token::MAX_CONSENSUS_COMMITMENT_LENGTH) {
        // token has oversized commitment
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-commitment-oversized", false,
                         strprintf("%s: token %s has nft commitment that is oversized %d",
                                   __func__, pdata->ToString(), commitment.size()));
    }

    return true;
}

bool CheckPreActivationSanity(const CTransaction &tx, CValidationState &state, const TokenCoinAccessor &view) {
    if (tx.IsCoinBase()) {
        return true;
    }
    // Tokens not enabled yet, however we must absolutely ensure that serialized token blobs
    // *in inputs* we happen to see in a txn are unspendable!  This is important because
    // of the way we now break apart scriptPubKey if we see a PREFIX_BYTE.  We must absolutely
    // forbid spending of UTXOs that contain token data that deserialized correctly (or incorrectly).
    for (const auto &in : tx.vin) {

        bool is_spent;
        uint32_t _creation_height;
        CTxOut txout;
        std::tie(is_spent,  txout, _creation_height) = view.AccessCoin(in.prevout);

        if (is_spent) {
            // should not normally happen, but skip if tests take this branch somehow
            continue;
        }
        if (txout.tokenDataPtr || txout.HasUnparseableTokenData()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vin-tokenprefix-preactivation", false);
        }
    }
    return true;
}

} // namespace

bool CheckTxTokens(const CTransaction &tx, CValidationState &state, const TokenCoinAccessor &view,
                   const uint32_t scriptFlags, const int64_t firstTokenEnabledBlockHeight) {
    if ((scriptFlags & SCRIPT_ENABLE_TOKENS) == 0) {
        // Pre-activation we must also do some checks because of the way we changed how serialization works.
        return CheckPreActivationSanity(tx, state, view);
    }

    // Do post-activation checks

    if (tx.HasOutputsWithUnparseableTokenData()) {
        // Txn with vouts that have token::PREFIX_BYTE in scriptPubKey but the token data has failed to parse.
        // This is allowed pre-activation, but forbidden post-activation of Upgrade9.
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-tokenprefix");
    }

    if (tx.IsCoinBase()) {
        if (tx.HasTokenOutputs()) {
            // forbid coinbase txn from doing any token ops
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-coinbase-has-tokens");
        }
        // otherwise no work to do since coinbase tx never has token inputs
        return true;
    }

    try {
        // TxIds from ins with prevout index 0 that can become new categories (genesis candidates)
        Set<token::Id> potentialGenesisIds; // tokenId/txid's where the input coin had n == 0

        // Tallies of amounts seen both from input tokens spent and new genesis tokens
        Map<token::Id, token::SafeAmount> inputAmountsByCategory; // id -> sum of input tokens
        Map<token::Id, token::SafeAmount> genesisAmountsByCategory; // id -> sum of OUTPUT tokens

        // NFTs
        Set<token::Id> inputMintingIds; // set of all tokenId's seen in inputs that have the "Mint" capability (0x01)
        Map<token::Id, size_t> inputMutables; // id -> count map seen in inputs that have "Mutable" capability (0x02)
        // Note: for `NFT`, we use a reference here to save on redundant copying. Assumption is that all the
        // commitments in the ins are unchanging heap data (true assumption currently).
        using NFT = std::tuple<const token::NFTCommitment &>;
        Map<token::Id, Map<NFT, size_t>> inputImmutables; // id -> NFT -> count seen in inputs

        // Scan the inputs, tallying amounts seen and NFTs seen
        for (auto const& in : tx.vin) {
            const auto &prevout = in.prevout;
            bool is_spent;
            uint64_t creation_height;
            CTxOut txout;
            std::tie(is_spent, txout, creation_height) = view.AccessCoin(prevout);
            if (is_spent) {
                // this should already be checked for us in Consensus::CheckTxInputs() but we can be paranoid here
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                                 strprintf("%s: inputs missing/spent", __func__));
            }

            const auto &pdata = txout.tokenDataPtr;

            if (txout.HasUnparseableTokenData()) {
                // Blanket consensus rule post-activation: disallow any inputs that had token::PREFIX_BYTE as the first
                // byte but that didn't parse ok as token data.
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-vin-tokenprefix");
            }

            if (pdata && int64_t(creation_height) < firstTokenEnabledBlockHeight) {
                // Disallow UTXOs that had PREFIX_BYTE and which parsed correctly as token data
                // but which were created *before* upgrade9 activated.
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-vin-token-created-pre-activation");
            }

            if (prevout.n== 0) { // mark potential genesis inputs (inputs that have prevout.n == 0)
                if ( ! potentialGenesisIds.insert(token::Id{prevout.hash}).second) {
                    // should never happen -- means a dupe input
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
                }
            }

            if (pdata) {
                // tally input tokens seen
                if ( ! CheckTokenData(pdata, state)) { //! check basic consensus sanity
                    return false; // state set by CheckTokenData
                }

                const auto &id = pdata->GetId();
                const int64_t amt = pdata->GetAmount().getint64();
                auto & catAmount = inputAmountsByCategory[id];
                catAmount = catAmount.safeAdd(amt).value(); // .value() will throw if overflow

                // remember NFTs
                if (pdata->HasNFT()) {
                    if (pdata->IsImmutableNFT()) {
                        // Increment counter for this exact immutable NFT seen
                        ++inputImmutables[id][NFT{pdata->GetCommitment()}];
                    } else if (pdata->IsMutableNFT()) {
                        // For mutable (editable) NFTs, we don't need to remember the exact NFT data since
                        // the output can create a new NFT of at most the same capability.
                        ++inputMutables[id];
                    } else if (pdata->IsMintingNFT()) {
                        // Remember that we have a minting input for this id. Minting tokens can create
                        // any number of tokens of the same rank or lower (it's like root access for tokens).
                        inputMintingIds.insert(id);
                    } else {
                        assert(false);  // this should never be reached and indicates a programming error.
                    }
                }
            }
        }

        // Scan outputs, handle spends and genesis tallies, and NFT ownership transfer
        for (auto const& out : tx.vout) {
            const auto &pdata = out.tokenDataPtr;
            if (!pdata) continue;
            // Check token consensus sanity (amount + everything else)
            if ( ! CheckTokenData(pdata, state)) {
                return false; // state set by CheckTokenData
            }
            const auto &id = pdata->GetId();
            const int64_t amt = pdata->GetAmount().getint64();

            // find the category, and debit/credit the amount
            using SafeAddMemberPtr = std::optional<token::SafeAmount> (token::SafeAmount::*)(int64_t) const;
            SafeAddMemberPtr pmember{}; // pointer to member either safeAdd or safeSub
            token::SafeAmount *tally{};
            bool isGenesis = false;
            if (auto it = inputAmountsByCategory.find(id); it != inputAmountsByCategory.end()) {
                tally = &it->second; // we tally from the inputAmountsByCategoryId
                pmember = &token::SafeAmount::safeSub; // for non-genesis we subtract and must not reach <0
            }
            if (auto it = potentialGenesisIds.find(id); it != potentialGenesisIds.end()) {
                if (tally != nullptr) {
                    // this should never happen -- a genesis txid equals a previous token id!!
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-dupe-genesis", false,
                                     strprintf("%s: token %s has a duped genesis", __func__, pdata->ToString()));
                }
                tally = &genesisAmountsByCategory[id]; // creates a new SafeAmount(0) if not exist for this id
                pmember = &token::SafeAmount::safeAdd; // for genesis we just sum the amounts to ensure no overflow
                isGenesis = true;
            }
            if (!tally || !pmember) {
                // illegal spend, invalid category
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-invalid-category", false);
            }
            *tally = (tally->*pmember)(amt).value(); // tally total of amt; .value() will throw if overflow
            if (tally->getint64() < 0) {
                // spent more fungibles of this category than was put into the txn
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-in-belowout", false,
                                 strprintf("%s: token (%s) value in < value out", __func__, pdata->ToString()));
            }

            // Handle nft ownership transfer for non-genesis. Genesis can create any number of NFTs so nothing
            // needs to be checked. But for non-genesis we must consult the input sets to enforce rules.
            if (pdata->HasNFT() && ! isGenesis) {
                bool found{};

                // First search immutables (lowest capability) and spend those first, but only if token rank is
                // immutable as well.
                if (pdata->IsImmutableNFT()) {
                    if (auto it0 = inputImmutables.find(id); it0 != inputImmutables.end()) {
                        auto & map = it0->second;
                        const NFT nft{pdata->GetCommitment()};
                        if (auto it1 = map.find(nft); it1 != map.end() && it1->second > 0) {
                            --it1->second; // "spend" this immutable by decreasing its count
                            found = true;
                        }
                    }
                }

                // Next, if not found there, search editables (spend those next), but only if the output is
                // < minting in capability.
                if ( ! found && ! pdata->IsMintingNFT()) {
                    if (auto it = inputMutables.find(id); it != inputMutables.end() && it->second > 0) {
                        --it->second; // "spend" this mutable by decreasing its count
                        found = true;
                    }
                }

                // Lastly, consult the minting Id set (minting tokens can mint any number of new tokens with same Id
                // of any rank)
                if ( ! found) {
                    if (auto it = inputMintingIds.find(id); it != inputMintingIds.end()) {
                        found = true;
                    }
                }

                if ( ! found) {
                    // cannot find this NFT -- illegal ex-nihilo NFT spend
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-nft-ex-nihilo", false,
                                     strprintf("%s: token (%s) nft output cannot be created out of thin air",
                                               __func__, pdata->ToString()));
                }
            }
        }

    } catch (const std::bad_optional_access &) {
        // can only happen when our SafeAmount overflows
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-amount-overflow", false);
    }

    return true;
}
