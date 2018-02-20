// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"

#include "chain.h"
#include "chainparams.h"
#include "hash.h"
#include "main.h"
#include "pow.h"
#include "ui_interface.h"
#include "uint256.h"

#include <stdint.h>

using namespace std;

static const char DB_COIN = 'C';
static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_BEST_BLOCK = 'B';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

namespace
{
struct CoinEntry
{
    COutPoint *outpoint;
    char key;
    CoinEntry(const COutPoint *ptr) : outpoint(const_cast<COutPoint *>(ptr)), key(DB_COIN) {}
    template <typename Stream>
    void Serialize(Stream &s) const
    {
        s << key;
        s << outpoint->hash;
        s << VARINT(outpoint->n);
    }

    template <typename Stream>
    void Unserialize(Stream &s)
    {
        s >> key;
        s >> outpoint->hash;
        s >> VARINT(outpoint->n);
    }
};
}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe, true)
{
}

bool CCoinsViewDB::GetCoin(const COutPoint &outpoint, Coin &coin) const { return db.Read(CoinEntry(&outpoint), coin); }
bool CCoinsViewDB::HaveCoin(const COutPoint &outpoint) const { return db.Exists(CoinEntry(&outpoint)); }
uint256 CCoinsViewDB::GetBestBlock() const
{
    LOCK(cs_utxo);
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins,
    const uint256 &hashBlock,
    const uint64_t nBestCoinHeight,
    size_t &nChildCachedCoinsUsage)
{
    LOCK(cs_utxo);
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    size_t nBatchSize = 0;
    size_t nBatchWrites = 0;

    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();)
    {
        if (it->second.flags & CCoinsCacheEntry::DIRTY)
        {
            CoinEntry entry(&it->first);
            size_t nUsage = it->second.coin.DynamicMemoryUsage();
            if (it->second.coin.IsSpent())
            {
                batch.Erase(entry);

                // Update the usage of the child cache before deleting the entry in the child cache
                nChildCachedCoinsUsage -= nUsage;
                it = mapCoins.erase(it);
            }
            else
            {
                batch.Write(entry, it->second.coin);

                // Only delete valid coins from the cache when we're nearly syncd.  During IBD, and also
                // if BlockOnly mode is turned on, these coins will be used, whereas, once the chain is
                // syncd we only need the coins that have come from accepting txns into the memory pool.
                bool fBlocksOnly = GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);
                if (IsChainNearlySyncd() && !fImporting && !fReindex && !fBlocksOnly)
                {
                    // Update the usage of the child cache before deleting the entry in the child cache
                    nChildCachedCoinsUsage -= nUsage;
                    it = mapCoins.erase(it);
                }
                else
                {
                    it->second.flags = 0;
                    it++;
                }
            }
            changed++;

            // In order to prevent the spikes in memory usage that used to happen when we prepared large as
            // was possible, we instead break up the batches such that the performance gains for writing to
            // leveldb are still realized but the memory spikes are not seen.
            nBatchSize += nUsage;
            if (nBatchSize > nCoinCacheUsage * 0.01)
            {
                db.WriteBatch(batch);
                batch.Clear();
                nBatchSize = 0;
                nBatchWrites++;
            }
        }
        else
            it++;
        count++;
    }
    if (!hashBlock.IsNull())
        batch.Write(DB_BEST_BLOCK, hashBlock);

    bool ret = db.WriteBatch(batch);
    LOG(COINDB, "Committing %u changed transactions (out of %u) to coin database with %u batch writes...\n",
        (unsigned int)changed, (unsigned int)count, (unsigned int)nBatchWrites);
    return ret;
}

