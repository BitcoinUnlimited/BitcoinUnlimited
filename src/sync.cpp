// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sync.h"

#include "util.h"
#include "utilstrencodings.h"

#include <boost/thread/tss.hpp> // for boost::thread_specific_ptr
#include <stdio.h>
#include <thread>

#ifdef DEBUG_LOCKCONTENTION
void PrintLockContention(const char *pszName, const char *pszFile, unsigned int nLine)
{
    LOGA("LOCKCONTENTION: %s\n", pszName);
    LOGA("Locker: %s:%d\n", pszFile, nLine);
}
#endif /* DEBUG_LOCKCONTENTION */

#ifdef DEBUG_LOCKORDER

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

//
// Early deadlock detection.
// Problem being solved:
//    Thread 1 locks  A, then B, then C
//    Thread 2 locks  D, then C, then A
//     --> may result in deadlock between the two threads, depending on when they run.
// Solution implemented here:
// Keep track of pairs of locks: (A before B), (A before C), etc.
// Complain if any thread tries to lock in a different order.
//

// BU move to sync.h because I need to create these in globals.cpp
// struct CLockLocation {
CLockLocation::CLockLocation(const char *pszName, const char *pszFile, unsigned int nLine, bool fTryIn)
{
    mutexName = pszName;
    sourceFile = pszFile;
    sourceLine = nLine;
    fTry = fTryIn;
}

std::string CLockLocation::ToString() const
{
    return mutexName + "  " + sourceFile + ":" + itostr(sourceLine) + (fTry ? " (TRY)" : "");
}

std::string CLockLocation::MutexName() const { return mutexName; }
#if 0
    bool fTry;
private:
    std::string mutexName;
    std::string sourceFile;
    int sourceLine;
};
#endif

// BU move to .h: typedef std::vector<std::pair<void*, CLockLocation> > LockStack;

// BU control the ctor/dtor ordering
extern boost::mutex dd_mutex;
extern LockStackMap lockorders;
extern boost::thread_specific_ptr<LockStack> lockstack;


static void potential_deadlock_detected(const std::pair<void *, void *> &mismatch,
    const LockStack &s1,
    const LockStack &s2)
{
    // We attempt to not assert on probably-not deadlocks by assuming that
    // a try lock will immediately have otherwise bailed if it had
    // failed to get the lock
    // We do this by, for the locks which triggered the potential deadlock,
    // in either lockorder, checking that the second of the two which is locked
    // is only a TRY_LOCK, ignoring locks if they are reentrant.
    bool firstLocked = false;
    bool secondLocked = false;
    bool onlyMaybeDeadlock = false;

    LOGA("POTENTIAL DEADLOCK DETECTED\n");
    LOGA("Previous lock order was:\n");
    for (const PAIRTYPE(void *, CLockLocation) & i : s2)
    {
        if (i.first == mismatch.first)
        {
            LOGA(" (1)");
            if (!firstLocked && secondLocked && i.second.fTry)
                onlyMaybeDeadlock = true;
            firstLocked = true;
        }
        if (i.first == mismatch.second)
        {
            LOGA(" (2)");
            if (!secondLocked && firstLocked && i.second.fTry)
                onlyMaybeDeadlock = true;
            secondLocked = true;
        }
        LOGA(" %s\n", i.second.ToString());
    }
    firstLocked = false;
    secondLocked = false;
    LOGA("Current lock order is:\n");
    for (const PAIRTYPE(void *, CLockLocation) & i : s1)
    {
        if (i.first == mismatch.first)
        {
            LOGA(" (1)");
            if (!firstLocked && secondLocked && i.second.fTry)
                onlyMaybeDeadlock = true;
            firstLocked = true;
        }
        if (i.first == mismatch.second)
        {
            LOGA(" (2)");
            if (!secondLocked && firstLocked && i.second.fTry)
                onlyMaybeDeadlock = true;
            secondLocked = true;
        }
        LOGA(" %s\n", i.second.ToString());
    }
    assert(onlyMaybeDeadlock);
}

static void push_lock(void *c, const CLockLocation &locklocation, bool fTry)
{
    if (lockstack.get() == nullptr)
        lockstack.reset(new LockStack);

    dd_mutex.lock();

    (*lockstack).push_back(std::make_pair(c, locklocation));
    // If this is a blocking lock operation, we want to make sure that the locking order between 2 mutexes is consistent
    // across the program
    if (!fTry)
    {
        for (const PAIRTYPE(void *, CLockLocation) & i : (*lockstack))
        {
            if (i.first == c)
                break;

            std::pair<void *, void *> p1 = std::make_pair(i.first, c);
            // If this order has already been placed into the order map, we've already tested it
            if (lockorders.count(p1))
                continue;
            lockorders[p1] = (*lockstack);
            // check to see if the opposite order has ever occurred, if so flag a possible deadlock
            std::pair<void *, void *> p2 = std::make_pair(c, i.first);
            if (lockorders.count(p2))
                potential_deadlock_detected(p1, lockorders[p1], lockorders[p2]);
        }
    }
    dd_mutex.unlock();
}

static void pop_lock()
{
    dd_mutex.lock();
    (*lockstack).pop_back();
    dd_mutex.unlock();
}

void EnterCritical(const char *pszName, const char *pszFile, unsigned int nLine, void *cs, bool fTry)
{
    push_lock(cs, CLockLocation(pszName, pszFile, nLine, fTry), fTry);
}

