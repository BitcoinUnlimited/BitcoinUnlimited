// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THREAD_DEADLOCK_H
#define BITCOIN_THREAD_DEADLOCK_H

#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <string>

#include "utilstrencodings.h"

enum LockType
{
    RECURSIVE, // CCriticalSection
    SHARED, // CSharedCriticalSection
    RECRUSIVESHARED, // CRecursiveSharedCriticalSection
};

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file
#include <sys/syscall.h>
#ifdef __linux__
#include <sys/syscall.h>
uint64_t getTid(void)
{
    // "native" thread id used so the number correlates with what is shown in gdb
    pid_t tid = (pid_t)syscall(SYS_gettid);
    return tid;
}
#else
#include <functional>
uint64_t getTid(void)
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
    CLockLocation(const char *pszName, const char *pszFile, int nLine, bool fTryIn, bool fExclusiveIn)
    {
        mutexName = pszName;
        sourceFile = pszFile;
        sourceLine = nLine;
        fTry = fTryIn;
        fExclusive = fExclusiveIn;
        fWaiting = true;
    }

    std::string ToString() const
    {
        return mutexName + "  " + sourceFile + ":" + itostr(sourceLine) + (fTry ? " (TRY)" : "") +
               (fExclusive ? " (EXCLUSIVE)" : "") + (fWaiting ? " (WAITING)" : "");
    }

    bool GetTry() const { return fTry; }
    bool GetExclusive() const { return fExclusive; }
    bool GetWaiting() const { return fWaiting; }
    void ChangeWaitingToHeld() { fWaiting = false; }
private:
    bool fTry;
    std::string mutexName;
    std::string sourceFile;
    int sourceLine;
    bool fExclusive; // signifies Exclusive Ownership, this is always true for a CCriticalSection
    bool fWaiting; // determines if lock is held or is waiting to be held
};

void push_lock(void *c, const CLockLocation &locklocation, LockType type, bool isExclusive, bool fTry);
void DeleteCritical(void *cs);
void _remove_lock_critical_exit(void *cs);
void remove_lock_critical_exit(void *cs);
std::string LocksHeld();
void SetWaitingToHeld(void *c, bool isExclusive);
bool HasAnyOwners(void *c);

#else // NOT DEBUG_LOCKORDER

static inline void SetWaitingToHeld(void *c, bool isExclusive) {}

#endif // END DEBUG_LOCKORDER

#endif