size_t CCoinsViewDB::EstimateSize() const { return db.EstimateSize(DB_COIN, (char)(DB_COIN + 1)); }
CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe)
{
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info)
{
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing)
{
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing)
{
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) { return Read(DB_LAST_BLOCK, nFile); }
CCoinsViewCursor *CCoinsViewDB::Cursor() const
{
    CCoinsViewDBCursor *i = new CCoinsViewDBCursor(const_cast<CDBWrapper *>(&db)->NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid())
    {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    }
    else
    {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN)
    {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const { return pcursor->GetValue(coin); }
unsigned int CCoinsViewDBCursor::GetValueSize() const { return pcursor->GetValueSize(); }
bool CCoinsViewDBCursor::Valid() const { return keyTmp.first == DB_COIN; }
void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry))
    {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    }
    else
    {
        keyTmp.first = entry.key;
    }
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo *> > &fileInfo,
    int nLastFile,
    const std::vector<const CBlockIndex *> &blockinfo)
{
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo *> >::const_iterator it = fileInfo.begin();
         it != fileInfo.end(); it++)
    {
        batch.Write(make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex *>::const_iterator it = blockinfo.begin(); it != blockinfo.end(); it++)
    {
        batch.Write(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) { return Read(make_pair(DB_TXINDEX, txid), pos); }
bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &vect)
{
    CDBBatch batch(*this);
    for (std::vector<std::pair<uint256, CDiskTxPos> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue)
{
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue)
{
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts()
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

    // Load mapBlockIndex
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX)
        {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex))
            {
                // Construct block index object
                CBlockIndex *pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev = InsertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight = diskindex.nHeight;
                pindexNew->nFile = diskindex.nFile;
                pindexNew->nDataPos = diskindex.nDataPos;
                pindexNew->nUndoPos = diskindex.nUndoPos;
                pindexNew->nVersion = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime = diskindex.nTime;
                pindexNew->nBits = diskindex.nBits;
                pindexNew->nNonce = diskindex.nNonce;
                pindexNew->nStatus = diskindex.nStatus;
                pindexNew->nTx = diskindex.nTx;

                if (!CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits, Params().GetConsensus()))
                    return error("LoadBlockIndex(): CheckProofOfWork failed: %s", pindexNew->ToString());

                pcursor->Next();
            }
            else
            {
                return error("LoadBlockIndex() : failed to read value");
            }
        }
        else
        {
            break;
        }
    }

    return true;
}

namespace
{
//! Legacy class to deserialize pre-pertxout database entries without reindex.
class CCoins
{
public:
    //! whether transaction is a coinbase
    bool fCoinBase;

    //! unspent transaction outputs; spent outputs are .IsNull(); spent outputs at the end of the array are dropped
    std::vector<CTxOut> vout;

    //! at which height this transaction was included in the active block chain
    int nHeight;

    //! empty constructor
    CCoins() : fCoinBase(false), vout(0), nHeight(0) {}
    template <typename Stream>
    void Unserialize(Stream &s)
    {
        unsigned int nCode = 0;
        // version
        int nVersionDummy;
        ::Unserialize(s, VARINT(nVersionDummy));
        // header code
        ::Unserialize(s, VARINT(nCode));
        fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0)
        {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail);
            for (unsigned int p = 0; p < 8; p++)
            {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++)
        {
            if (vAvail[i])
                ::Unserialize(s, REF(CTxOutCompressor(vout[i])));
        }
        // coinbase height
        ::Unserialize(s, VARINT(nHeight));
    }
};
}

/** Upgrade the database from older formats.
 *
 * Currently implemented: from the per-tx utxo model (0.8..0.14.x) to per-txout.
 */
bool CCoinsViewDB::Upgrade()
{
    std::unique_ptr<CDBIterator> pcursor(db.NewIterator());
    pcursor->Seek(std::make_pair(DB_COINS, uint256()));
    if (!pcursor->Valid())
    {
        return true;
    }

    LOGA("Upgrading database...\n");
    uiInterface.InitMessage(_("Upgrading database...this may take a while"));
    size_t batch_size = 1 << 24;
    CDBBatch batch(db);

    std::pair<unsigned char, uint256> key;
    std::pair<unsigned char, uint256> prev_key = {DB_COINS, uint256()};
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();

        if (pcursor->GetKey(key) && key.first == DB_COINS)
        {
            CCoins old_coins;
            if (!pcursor->GetValue(old_coins))
            {
                return error("%s: cannot parse CCoins record", __func__);
            }
            COutPoint outpoint(key.second, 0);
            for (size_t i = 0; i < old_coins.vout.size(); ++i)
            {
                if (!old_coins.vout[i].IsNull() && !old_coins.vout[i].scriptPubKey.IsUnspendable())
                {
                    Coin newcoin(std::move(old_coins.vout[i]), old_coins.nHeight, old_coins.fCoinBase);
                    outpoint.n = i;
                    CoinEntry entry(&outpoint);
                    batch.Write(entry, newcoin);
                }
            }
            batch.Erase(key);
            if (batch.SizeEstimate() > batch_size)
            {
                db.WriteBatch(batch);
                batch.Clear();
                db.CompactRange(prev_key, key);
                prev_key = key;
            }
            pcursor->Next();
        }
        else
        {
            break;
        }
    }
    db.WriteBatch(batch);
    db.CompactRange({DB_COINS, uint256()}, key);

    return true;
}

