// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "blockstorage/dbabstract.h"
#include "chain.h"
#include "coins.h"
#include "dbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockFileInfo;
class CBlockIndex;
class uint256;

static const bool DEFAULT_TXINDEX = false;

//! The max allowed size of the in memory UTXO cache which can also be dynamically adjusted
//! (if it has been configured) based on the current availability of memory.
extern std::atomic<int64_t> nCoinCacheMaxSize;
//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 500;
//! max. -dbcache in (MiB)
static const int64_t nMaxDbCache = sizeof(void *) > 4 ? 32736 : 2048;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;
//! % of available memory to leave unused by dbcache if/when we dynamically size the dbcache.
static const int64_t nDefaultPcntMemUnused = 10;
//! max increase in cache size since the last time we did a full flush
static const uint64_t nMaxCacheIncreaseSinceLastFlush = 512 * 1000 * 1000;
/** The cutoff dbcache size where a node becomes a high performance node and will keep all unspent coins in cache
 *  after each block is processed. Lower performance nodes will purge these unspent coins from each block and
 *  instead only keep coins in cache from incoming transactions that have been fully validated which gives these lower
 *  performance and more marginal nodes, such as those run on rapberry pi's, a very small memory footprint.
 */
static const int64_t DEFAULT_HIGH_PERF_MEM_CUTOFF = 2048 * 1000 * 1000;
//! the minimum system memory we always keep free when doing automatic dbcache sizing
static const uint64_t nMinMemToKeepAvailable = 300 * 1000 * 1000;
//! the max size a batch can get before a write to the utxo is made
static const size_t nMaxDBBatchSize = 16 << 20;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static const int64_t nMaxBlockDBCache = 2;

/** Get the current available memory */
uint64_t GetAvailableMemory();
/** Get the total physical memory */
uint64_t GetTotalSystemMemory();

/** Storage allocation settings are helod here for the various database caches
 * @param nBlockDBCache       The total database size for the block read/write caches, used in blocksdb
 * @param nBlockUndoDBCache   The total database size for the block undo read/write caches, used in blocksdb
 * @param nBlockTreeDBCache   The total database size for the block index read/write caches
 * @param nBlockTxIndexCache  The total database size for the transaction index read/write caches
 * @param nCoinDBCache        The total database size for the on disk utxo read/write caches
 * NOTE: the UTXO in memory cache size is a global var and so is not held in this struct
 */
struct CacheConfig
{
    int64_t nBlockDBCache;
    int64_t nBlockUndoDBCache;
    int64_t nBlockTreeDBCache;
    int64_t nTxIndexCache;
    int64_t nCoinDBCache;

    CacheConfig() : nBlockDBCache(0), nBlockUndoDBCache(0), nBlockTreeDBCache(0), nTxIndexCache(0), nCoinDBCache(0) {}
};

/** Discover the sizes for each of the caches. This is done during init.cpp on startup but also
 *  later, during dynamic sizing of the coins cache, when need to know the initial startup values.
 */
CacheConfig DiscoverCacheConfiguration(bool fDefault = false);

/** Calculate the various cache sizes. This is primarily used in DisoverCacheConfiguration() however during
 *  dynamic sizing of the coins cache we also need to use this function directly.
 */
CacheConfig CacheSizeCalculations(int64_t _nTotalCache);

/** This function is called during FlushStateToDisk.  The coins cache is dynamically sized before any
 *  checking is done for cache flushing and trimming
 */
void AdjustCoinCacheSize();

struct CDiskTxPos : public CDiskBlockPos
{
    unsigned int nTxOffset; // after header

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(*(CDiskBlockPos *)this);
        READWRITE(VARINT(nTxOffset));
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, unsigned int nTxOffsetIn)
        : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn)
    {
    }

    CDiskTxPos() { SetNull(); }
    void SetNull()
    {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }
};

class CCoinsViewDBCursor;

/** CCoinsView backed by the coin database (chainstate/) */
class CCoinsViewDB : public CCoinsView
{
protected:
    CDBWrapper db;

public:
    CCoinsViewDB(size_t nCacheSize,
        bool fMemory = false,
        bool fWipe = false,
        bool fObfuscate = false,
        COverrideOptions *overridecache = nullptr);

    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const;
    uint256 _GetBestBlock() const override;
    uint256 GetBestBlock(BlockDBMode mode) const;
    uint256 _GetBestBlock(BlockDBMode mode) const;
    void WriteBestBlock(const uint256 &hashBlock);
    void _WriteBestBlock(const uint256 &hashBlock);
    void WriteBestBlock(const uint256 &hashBlock, BlockDBMode mode);
    void _WriteBestBlock(const uint256 &hashBlock, BlockDBMode mode);
    bool BatchWrite(CCoinsMap &mapCoins,
        const uint256 &hashBlock,
        const uint64_t nBestCoinHeight,
        size_t &nChildCachedCoinsUsage) override;
    CCoinsViewCursor *Cursor() const override;

    //! Attempt to update from an older database format. Returns whether an error occurred.
    bool Upgrade();
    size_t EstimateSize() const override;

    //! Return the current memory allocated for the write buffers
    size_t TotalWriteBufferSize() const;
};

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor : public CCoinsViewCursor
{
public:
    ~CCoinsViewDBCursor() {}
    bool GetKey(COutPoint &key) const;
    bool GetValue(Coin &coin) const;
    unsigned int GetValueSize() const;

    bool Valid() const;
    void Next();

private:
    CCoinsViewDBCursor(CDBIterator *pcursorIn, const uint256 &hashBlockIn)
        : CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn)
    {
    }
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDBWrapper
{
public:
    CBlockTreeDB(size_t nCacheSize, std::string folder, bool fMemory = false, bool fWipe = false);

private:
    CBlockTreeDB(const CBlockTreeDB &);
    void operator=(const CBlockTreeDB &);

public:
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo *> > &fileInfo,
        int nLastFile,
        const std::vector<const CBlockIndex *> &blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool FindBlockIndex(uint256 blockhash, CDiskBlockIndex *index);
    bool LoadBlockIndexGuts();
    bool GetSortedHashIndex(std::vector<std::pair<int, CDiskBlockIndex> > &hashesByHeight);
};

/** Global variable that points to the coins database */
extern CCoinsViewDB *pcoinsdbview;

/**
 * Access to the txindex database (indexes/txindex/)
 *
 * The database stores a block locator of the chain the database is synced to
 * so that the TxIndex can efficiently determine the point it last stopped at.
 * A locator is used instead of a simple hash of the chain tip because blocks
 * and block index entries may not be flushed to disk until after this database
 * is updated.
 */
class TxIndexDB : public CDBWrapper
{
public:
    explicit TxIndexDB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    /// Read the disk location of the transaction data with the given hash. Returns false if the
    /// transaction hash is not indexed.
    bool ReadTxPos(const uint256 &txid, CDiskTxPos &pos) const;

    /// Write a batch of transaction positions to the DB.
    bool WriteTxs(const std::vector<std::pair<uint256, CDiskTxPos> > &v_pos);

    /// Read block locator of the chain that the txindex is in sync with.
    bool ReadBestBlock(CBlockLocator &locator) const;

    /// Write block locator of the chain that the txindex is in sync with.
    bool WriteBestBlock(const CBlockLocator &locator);

    /// Migrate txindex data from the block tree DB, where it may be for older nodes that have not
    /// been upgraded yet to the new database.
    bool MigrateData(CBlockTreeDB &block_tree_db, const CBlockLocator &best_locator);
};
#endif // BITCOIN_TXDB_H
