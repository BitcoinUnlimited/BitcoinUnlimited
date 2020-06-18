// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INDEX_TXINDEX_H
#define BITCOIN_INDEX_TXINDEX_H

#include "primitives/block.h"
#include "txdb.h"
#include "uint256.h"
#include "validationinterface.h"

class CBlockIndex;

bool IsTxIndexReady();

/**
 * TxIndex is used to look up transactions included in the blockchain by hash.
 * The index is written to a LevelDB database and records the filesystem
 * location of each transaction by transaction hash.
 */
class TxIndex final : public CValidationInterface
{
private:
    const std::unique_ptr<TxIndexDB> db;

    /// Whether the index is in sync with the main chain. The flag is flipped
    /// from false to true once, after which point this starts processing
    /// ValidationInterface notifications to stay in sync.
    std::atomic<bool> fSynced;

    /// The last block in the chain that the TxIndex is in sync with.
    std::atomic<CBlockIndex *> pbestindex;

    std::thread syncthread;

    /// Initialize internal state from the database and block index.
    bool Init();

    /// Write the genesis transaction to the txindex
    bool WriteGenesisTransaction();

    /// Sync the tx index with the block index starting from the current best
    /// block. Intended to be run in its own thread, m_thread_sync, and can be
    /// interrupted with shutdown_threads.store(true). Once the txindex gets in sync, the
    /// m_synced flag is set and the BlockConnected ValidationInterface callback
    /// takes over and the sync thread exits.
    void ThreadSync();

    /// Write update index entries for a newly connected block.
    bool WriteBlock(const CBlock &block, const CBlockIndex *pindex);

public:
    /// Update the txindex with this newly connected block data
    void BlockConnected(const CBlock &block, CBlockIndex *pindex);

    /// Write the current chain block locator to the DB.
    bool WriteBestBlock(CBlockIndex *block_index);

    /// Constructs the TxIndex, which becomes available to be queried.
    explicit TxIndex(TxIndexDB *db);

    /// Destructor interrupts sync thread if running and blocks until it exits.
    ~TxIndex();

    /// Is the transaction index is caught up to the current state of the block chain.
    bool IsSynced();

    /// Look up the on-disk location of a transaction by hash.
    /// @returns A reference to the transaction, and the epoch time it was confirmed
    bool FindTx(const uint256 &txhash, uint256 &blockhash, CTransactionRef &ptx, int32_t &time) const;

    /// Start initializes the sync state and registers the instance as a
    /// ValidationInterface so that it stays in sync with blockchain updates.
    void Start();

    /// Stops the instance from staying in sync with blockchain updates.
    void Stop();
};

/// The global transaction index, used in GetTransaction. May be null.
extern std::unique_ptr<TxIndex> g_txindex;

#endif // BITCOIN_INDEX_TXINDEX_H