// For Windows we can use the current total available memory, however for other systems we can only use the
// the physical RAM in our calculations.
static uint64_t nDefaultPhysMem = 1000000000; // if we can't get RAM size then default to an assumed 1GB system memory
#ifdef WIN32
#include <windows.h>
unsigned long long GetAvailableMemory()
{
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    if (status.ullAvailPhys > 0)
    {
        return status.ullAvailPhys;
    }
    else
    {
        LOGA("Could not get size of available memory - returning with default\n");
        return nDefaultPhysMem / 2;
    }
}
unsigned long long GetTotalSystemMemory()
{
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    if (status.ullTotalPhys > 0)
    {
        return status.ullTotalPhys;
    }
    else
    {
        LOGA("Could not get size of physical memory - returning with default\n");
        return nDefaultPhysMem;
    }
}
#elif __APPLE__
#include <sys/sysctl.h>
#include <sys/types.h>
unsigned long long GetTotalSystemMemory()
{
    int mib[] = {CTL_HW, HW_MEMSIZE};
    int64_t nPhysMem = 0;
    size_t nLength = sizeof(nPhysMem);

    if (sysctl(mib, 2, &nPhysMem, &nLength, nullptr, 0))
    {
        return nPhysMem;
    }
    else
    {
        LOGA("Could not get size of physical memory - returning with default\n");
        return nDefaultPhysMem;
    }
}
#elif __unix__
#include <unistd.h>
unsigned long long GetTotalSystemMemory()
{
    long nPages = sysconf(_SC_PHYS_PAGES);
    long nPageSize = sysconf(_SC_PAGE_SIZE);
    if (nPages > 0 && nPageSize > 0)
    {
        return nPages * nPageSize;
    }
    else
    {
        LOGA("Could not get size of physical memory - returning with default\n");
        return nDefaultPhysMem;
    }
}
#else
unsigned long long GetTotalSystemMemory()
{
    LOGA("Could not get size of physical memory - returning with default\n");
    return nDefaultPhysMem; // if we can't get RAM size then default to an assumed 1GB system memory
}
#endif

void GetCacheConfiguration(int64_t &_nBlockTreeDBCache,
    int64_t &_nCoinDBCache,
    int64_t &_nCoinCacheUsage,
    bool fDefault)
{
#ifdef WIN32
    // If using WINDOWS then determine the actual physical memory that is currently available for dbcaching.
    // Alway leave 10% of the available RAM unused.
    int64_t nMemAvailable = GetAvailableMemory();
    nMemAvailable = nMemAvailable - (nMemAvailable * nDefaultPcntMemUnused / 100);
#else
    // Get total system memory but only use half.
    // - This half of system memory is only used as a basis for the total cache size
    // - if and only if the operator has not already set a value for -dbcache. This mitigates a common problem
    // - where new operators are unaware of the importance of the dbcache setting and therefore do not size their
    // - dbcache correctly resulting in a very slow initial block sync.
    int64_t nMemAvailable = GetTotalSystemMemory() / 2;
#endif

    // Convert from bytes to MiB.
    nMemAvailable = nMemAvailable >> 20;

    // nTotalCache size calculations returned in bytes (convert from MiB to bytes)
    int64_t nTotalCache = 0;
    if (fDefault)
    {
        // With the default flag set we only want the settings returned if the default dbcache were selected.
        // This is useful in that it gives us the lowest possible dbcache configuration.
        nTotalCache = nDefaultDbCache << 20;
    }
    else if (nDefaultDbCache < nMemAvailable)
    {
        // only use the dynamically calculated nMemAvailable if and only if the node operator has not set
        // a value for dbcache!
        nTotalCache = (GetArg("-dbcache", nMemAvailable) << 20);
    }
    else
    {
        nTotalCache = (GetArg("-dbcache", nDefaultDbCache) << 20);
    }

    // Now that we have the nTotalCache we can calculate all the various cache sizes.
    CacheSizeCalculations(nTotalCache, _nBlockTreeDBCache, _nCoinDBCache, _nCoinCacheUsage);
}

