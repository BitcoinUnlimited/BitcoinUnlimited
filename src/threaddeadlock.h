// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THREAD_DEADLOCK_H
#define BITCOIN_THREAD_DEADLOCK_H

#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <inttypes.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string>
#include <utility>
#include <vector>

#include "utilstrencodings.h"

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
#include <sys/syscall.h>
#ifdef __linux__
#include <sys/syscall.h>
inline uint64_t getTid(void)
{
    // "native" thread id used so the number correlates with what is shown in gdb
    pid_t tid = (pid_t)syscall(SYS_gettid);
    return tid;
}
#else
#include <functional>
inline uint64_t getTid(void)
{
    // Note: there is no guaranteed way to turn the thread-id into an int
    // since it's an opaque type. Just about the only operation it supports
    // is std::hash (so that thread id's may be placed in maps).
    // So we just do this.
    static std::hash<std::thread::id> hasher;
    return uint64_t(hasher(std::this_thread::get_id()));
}
#endif

struct CLockLocation
{
    CLockLocation(const char *pszName,
        const char *pszFile,
        int nLine,
        bool fTryIn,
        OwnershipType eOwnershipIn,
        LockType eLockTypeIn)
    {
        mutexName = pszName;
        sourceFile = pszFile;
        sourceLine = nLine;
        fTry = fTryIn;
        eOwnership = eOwnershipIn;
        eLockType = eLockTypeIn;
        fWaiting = true;
    }

    std::string ToString() const
    {
        return mutexName + "  " + sourceFile + ":" + std::to_string(sourceLine) + (fTry ? " (TRY)" : "") +
               (eOwnership == OwnershipType::EXCLUSIVE ? " (EXCLUSIVE)" : "") + (fWaiting ? " (WAITING)" : "");
    }

    bool GetTry() const { return fTry; }
    OwnershipType GetExclusive() const { return eOwnership; }
    bool GetWaiting() const { return fWaiting; }
    void ChangeWaitingToHeld() { fWaiting = false; }
    LockType GetLockType() const { return eLockType; }
    std::string GetFileName() const { return sourceFile; }
    int GetLineNumber() const { return sourceLine; }
    std::string GetMutexName() const { return mutexName; }
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

// tracks the globally seen lock ordering
// key is lockname, value is vector of locknames that have ever been locked while key was locked
typedef std::map<std::string, std::set<std::string> > SeenLockOrders;

struct LockData
{
    // Very ugly hack: as the global constructs and destructors run single
    // threaded, we use this boolean to know whether LockData still exists,
    // as DeleteLock can get called by global CCriticalSection destructors
    // after LockData disappears.
    bool available;
    LockData() : available(true) {}
    ~LockData() { available = false; }
    ReadLocksWaiting readlockswaiting;
    WriteLocksWaiting writelockswaiting;

    ReadLocksHeld readlocksheld;
    WriteLocksHeld writelocksheld;
    LocksHeldByThread locksheldbythread;
    SeenLockOrders seenlockorders;
    std::mutex dd_mutex;
};
extern LockData lockdata;

/**
 * Adds a new lock to LockData tracking
 *
 * Should only be called by EnterCritical
 */
void push_lock(void *c, const CLockLocation &locklocation, LockType locktype, OwnershipType ownership, bool fTry);

/**
 * Removes a critical section and all locks related to it from LockData
 *
 * Should only be called by a critical section destructor
 */
void DeleteCritical(void *cs);

/**
 * Removes the most recent instance of locks from LockData
 *
 * Should only be called by LeaveCritical
 */
void remove_lock_critical_exit(void *cs);

/**
 * Prints all of the locks held by the calling thread
 */
std::string LocksHeld();

/**
 * Moves a lock that is currently in one of the waiting maps to the corresponding held map
 */
void SetWaitingToHeld(void *c, OwnershipType ownership);

#else // NOT DEBUG_LOCKORDER

static inline void SetWaitingToHeld(void *c, OwnershipType ownership) {}

#endif // END DEBUG_LOCKORDER

#endif
