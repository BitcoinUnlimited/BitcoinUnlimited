// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbwrapper.h"

#include "fs.h"
#include "random.h"
#include "util.h"

#include <leveldb/cache.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include <memenv.h>
#include <stdint.h>

static void SetMaxOpenFiles(leveldb::Options *options)
{
    // On most platforms the default setting of max_open_files (which is 1000)
    // is optimal. On Windows using a large file count is OK because the handles
    // do not interfere with select() loops. On 64-bit Unix hosts this value is
    // also OK, because up to that amount LevelDB will use an mmap
    // implementation that does not use extra file descriptors (the fds are
    // closed after being mmaped).
    //
    // Increasing the value beyond the default is dangerous because LevelDB will
    // fall back to a non-mmap implementation when the file count is too large.
    // On 32-bit Unix host we should decrease the value because the handles use
    // up real fds, and we want to avoid fd exhaustion issues.
    //
    // See Bitoin Core PR #12495 for further discussion.

    int default_open_files = options->max_open_files;
#ifndef WIN32
    if (sizeof(void *) < 8)
    {
        options->max_open_files = 64;
    }
#endif
    LOGA("LevelDB using max_open_files=%d (default=%d)\n", options->max_open_files, default_open_files);
}

static leveldb::Options GetDefaultOptions(size_t nCacheSize)
{
    leveldb::Options options;
    options.block_cache = leveldb::NewLRUCache(nCacheSize / 2);
    options.write_buffer_size = nCacheSize / 4; // up to two write buffers may be held in memory simultaneously
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    options.compression = leveldb::kNoCompression;
    if (leveldb::kMajorVersion > 1 || (leveldb::kMajorVersion == 1 && leveldb::kMinorVersion >= 16))
    {
        // LevelDB versions before 1.16 consider short writes to be corruption. Only trigger error
        // on corruption in later versions.
        options.paranoid_checks = true;
    }
    options.create_if_missing = true;

    SetMaxOpenFiles(&options);
    return options;
}

static void OverrideOptions(leveldb::Options &_options, COverrideOptions *_pOverride)
{
    if (_pOverride == nullptr)
        return;

    // If an ovverride option was defined and is valid then modify the corresponding level db option.
    if (_pOverride->max_file_size > 0)
        _options.max_file_size = _pOverride->max_file_size;

    if (_pOverride->block_size  > 0)
        _options.block_size = _pOverride->block_size;

    if (_pOverride->write_buffer_size > 0)
        _options.write_buffer_size = _pOverride->write_buffer_size;
}

CDBWrapper::CDBWrapper(const fs::path &path,
    size_t nCacheSize,
    bool fMemory,
    bool fWipe,
    bool obfuscate,
    COverrideOptions *pOverride)
{
    penv = nullptr;
    readoptions.verify_checksums = true;
    iteroptions.verify_checksums = true;
    iteroptions.fill_cache = false;
    syncoptions.sync = true;
    options = GetDefaultOptions(nCacheSize);

    // Modify default database options
    OverrideOptions(options, pOverride);

    if (fMemory)
    {
        penv = leveldb::NewMemEnv(leveldb::Env::Default());
        options.env = penv;
    }
    else
    {
        if (fWipe)
        {
            LOGA("Wiping LevelDB in %s\n", path.string());
            leveldb::Status result = leveldb::DestroyDB(path.string(), options);
            dbwrapper_private::HandleError(result);
        }
        TryCreateDirectories(path);
        LOGA("Opening LevelDB in %s\n", path.string());
    }
    leveldb::Status status = leveldb::DB::Open(options, path.string(), &pdb);
    dbwrapper_private::HandleError(status);
    LOGA("Opened LevelDB successfully\n");

    // The base-case obfuscation key, which is a noop.
    obfuscate_key = std::vector<unsigned char>(OBFUSCATE_KEY_NUM_BYTES, '\000');

    bool key_exists = Read(OBFUSCATE_KEY_KEY, obfuscate_key);

    if (!key_exists && obfuscate && IsEmpty())
    {
        // Initialize non-degenerate obfuscation if it won't upset
        // existing, non-obfuscated data.
        std::vector<unsigned char> new_key = CreateObfuscateKey();

        // Write `new_key` so we don't obfuscate the key with itself
        Write(OBFUSCATE_KEY_KEY, new_key);
        obfuscate_key = new_key;

        LOGA("Wrote new obfuscate key for %s: %s\n", path.string(), HexStr(obfuscate_key));
    }

    LOGA("Using obfuscation key for %s: %s\n", path.string(), HexStr(obfuscate_key));
}

CDBWrapper::~CDBWrapper()
{
    delete pdb;
    pdb = nullptr;
    delete options.filter_policy;
    options.filter_policy = nullptr;
    delete options.block_cache;
    options.block_cache = nullptr;
    delete penv;
    options.env = nullptr;
}

bool CDBWrapper::WriteBatch(CDBBatch &batch, bool fSync)
{
    leveldb::Status status = pdb->Write(fSync ? syncoptions : writeoptions, &batch.batch);
    dbwrapper_private::HandleError(status);
    return true;
}

// Prefixed with null character to avoid collisions with other keys
//
// We must use a string constructor which specifies length so that we copy
// past the null-terminator.
const std::string CDBWrapper::OBFUSCATE_KEY_KEY("\000obfuscate_key", 14);

const unsigned int CDBWrapper::OBFUSCATE_KEY_NUM_BYTES = 8;

/**
 * Returns a string (consisting of 8 random bytes) suitable for use as an
 * obfuscating XOR key.
 */
std::vector<unsigned char> CDBWrapper::CreateObfuscateKey() const
{
    unsigned char buff[OBFUSCATE_KEY_NUM_BYTES];
    GetRandBytes(buff, OBFUSCATE_KEY_NUM_BYTES);
    return std::vector<unsigned char>(&buff[0], &buff[OBFUSCATE_KEY_NUM_BYTES]);
}

bool CDBWrapper::IsEmpty()
{
    boost::scoped_ptr<CDBIterator> it(NewIterator());
    it->SeekToFirst();
    return !(it->Valid());
}

CDBIterator::~CDBIterator() { delete piter; }
bool CDBIterator::Valid() const { return piter->Valid(); }
void CDBIterator::SeekToFirst() { piter->SeekToFirst(); }
void CDBIterator::Next() { piter->Next(); }
namespace dbwrapper_private
{
void HandleError(const leveldb::Status &status)
{
    if (status.ok())
        return;
    LOGA("%s\n", status.ToString());
    if (status.IsCorruption())
        throw dbwrapper_error("Database corrupted");
    if (status.IsIOError())
        throw dbwrapper_error("Database I/O error");
    if (status.IsNotFound())
        throw dbwrapper_error("Database entry missing");
    throw dbwrapper_error("Unknown database error");
}

const std::vector<unsigned char> &GetObfuscateKey(const CDBWrapper &w) { return w.obfuscate_key; }
};
