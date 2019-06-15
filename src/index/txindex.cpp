// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "index/txindex.h"
#include "blockstorage/blockstorage.h"
#include "blockstorage/sequential_files.h"
#include "chainparams.h"
#include "init.h"
#include "tinyformat.h"
#include "ui_interface.h"
#include "util.h"
#include "validation/validation.h"

constexpr int64_t SYNC_LOG_INTERVAL = 30; // seconds
constexpr int64_t SYNC_LOCATOR_WRITE_INTERVAL = 30; // seconds

std::unique_ptr<TxIndex> g_txindex;

template <typename... Args>
static void FatalError(const char *fmt, const Args &... args)
{
    std::string strMessage = tfm::format(fmt, args...);
    LOGA("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        "Error: A fatal internal error occurred, see debug.log for details", "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
}

TxIndex::TxIndex(std::unique_ptr<TxIndexDB> db) : m_db(std::move(db)), m_synced(false), m_best_block_index(nullptr) {}
TxIndex::~TxIndex() { Stop(); }
bool TxIndex::Init()
{
    LOCK(cs_main);

    // Attempt to migrate txindex from the old database to the new one. Even if
    // chain_tip is null, the node could be reindexing and we still want to
    // delete txindex records in the old database.
    if (!m_db->MigrateData(*pblocktree, chainActive.GetLocator()))
    {
        return false;
    }

    CBlockLocator locator;
    if (!m_db->ReadBestBlock(locator))
    {
        locator.SetNull();
    }

    m_best_block_index = FindForkInGlobalIndex(chainActive, locator);
    m_synced = m_best_block_index.load() == chainActive.Tip();
    return true;
}

static const CBlockIndex *NextSyncBlock(const CBlockIndex *pindex_prev)
{
    AssertLockHeld(cs_main);

    if (!pindex_prev)
    {
        return chainActive.Genesis();
    }

    const CBlockIndex *pindex = chainActive.Next(pindex_prev);
    if (pindex)
    {
        return pindex;
    }

    return chainActive.Next(chainActive.FindFork(pindex_prev));
}

void TxIndex::ThreadSync()
{
    CBlockIndex *pindex = m_best_block_index.load();
    if (!m_synced)
    {
        auto &consensus_params = Params().GetConsensus();

        int64_t last_log_time = 0;
        int64_t last_locator_write_time = 0;
        while (true)
        {
            if (shutdown_threads.load() == true)
            {
                WriteBestBlock(pindex);
                return;
            }

            {
                LOCK(cs_main);
                const CBlockIndex *pindex_next = NextSyncBlock(pindex);
                if (!pindex_next)
                {
                    WriteBestBlock(pindex);
                    m_best_block_index = pindex;
                    m_synced = true;
                    break;
                }
                pindex = const_cast<CBlockIndex *>(pindex_next);
            }

            int64_t current_time = GetTime();
            if (last_log_time + SYNC_LOG_INTERVAL < current_time)
            {
                LOGA("Syncing txindex with block chain from height %d\n", pindex->nHeight);
                last_log_time = current_time;
            }

            if (last_locator_write_time + SYNC_LOCATOR_WRITE_INTERVAL < current_time)
            {
                WriteBestBlock(pindex);
                last_locator_write_time = current_time;
            }

            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, consensus_params))
            {
                FatalError("%s: Failed to read block %s from disk", __func__, pindex->GetBlockHash().ToString());
                return;
            }
            if (!WriteBlock(block, pindex))
            {
                FatalError(
                    "%s: Failed to write block %s to tx index database", __func__, pindex->GetBlockHash().ToString());
                return;
            }
        }
    }

    if (pindex)
    {
        LOGA("txindex is enabled at height %d\n", pindex->nHeight);
    }
    else
    {
        LOGA("txindex is enabled\n");
    }
}

bool TxIndex::WriteBlock(const CBlock &block, const CBlockIndex *pindex)
{
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    for (const auto &tx : block.vtx)
    {
        vPos.emplace_back(tx->GetHash(), pos);
        pos.nTxOffset += ::GetSerializeSize(*tx, SER_DISK, CLIENT_VERSION);
    }
    return m_db->WriteTxs(vPos);
}

bool TxIndex::WriteBestBlock(CBlockIndex *block_index)
{
    LOCK(cs_main);
    if (!m_db->WriteBestBlock(chainActive.GetLocator(block_index)))
    {
        return error("%s: Failed to write locator to disk", __func__);
    }
    return true;
}

void TxIndex::BlockConnected(const CBlock &block,
    CBlockIndex *pindex)
{
    if (!m_synced)
    {
        return;
    }

    const CBlockIndex *best_block_index = m_best_block_index.load();
    if (!best_block_index)
    {
        if (pindex->nHeight != 0)
        {
            FatalError("%s: First block connected is not the genesis block (height=%d)", __func__, pindex->nHeight);
            return;
        }
    }
    else
    {
        // Ensure block connects to an ancestor of the current best block. This should be the case
        // most of the time, but may not be immediately after the the sync thread catches up and sets
        // m_synced. Consider the case where there is a reorg and the blocks on the stale branch are
        // in the ValidationInterface queue backlog even after the sync thread has caught up to the
        // new chain tip. In this unlikely event, log a warning and let the queue clear.
        if (best_block_index->GetAncestor(pindex->nHeight - 1) != pindex->pprev)
        {
            LOGA("%s: WARNING: Block %s does not connect to an ancestor of " /* Continued */
                 "known best chain (tip=%s); not updating txindex\n",
                __func__, pindex->GetBlockHash().ToString(), best_block_index->GetBlockHash().ToString());
            return;
        }
    }

    if (WriteBlock(block, pindex))
    {
        m_best_block_index = pindex;

        if (WriteBestBlock(pindex))
        {
            error("%s: Failed to write locator to disk", __func__);
        }
    }
    else
    {
        FatalError("%s: Failed to write block %s to txindex", __func__, pindex->GetBlockHash().ToString());
        return;
    }
}

bool TxIndex::BlockUntilSyncedToCurrentChain()
{
    AssertLockNotHeld(cs_main);

    if (!m_synced)
    {
        return false;
    }

    LOGA("%s: txindex is catching up on block notifications\n", __func__);
    return true;
}

bool TxIndex::FindTx(const uint256 &txhash, uint256 &blockhash, CTransactionRef ptx) const
{
    AssertLockHeld(cs_main);

    CDiskTxPos postx;
    if (!m_db->ReadTxPos(txhash, postx))
        return false;

    CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
    if (file.IsNull())
        return error("%s: OpenBlockFile failed", __func__);
    CBlockHeader header;
    try
    {
        file >> header;
        fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
        file >> ptx;
    }
    catch (const std::exception &e)
    {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }
    blockhash = header.GetHash();
    if (ptx->GetHash() != txhash)
        return error("%s: txid mismatch", __func__);

    return true;
}
void TxIndex::Start()
{
    // Need to register this ValidationInterface before running Init(), so that
    // callbacks are not missed if Init sets m_synced to true.
    RegisterValidationInterface(this);
    if (!Init())
    {
        FatalError("%s: txindex failed to initialize", __func__);
        return;
    }

    m_thread_sync = std::thread(&TraceThread<std::function<void()> >, "txindex", std::bind(&TxIndex::ThreadSync, this));
}

void TxIndex::Stop()
{
    shutdown_threads.store(true);
    UnregisterValidationInterface(this);

    if (m_thread_sync.joinable())
    {
        m_thread_sync.join();
    }
}