void CacheSizeCalculations(int64_t _nTotalCache,
    int64_t &_nBlockTreeDBCache,
    int64_t &_nCoinDBCache,
    int64_t &_nCoinCacheUsage)
{
    // make sure total cache is within limits
    _nTotalCache = std::max(_nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    _nTotalCache = std::min(_nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache

    // calculate the block index leveldb cache size. It shouldn't be larger than 2 MiB.
    // NOTE: this is not the same as the in memory block index which is fully stored in memory.
    _nBlockTreeDBCache = _nTotalCache / 8;
    if (_nBlockTreeDBCache > (1 << 21) && !GetBoolArg("-txindex", DEFAULT_TXINDEX))
        _nBlockTreeDBCache = (1 << 21);

    // use 25%-50% of the remainder for the utxo leveldb disk cache
    _nTotalCache -= _nBlockTreeDBCache;
    _nCoinDBCache = std::min(_nTotalCache / 2, (_nTotalCache / 4) + (1 << 23));

    // the remainder goes to the in-memory utxo coins cache
    _nTotalCache -= _nCoinDBCache;
    _nCoinCacheUsage = _nTotalCache;
}

void AdjustCoinCacheSize()
{
#ifdef WIN32
    static int64_t nLastDbAdjustment = 0;
    int64_t nNow = GetTimeMicros();

    if (nLastDbAdjustment == 0)
    {
        nLastDbAdjustment = nNow;
    }

    // used to determine if we had previously reduced the nCoinCacheUsage and also to tell us what the last
    // mem available was when we modified the nCoinCacheUsage.
    static int64_t nLastMemAvailable = 0;

    // If there is no dbcache setting specified by the node operator then float the dbache setting down or up
    // based on current available memory.
    if (!GetArg("-dbcache", 0) && (nNow - nLastDbAdjustment) > 60000000)
    {
        int64_t nMemAvailable = GetAvailableMemory();
        int64_t nUnusedMem = GetTotalSystemMemory() * nDefaultPcntMemUnused / 100;

        // Reduce nCoinCacheUsage if mem available drop below 10%. We don't want to constantly be
        // triggering a trim or flush every whenever the nMemAvailable crosses the threshold by just a
        // few bytes, so we'll dampen the triggering of flushing/trimming only if the threshold is crossed by 5%.
        if (nMemAvailable * 1.05 < nUnusedMem)
        {
            // Get the lowest possible default coins cache configuration possible and use this value as a limiter
            // to prevent the nCoinCacheUsage from falling below this value.
            int64_t dummyBIDiskCache, dummyUtxoDiskCache, nDefaultCoinCache = 0;
            GetCacheConfiguration(dummyBIDiskCache, dummyUtxoDiskCache, nDefaultCoinCache, true);

            nCoinCacheUsage = std::max(nDefaultCoinCache, nCoinCacheUsage - (nUnusedMem - nMemAvailable));
            LOGA("Current cache size: %ld MB, nCoinCacheUsage was reduced by %u MB\n", nCoinCacheUsage / 1000000,
                (nUnusedMem - nMemAvailable) / 1000000);
            nLastDbAdjustment = nNow;
            nLastMemAvailable = nMemAvailable;
        }

        // Increase nCoinCacheUsage if mem available increases above 10%. We don't want to constantly be
        // triggering an increase whenever the nMemAvailable crosses the threshold by just a
        // few bytes, so we'll dampen the increases by triggering only when the threshold is crossed by 5%.
        else if (nLastMemAvailable && nMemAvailable * 0.95 >= nLastMemAvailable)
        {
            // find the max coins cache possible for this configuration.  Use the max int possible for total cache
            // size to ensure you receive the max cache size possible.
            int64_t dummyBIDiskCache, dummyUtxoDiskCache, nMaxCoinCache = 0;
            CacheSizeCalculations(
                std::numeric_limits<long long>::max(), dummyBIDiskCache, dummyUtxoDiskCache, nMaxCoinCache);

            nCoinCacheUsage = std::min(nMaxCoinCache, nCoinCacheUsage + (nMemAvailable - nLastMemAvailable));
            LOGA("Current cache size: %ld MB, nCoinCacheUsage was increased by %u MB\n", nCoinCacheUsage / 1000000,
                (nMemAvailable - nLastMemAvailable) / 1000000);
            nLastDbAdjustment = nNow;
            nLastMemAvailable = nMemAvailable;
        }
    }
#endif
}
