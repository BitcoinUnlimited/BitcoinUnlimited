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

/**
 * TxIndex is used to look up transactions included in the blockchain by hash.
 * The index is written to a LevelDB database and records the filesystem
 * location of each transaction by transaction hash.
 */
class TxIndex final : public CValidationInterface
{
private:
    const std::unique_ptr<TxIndexDB> m_db;

    /// Whether the index is in sync with the main chain. The flag is flipped
    /// from false to true once, after which point this starts processing
    /// ValidationInterface notifications to stay in sync.
    std::atomic<bool> m_synced;

    /// The last block in the chain that the TxIndex is in sync with.
    std::atomic<CBlockIndex *> m_best_block_index;

    std::thread m_thread_sync;

    /// Initialize internal state from the database and block index.
    bool Init();

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
    void BlockConnected(const CBlock &block,
        CBlockIndex *pindex);

    /// Write the current chain block locator to the DB.
    bool WriteBestBlock(CBlockIndex *block_index);

    /// Constructs the TxIndex, which becomes available to be queried.
    explicit TxIndex(std::unique_ptr<TxIndexDB> db);

    /// Destructor interrupts sync thread if running and blocks until it exits.
    ~TxIndex();

    /// Blocks the current thread until the transaction index is caught up to
    /// the current state of the block chain. This only blocks if the index has gotten in sync once
    /// and only needs to process blocks in the ValidationInterface queue. If the index is catching
    /// up from far behind, this method does not block and immediately returns false.
    bool BlockUntilSyncedToCurrentChain();

    /// Look up the on-disk location of a transaction by hash.
    bool FindTx(const uint256 &txhash, uint256 &blockhash, CTransactionRef tx) const;

    /// Start initializes the sync state and registers the instance as a
    /// ValidationInterface so that it stays in sync with blockchain updates.
    void Start();

    /// Stops the instance from staying in sync with blockchain updates.
    void Stop();
};

/// The global transaction index, used in GetTransaction. May be null.
extern std::unique_ptr<TxIndex> g_txindex;

#endif // BITCOIN_INDEX_TXINDEX_H