void LeaveCritical() { pop_lock(); }
void DeleteCritical(const void *cs)
{
    dd_mutex.lock();
    LockStackMap::iterator prev = lockorders.end();
    for (LockStackMap::iterator i = lockorders.begin(); i != lockorders.end(); ++i)
    {
        // if prev is valid and one of its locks is the one we are deleting, then erase the entry
        if ((prev != lockorders.end()) && ((prev->first.first == cs) || (prev->first.second == cs)))
        {
            lockorders.erase(prev);
        }
        prev = i;
    }
    // get the last one
    if ((prev != lockorders.end()) && ((prev->first.first == cs) || (prev->first.second == cs)))
    {
        lockorders.erase(prev);
    }

    dd_mutex.unlock();
}


std::string LocksHeld()
{
    std::string result;
    for (const PAIRTYPE(void *, CLockLocation) & i : *lockstack)
        result += i.second.ToString() + std::string("\n");
    return result;
}

void AssertLockHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs)
{
    for (const PAIRTYPE(void *, CLockLocation) & i : *lockstack)
        if (i.first == cs)
            return;
    fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
        LocksHeld().c_str());
    abort();
}

void AssertLockNotHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs)
{
    for (const std::pair<void *, CLockLocation> &i : *lockstack)
    {
        if (i.first == cs)
        {
            fprintf(stderr, "Assertion failed: lock %s held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
                LocksHeld().c_str());
            abort();
        }
    }
}

void AssertWriteLockHeldInternal(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    CSharedCriticalSection *cs)
{
    if (cs->try_lock()) // It would be better to check that this thread has the lock
    {
        fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
            LocksHeld().c_str());
        fflush(stderr);
        abort();
    }
}

void AssertRecursiveWriteLockHeldInternal(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    CRecursiveSharedCriticalSection *cs)
{
    if (cs->try_lock()) // It would be better to check that this thread has the lock
    {
        fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
            LocksHeld().c_str());
        fflush(stderr);
        abort();
    }
}

// BU normally CCriticalSection is a typedef, but when lockorder debugging is on we need to delete the critical
// section from the lockorder map
#ifdef DEBUG_LOCKORDER
CCriticalSection::CCriticalSection() : name(nullptr) {}
CCriticalSection::CCriticalSection(const char *n) : name(n)
{
// print the address of named critical sections so they can be found in the mutrace output
#ifdef ENABLE_MUTRACE
    if (name)
    {
        printf("CCriticalSection %s at %p\n", name, this);
        fflush(stdout);
    }
#endif
}

CCriticalSection::~CCriticalSection()
{
#ifdef ENABLE_MUTRACE
    if (name)
    {
        printf("Destructing %s\n", name);
        fflush(stdout);
    }
#endif
    DeleteCritical((void *)this);
}
#endif

// BU normally CSharedCriticalSection is a typedef, but when lockorder debugging is on we need to delete the critical
// section from the lockorder map
#ifdef DEBUG_LOCKORDER
CSharedCriticalSection::CSharedCriticalSection() : name(nullptr), exclusiveOwner(0) {}
CSharedCriticalSection::CSharedCriticalSection(const char *n) : name(n), exclusiveOwner(0)
{
// print the address of named critical sections so they can be found in the mutrace output
#ifdef ENABLE_MUTRACE
    if (name)
    {
        printf("CSharedCriticalSection %s at %p\n", name, this);
        fflush(stdout);
    }
#endif
}

CSharedCriticalSection::~CSharedCriticalSection()
{
#ifdef ENABLE_MUTRACE
    if (name)
    {
        printf("Destructing CSharedCriticalSection %s\n", name);
        fflush(stdout);
    }
#endif
    DeleteCritical((void *)this);
}
#endif


void CSharedCriticalSection::lock_shared()
{
    uint64_t tid = getTid();
    // detect recursive locking
    {
        std::unique_lock<std::mutex> lock(setlock);
        assert(exclusiveOwner != tid);
        auto alreadyLocked = sharedowners.find(tid);
        if (alreadyLocked != sharedowners.end())
        {
            LockInfo li = alreadyLocked->second;
            printf("already locked at %s:%d\n", li.file, li.line);
            assert(alreadyLocked == sharedowners.end());
        }
        sharedowners[tid] = LockInfo("", 0);
    }
    boost::shared_mutex::lock_shared();
}

void CSharedCriticalSection::unlock_shared()
{
    // detect recursive locking
    uint64_t tid = getTid();
    {
        std::unique_lock<std::mutex> lock(setlock);
        auto alreadyLocked = sharedowners.find(tid);
        if (alreadyLocked == sharedowners.end())
        {
            LockInfo li = alreadyLocked->second;
            printf("never locked at %s:%d\n", li.file, li.line);
            assert(alreadyLocked != sharedowners.end());
        }
        sharedowners.erase(tid);
    }
    boost::shared_mutex::unlock_shared();
}

bool CSharedCriticalSection::try_lock_shared()
{
    // detect recursive locking
    uint64_t tid = getTid();
    std::unique_lock<std::mutex> lock(setlock);
    assert(exclusiveOwner != tid);
    assert(sharedowners.find(tid) == sharedowners.end());

    bool result = boost::shared_mutex::try_lock_shared();
    if (result)
    {
        sharedowners[tid] = LockInfo("", 0);
    }
    return result;
}
void CSharedCriticalSection::lock()
{
    boost::shared_mutex::lock();
    exclusiveOwner = getTid();
}
void CSharedCriticalSection::unlock()
{
    uint64_t tid = getTid();
    assert(exclusiveOwner == tid);
    exclusiveOwner = 0;
    boost::shared_mutex::unlock();
}

bool CSharedCriticalSection::try_lock()
{
    bool result = boost::shared_mutex::try_lock();
    if (result)
    {
        exclusiveOwner = getTid();
    }
    return result;
}

#endif /* DEBUG_LOCKORDER */
