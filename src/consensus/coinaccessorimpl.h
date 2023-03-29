#ifndef BITCOIN_CONSENSUS_COINACCESSORIMPL_H
#define BITCOIN_CONSENSUS_COINACCESSORIMPL_H

#include "consensus/tokens.h"
#include "coins.h"
#include "primitives/transaction.h"

class TokenCoinAccessorImpl : public TokenCoinAccessor {
    private:
        const CCoinsViewCache& view;
    public:

    TokenCoinAccessorImpl(const CCoinsViewCache& viewIn) : view(viewIn) {};

    std::tuple<bool, CTxOut, uint32_t> AccessCoin(const COutPoint& prevout) const override {
        READLOCK(view.cs_utxo);
        const auto& coin = view._AccessCoin(prevout);
        return std::tuple(coin.IsSpent(), coin.out, coin.nHeight);
    }

    ~TokenCoinAccessorImpl() { }
};

#endif
