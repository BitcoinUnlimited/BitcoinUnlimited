#ifndef BITCOIN_BLOCKRELAY_NETDELTABLOCKS_H
#define BITCOIN_BLOCKRELAY_NETDELTABLOCKS_H

#include "serialize.h"
#include "deltablocks.h"
#include "graphene.h"

class CDataStream;

class CNetDeltaRequestMissing : public CRequestGrapheneBlockTx {
public:
    /*! Deal with an incoming network message of type DBMISSTX */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom);
};

/*! Graphene based network representation of a delta block. */
class CNetDeltaBlock {
    friend class CNetDeltaRequestMissing;
    friend bool sendFullDeltaBlock(ConstCDeltaBlockRef db, CNode* pto);
    friend bool sendDeltaBlock(ConstCDeltaBlockRef db, CNode* pto, std::set<uint64_t> requestedCheapHashes);
private:
    CBlockHeader header;
    CGrapheneSet *delta_gset;

    // total size of delta set, including coinbase
    uint64_t delta_tx_size;

    // transactions the receiver probably doesn't have - compare also CGrapheneBlock::vAdditionalTxs
    // contains the coinbase always
    std::vector<CTransactionRef> delta_tx_additional;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(header);
        READWRITE(delta_tx_size);
        if (delta_gset == nullptr) delta_gset = new CGrapheneSet(2);
        READWRITE(*delta_gset);
        READWRITE(delta_tx_additional);
    }

    // FIXME: sip hashing
    CNetDeltaBlock(ConstCDeltaBlockRef &dbref,
                   uint64_t nReceiverMemPoolTx);

    // Empty constructor used for deserialization
    CNetDeltaBlock();

    /*! Reconstruct delta block from wire format. Returns false for an
      unrecoverable error, true when it is worth trying again with more
      info. */
    bool reconstruct(CDeltaBlockRef& dbr, CNetDeltaRequestMissing** ppmissing_tx = nullptr);

    /*! Deal with an incoming network message of type DELTABLOCK */
    static bool HandleMessage(CDataStream &vRecv, CNode *pfrom);

    /*! Call this when a new delta block arrived, weak or strong. This
     *  will process it and send it around to everyone. */
    static void processNew(CDeltaBlockRef dbr, CNode *pfrom);
};


#endif
