// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOCK_LOCATION_H
#define BITCOIN_LOCK_LOCATION_H

#include <inttypes.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

enum LockType
{
    RECURSIVE_MUTEX, // CCriticalSection
    SHARED_MUTEX, // CSharedCriticalSection
    RECURSIVE_SHARED_MUTEX, // CRecursiveSharedCriticalSection
};

enum OwnershipType
{
    SHARED,
    EXCLUSIVE
};

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

struct CLockLocation
{
    CLockLocation(const char *pszName,
        const char *pszFile,
        int nLine,
        bool fTryIn,
        OwnershipType eOwnershipIn,
        LockType eLockTypeIn);

    std::string ToString() const;

    bool GetTry() const;
    OwnershipType GetExclusive() const;
    bool GetWaiting() const;
    void ChangeWaitingToHeld();
    LockType GetLockType() const;
    std::string GetFileName() const;
    int GetLineNumber() const;
    std::string GetMutexName() const;

private:
    bool fTry;
    std::string mutexName;
    std::string sourceFile;
    int sourceLine;
    LockType eLockType;
    OwnershipType eOwnership; // determines if shared or exclusive ownership, locktype::mutex is always exclusive
    bool fWaiting; // determines if lock is held or is waiting to be held
};

// pair ( cs : lock location )
typedef std::pair<void *, CLockLocation> LockStackEntry;
typedef std::vector<LockStackEntry> LockStack;

// cs : set of thread ids
typedef std::map<void *, std::set<uint64_t> > ReadLocksHeld;
// cs : set of thread ids
typedef std::map<void *, std::set<uint64_t> > WriteLocksHeld;

// cs : set of thread ids
typedef std::map<void *, std::set<uint64_t> > ReadLocksWaiting;
// cs : set of thread ids
typedef std::map<void *, std::set<uint64_t> > WriteLocksWaiting;

// thread id : vector of locks held (both shared and exclusive, waiting and held)
typedef std::map<uint64_t, LockStack> LocksHeldByThread;

#endif // end DEBUG_LOCKORDER

#endif
