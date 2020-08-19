// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SYNC_H
#define BITCOIN_SYNC_H

#include "deadlock-detection/threaddeadlock.h"
#include "recursive_shared_mutex.h"
#include "threadsafety.h"
#include "util.h"
#include "utiltime.h"

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/thread.hpp>


/**
 * Template mixin that adds -Wthread-safety locking
 * annotations to a subset of the mutex API.
 */
template <typename PARENT>
class LOCKABLE AnnotatedMixin : public PARENT
{
public:
    void lock() EXCLUSIVE_LOCK_FUNCTION() { PARENT::lock(); }
    void unlock() UNLOCK_FUNCTION() { PARENT::unlock(); }
    bool try_lock() EXCLUSIVE_TRYLOCK_FUNCTION(true) { return PARENT::try_lock(); }
};

/**
 * Wrapped boost mutex: supports recursive locking, but no waiting
 * TODO: We should move away from using the recursive lock by default.
 */
#ifndef DEBUG_LOCKORDER
typedef AnnotatedMixin<boost::recursive_mutex> CCriticalSection;
#define CRITSEC(x) CCriticalSection x
#else // BU we need to remove the critical section from the lockorder map when destructed
class CCriticalSection : public AnnotatedMixin<boost::recursive_mutex>
{
public:
    const char *name;
    CCriticalSection(const char *name);
    CCriticalSection();
    ~CCriticalSection();
};
/** Define a critical section that is named in debug builds.
    Named critical sections are useful in conjunction with a lock analyzer to discover bottlenecks. */
#define CRITSEC(zzname) CCriticalSection zzname(#zzname)
#endif

#ifndef DEBUG_LOCKORDER
typedef AnnotatedMixin<boost::shared_mutex> CSharedCriticalSection;
/** Define a named, shared critical section that is named in debug builds.
    Named critical sections are useful in conjunction with a lock analyzer to discover bottlenecks. */
#define SCRITSEC(x) CSharedCriticalSection x
#else

/** A shared critical section allows multiple entities to take the critical section in a "shared" mode,
    but only one entity to take the critical section exclusively.
    This is very useful for single-writer, many reader data structures. For example most of the containers
    in the std and boost libraries follow these access semantics.

    A SharedCriticalSection is NOT recursive.
*/
class CSharedCriticalSection : public AnnotatedMixin<boost::shared_mutex>
{
public:
    const char *name;
    CSharedCriticalSection();
    CSharedCriticalSection(const char *name);
    ~CSharedCriticalSection();
    void lock_shared() { boost::shared_mutex::lock_shared(); }
    void unlock_shared() { boost::shared_mutex::unlock_shared(); }
    bool try_lock_shared() { return boost::shared_mutex::try_lock_shared(); }
    void lock() { boost::shared_mutex::lock(); }
    void unlock() { boost::shared_mutex::unlock(); }
    bool try_lock() { return boost::shared_mutex::try_lock(); }
};
#define SCRITSEC(zzname) CSharedCriticalSection zzname(#zzname)
#endif

#ifndef DEBUG_LOCKORDER
typedef recursive_shared_mutex CRecursiveSharedCriticalSection;
/** Define a named, shared critical section that is named in debug builds.
    Named critical sections are useful in conjunction with a lock analyzer to discover bottlenecks. */
#define RSCRITSEC(x) CRecursiveSharedCriticalSection x
#else

/** A shared critical section allows multiple entities to recursively take the critical section in a "shared" mode,
    but only one entity to recursively take the critical section exclusively.

    A RecursiveSharedCriticalSection IS recursive.
*/
class CRecursiveSharedCriticalSection : public recursive_shared_mutex
{
public:
    const char *name;
    CRecursiveSharedCriticalSection();
    CRecursiveSharedCriticalSection(const char *n);
    ~CRecursiveSharedCriticalSection();
    // shared lock functions
    void lock_shared() SHARED_LOCK_FUNCTION() { recursive_shared_mutex::lock_shared(); }
    bool try_lock_shared() SHARED_TRYLOCK_FUNCTION(true) { return recursive_shared_mutex::try_lock_shared(); }
    void unlock_shared() UNLOCK_FUNCTION() { recursive_shared_mutex::unlock_shared(); }
    // exclusive lock functions
    void lock() EXCLUSIVE_LOCK_FUNCTION() { recursive_shared_mutex::lock(); }
    bool try_lock() EXCLUSIVE_TRYLOCK_FUNCTION(true) { return recursive_shared_mutex::try_lock(); }
    void unlock() UNLOCK_FUNCTION() { recursive_shared_mutex::unlock(); }
};
#define RSCRITSEC(zzname) CRecursiveSharedCriticalSection zzname(#zzname)
#endif

// This object can be locked or shared locked some time during its lifetime.
// Subsequent locks or shared lock calls will be ignored.
// When it is deleted, the lock is released.
class CDeferredSharedLocker
{
    enum class LockState
    {
        UNLOCKED,
        SHARED,
        EXCLUSIVE
    };
    CSharedCriticalSection &scs;
    LockState state;

public:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
#endif
    CDeferredSharedLocker(CSharedCriticalSection &scsp) : scs(scsp), state(LockState::UNLOCKED) {}
    void lock_shared()
    {
        if (state == LockState::UNLOCKED)
        {
            scs.lock_shared();
            state = LockState::SHARED;
        }
    }
    void lock()
    {
        if (state == LockState::UNLOCKED)
        {
            scs.lock();
            state = LockState::EXCLUSIVE;
        }
    }

    void unlock()
    {
        if (state == LockState::SHARED)
            scs.unlock_shared();
        else if (state == LockState::EXCLUSIVE)
            scs.unlock();
        state = LockState::UNLOCKED;
    }
    ~CDeferredSharedLocker() { unlock(); }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
};


/** Wrapped boost mutex: supports waiting but not recursive locking */
typedef AnnotatedMixin<boost::mutex> CWaitableCriticalSection;

/** Just a typedef for boost::condition_variable, can be wrapped later if desired */
typedef boost::condition_variable CConditionVariable;

/** Just a typedef for boost::condition_variable_any, can be wrapped later if desired -- c++11 version missing on win */
typedef boost::condition_variable_any CCond;

#ifdef DEBUG_LOCKORDER
void EnterCritical(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    void *cs,
    LockType locktype,
    OwnershipType ownership,
    bool fTry = false);
void LeaveCritical(void *cs);
/** Asserts in debug builds if a critical section is not held. */
void AssertLockHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs)
    ASSERT_EXCLUSIVE_LOCK(cs);
void AssertLockNotHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs);
/** Asserts in debug builds if a shared critical section is not exclusively held. */
void AssertWriteLockHeldInternal(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    CSharedCriticalSection *cs) ASSERT_EXCLUSIVE_LOCK(cs);
void AssertRecursiveWriteLockHeldinternal(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    CRecursiveSharedCriticalSection *cs);
#else
void static inline EnterCritical(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    void *cs,
    LockType locktype,
    OwnershipType ownership,
    bool fTry = false)
{
}
void static inline LeaveCritical(void *cs) {}
void static inline AssertLockHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs)
    ASSERT_EXCLUSIVE_LOCK(cs){};
void static inline AssertLockNotHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs) {}
void static inline AssertWriteLockHeldInternal(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    CSharedCriticalSection *cs) ASSERT_EXCLUSIVE_LOCK(cs)
{
}
void static inline AssertRecursiveWriteLockHeldinternal(const char *pszName,
    const char *pszFile,
    unsigned int nLine,
    CRecursiveSharedCriticalSection *cs)
{
}
#endif
#define AssertLockHeld(cs) AssertLockHeldInternal(#cs, __FILE__, __LINE__, &cs)
#define AssertLockNotHeld(cs) AssertLockNotHeldInternal(#cs, __FILE__, __LINE__, &cs)
#define AssertWriteLockHeld(cs) AssertWriteLockHeldInternal(#cs, __FILE__, __LINE__, &cs)
#define AssertRecursiveWriteLockHeld(cs) AssertRecursiveWriteLockHeldInternal(#cs, __FILE__, __LINE__, &cs)

#ifdef DEBUG_LOCKCONTENTION
void PrintLockContention(const char *pszName, const char *pszFile, unsigned int nLine);
#endif

#define LOCK_WARN_TIME (500ULL * 1000ULL * 1000ULL)

/** Wrapper around boost::unique_lock<Mutex> */
template <typename Mutex>
class SCOPED_LOCKABLE CMutexLock
{
private:
    boost::unique_lock<Mutex> lock;
// Checking elapsed lock time is very inefficient compared to the lock/unlock operation so we must be able to
// turn the feature on and off at compile time.
#ifdef DEBUG_LOCKTIME
    uint64_t lockedTime = 0;
#endif
    const char *name = "unknown-name";
    const char *file = "unknown-file";
    unsigned int line = 0;

    void Enter(const char *pszName, const char *pszFile, unsigned int nLine, LockType type)
    {
#ifdef DEBUG_LOCKTIME
        uint64_t startWait = GetStopwatch();
#endif
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void *)(lock.mutex()), type, OwnershipType::EXCLUSIVE, false);
#ifdef DEBUG_LOCKCONTENTION
        if (!lock.try_lock())
        {
            PrintLockContention(pszName, pszFile, nLine);
#endif
            lock.lock();
#ifdef DEBUG_LOCKCONTENTION
        }
#endif

#ifdef DEBUG_LOCKTIME
        lockedTime = GetStopwatch();
        if (lockedTime - startWait > LOCK_WARN_TIME)
        {
            LOG(LCK, "Lock %s at %s:%d waited for %d ms\n", pszName, pszFile, nLine, (lockedTime - startWait));
        }
#endif
    }

    bool TryEnter(const char *pszName, const char *pszFile, unsigned int nLine, LockType type)
    {
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void *)(lock.mutex()), type, OwnershipType::EXCLUSIVE, true);
        lock.try_lock();
        if (!lock.owns_lock())
        {
#ifdef DEBUG_LOCKTIME
            lockedTime = 0;
#endif
            LeaveCritical((void *)(lock.mutex()));
        }
#ifdef DEBUG_LOCKTIME
        else
            lockedTime = GetStopwatch();
#endif
        return lock.owns_lock();
    }

public:
    CMutexLock(Mutex &mutexIn,
        const char *pszName,
        const char *pszFile,
        unsigned int nLine,
        LockType type,
        bool fTry = false) EXCLUSIVE_LOCK_FUNCTION(mutexIn)
        : lock(mutexIn, boost::defer_lock)
    {
        assert(pszName != nullptr);
        // we no longer allow naming critical sections cs, please name it something more meaningful
        assert(std::string(pszName) != "cs");
        if (fTry)
            TryEnter(pszName, pszFile, nLine, type);
        else
            Enter(pszName, pszFile, nLine, type);
    }

    CMutexLock(Mutex *pmutexIn,
        const char *pszName,
        const char *pszFile,
        unsigned int nLine,
        LockType type,
        bool fTry = false) EXCLUSIVE_LOCK_FUNCTION(pmutexIn)
    {
        if (!pmutexIn)
            return;

        assert(pszName != nullptr);
        // we no longer allow naming critical sections cs, please name it something more meaningful
        assert(std::string(pszName) != "cs");
        lock = boost::unique_lock<Mutex>(*pmutexIn, boost::defer_lock);
        if (fTry)
            TryEnter(pszName, pszFile, nLine, type);
        else
            Enter(pszName, pszFile, nLine, type);
    }

    ~CMutexLock() UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
        {
            LeaveCritical((void *)(lock.mutex()));
#ifdef DEBUG_LOCKTIME
            uint64_t doneTime = GetStopwatch();
            if (doneTime - lockedTime > LOCK_WARN_TIME)
            {
                LOG(LCK, "Lock %s at %s:%d remained locked for %d ms\n", name, file, line, doneTime - lockedTime);
            }
#endif
        }
    }

    operator bool() { return lock.owns_lock(); }
};

/** Wrapper around boost::unique_lock<Mutex> */
template <typename Mutex>
class SCOPED_LOCKABLE CMutexReadLock
{
private:
    boost::shared_lock<Mutex> lock;
// Checking elapsed lock time is very inefficient compared to the lock/unlock operation so we must be able to
// turn the feature on and off at compile time.
#ifdef DEBUG_LOCKTIME
    uint64_t lockedTime = 0;
#endif
    const char *name = "unknown-name";
    const char *file = "unknown-file";
    unsigned int line = 0;

    void Enter(const char *pszName, const char *pszFile, unsigned int nLine, LockType type)
    {
#ifdef DEBUG_LOCKTIME
        uint64_t startWait = GetStopwatch();
#endif
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void *)(lock.mutex()), type, OwnershipType::SHARED, false);
// LOG(LCK,"try ReadLock %p %s by %d\n", lock.mutex(), name ? name : "", boost::this_thread::get_id());
#ifdef DEBUG_LOCKCONTENTION
        if (!lock.try_lock())
        {
            PrintLockContention(pszName, pszFile, nLine);
#endif
            lock.lock();
#ifdef DEBUG_LOCKCONTENTION
        }
#endif
// LOG(LCK,"ReadLock %p %s taken by %d\n", lock.mutex(), name ? name : "", boost::this_thread::get_id());
#ifdef DEBUG_LOCKTIME
        lockedTime = GetStopwatch();
        if (lockedTime - startWait > LOCK_WARN_TIME)
        {
            LOG(LCK, "Lock %s at %s:%d waited for %d ms\n", pszName, pszFile, nLine, (lockedTime - startWait));
        }
#endif
    }

    bool TryEnter(const char *pszName, const char *pszFile, unsigned int nLine, LockType type)
    {
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void *)(lock.mutex()), type, OwnershipType::SHARED, true);
        if (!lock.try_lock())
        {
#ifdef DEBUG_LOCKTIME
            lockedTime = 0;
#endif
            LeaveCritical((void *)(lock.mutex()));
        }
#ifdef DEBUG_LOCKTIME
        else
            lockedTime = GetStopwatch();
#endif
        return lock.owns_lock();
    }

public:
    CMutexReadLock(Mutex &mutexIn,
        const char *pszName,
        const char *pszFile,
        unsigned int nLine,
        LockType type,
        bool fTry = false) SHARED_LOCK_FUNCTION(mutexIn)
        : lock(mutexIn, boost::defer_lock)
    {
        assert(pszName != nullptr);
        // we no longer allow naming critical sections cs, please name it something more meaningful
        assert(std::string(pszName) != "cs");
        if (fTry)
            TryEnter(pszName, pszFile, nLine, type);
        else
            Enter(pszName, pszFile, nLine, type);
    }

    CMutexReadLock(Mutex *pmutexIn,
        const char *pszName,
        const char *pszFile,
        unsigned int nLine,
        LockType type,
        bool fTry = false) SHARED_LOCK_FUNCTION(pmutexIn)
    {
        if (!pmutexIn)
            return;

        assert(pszName != nullptr);
        // we no longer allow naming critical sections cs, please name it something more meaningful
        assert(std::string(pszName) != "cs");
        lock = boost::shared_lock<Mutex>(*pmutexIn, boost::defer_lock);
        if (fTry)
            TryEnter(pszName, pszFile, nLine, type);
        else
            Enter(pszName, pszFile, nLine, type);
    }

    ~CMutexReadLock() UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
        {
            LeaveCritical((void *)(lock.mutex()));
#ifdef DEBUG_LOCKTIME
            int64_t doneTime = GetStopwatch();
            if (doneTime - lockedTime > LOCK_WARN_TIME)
            {
                LOG(LCK, "Lock %s at %s:%d remained locked for %d ms\n", name, file, line, doneTime - lockedTime);
            }
#endif
        }
        // When lock is destructed it will release
    }

    operator bool() { return lock.owns_lock(); }
};

typedef CMutexReadLock<CRecursiveSharedCriticalSection> CRecursiveReadBlock;
typedef CMutexLock<CRecursiveSharedCriticalSection> CRecursiveWriteBlock;

#define RECURSIVEREADLOCK(cs) \
    CRecursiveReadBlock UNIQUIFY(recursivereadblock)(cs, #cs, __FILE__, __LINE__, LockType::RECURSIVE_SHARED_MUTEX)
#define RECURSIVEWRITELOCK(cs) \
    CRecursiveWriteBlock UNIQUIFY(writeblock)(cs, #cs, __FILE__, __LINE__, LockType::RECURSIVE_SHARED_MUTEX)
#define RECURSIVEREADLOCK2(cs1, cs2)                                                                           \
    CReadBlock UNIQUIFY(recursivereadblock1)(cs1, #cs1, __FILE__, __LINE__, LockType::RECURSIVE_SHARED_MUTEX), \
        UNIQUIFY(recursivereadblock2)(cs2, #cs2, __FILE__, __LINE__, LockType::RECURSIVE_SHARED_MUTEX)
#define TRY_RECURSIVE_READ_LOCK(cs, name) \
    CRecursiveReadBlock name(cs, #cs, __FILE__, __LINE__, LockType::RECURSIVE_SHARED_MUTEX, true)

typedef CMutexReadLock<CSharedCriticalSection> CReadBlock;
typedef CMutexLock<CSharedCriticalSection> CWriteBlock;
typedef CMutexLock<CCriticalSection> CCriticalBlock;

#define READLOCK(cs) CReadBlock UNIQUIFY(readblock)(cs, #cs, __FILE__, __LINE__, LockType::SHARED_MUTEX)
#define WRITELOCK(cs) CWriteBlock UNIQUIFY(writeblock)(cs, #cs, __FILE__, __LINE__, LockType::SHARED_MUTEX)
#define READLOCK2(cs1, cs2)                                                                 \
    CReadBlock UNIQUIFY(readblock1)(cs1, #cs1, __FILE__, __LINE__, LockType::SHARED_MUTEX), \
        UNIQUIFY(readblock2)(cs2, #cs2, __FILE__, __LINE__, LockType::SHARED_MUTEX)
#define TRY_READ_LOCK(cs, name) CReadBlock name(cs, #cs, __FILE__, __LINE__, LockType::SHARED_MUTEX, true)

#define LOCK(cs) CCriticalBlock UNIQUIFY(criticalblock)(cs, #cs, __FILE__, __LINE__, LockType::RECURSIVE_MUTEX)
#define LOCK2(cs1, cs2)                                                                                \
    CCriticalBlock UNIQUIFY(criticalblock1)(cs1, #cs1, __FILE__, __LINE__, LockType::RECURSIVE_MUTEX), \
        UNIQUIFY(criticalblock2)(cs2, #cs2, __FILE__, __LINE__, LockType::RECURSIVE_MUTEX)
#define TRY_LOCK(cs, name) CCriticalBlock name(cs, #cs, __FILE__, __LINE__, LockType::RECURSIVE_MUTEX, true)

#define ENTER_CRITICAL_SECTION(cs)                                                                                  \
    {                                                                                                               \
        EnterCritical(#cs, __FILE__, __LINE__, (void *)(&cs), LockType::RECURSIVE_MUTEX, OwnershipType::EXCLUSIVE); \
        (cs).lock();                                                                                                \
    }

#define LEAVE_CRITICAL_SECTION(cs) \
    {                              \
        (cs).unlock();             \
        LeaveCritical(&cs);        \
    }

class CSemaphore
{
private:
    boost::condition_variable condition;
    boost::mutex mutex;
    int value;

public:
    CSemaphore(int init) : value(init) {}
    void wait()
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        while (value < 1)
        {
            condition.wait(lock);
        }
        value--;
    }

    bool try_wait()
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        if (value < 1)
            return false;
        value--;
        return true;
    }

    void post()
    {
        {
            boost::unique_lock<boost::mutex> lock(mutex);
            value++;
        }
        condition.notify_one();
    }
};

/** RAII-style semaphore lock */
class CSemaphoreGrant
{
private:
    CSemaphore *sem;
    bool fHaveGrant;

public:
    void Acquire()
    {
        if (fHaveGrant)
            return;
        sem->wait();
        fHaveGrant = true;
    }

    void Release()
    {
        if (!fHaveGrant)
            return;
        sem->post();
        fHaveGrant = false;
    }

    bool TryAcquire()
    {
        if (!fHaveGrant && sem->try_wait())
            fHaveGrant = true;
        return fHaveGrant;
    }

    void MoveTo(CSemaphoreGrant &grant)
    {
        grant.Release();
        grant.sem = sem;
        grant.fHaveGrant = fHaveGrant;
        sem = nullptr;
        fHaveGrant = false;
    }

    CSemaphoreGrant() : sem(nullptr), fHaveGrant(false) {}
    CSemaphoreGrant(CSemaphore &sema, bool fTry = false) : sem(&sema), fHaveGrant(false)
    {
        if (fTry)
            TryAcquire();
        else
            Acquire();
    }

    ~CSemaphoreGrant() { Release(); }
    operator bool() { return fHaveGrant; }
};

/** A thread corral is a granular thread organization technique.
Code is assigned to a corral via Enter(...) and Exit(...) APIs (but use the scoped CCorralLock object instead of direct
calls).
Multiple threads can be running in the same corral, but threads cannot run in multiple corrals simultaneously.

Higher corral numbers block lower ones, but are themselves blocked from entry until the all other corrals are clear.
For example, lets us assume threads are running in corral region 1.
If a thread wants to enter corral region 2, threads are blocked from entering region 1.  Once all threads have left 1,
the thread(s) waiting to enter 2 are allowed to run.
Now, a thread wants to enter corral region 1.  Threads can continue to enter and leave region 2 (since it is > 1).
If all threads leave region 2, the threads waiting for region 1 are allowed to run.

Higher corral numbers are used to implement higher priority tasks.
*/
class CThreadCorral
{
protected:
    int curRegion;
    int curCount;
    int maxRequestedRegion;
    CCriticalSection mutex;
    CCond cond;

public:
    CThreadCorral() : curRegion(0), curCount(0), maxRequestedRegion(0) {}
    /** Return the region this thread corral is in */
    int region() { return curRegion; }
    /** Enter a region.  Block until it is possible */
    void Enter(int region)
    {
        LOCK(mutex);
        while (1)
        {
            // If no region is running and I'm the biggest requested region, then run my region
            if ((curCount == 0) && (region >= maxRequestedRegion))
            {
                curRegion = region;
                maxRequestedRegion = 0;
                curCount = 1;
                return;
            }
            // If the current region is mine, and no higher priority regions want to run, then I can run
            else if ((curRegion == region) && (region >= maxRequestedRegion))
            {
                curCount++;
                return;
            }
            else // I can't run now.
            {
                if (region > maxRequestedRegion)
                    maxRequestedRegion = region;
                cond.wait(mutex);
            }
        }
    }

    /* Exit a region */
    void Exit(int region)
    {
        LOCK(mutex);
        assert(curRegion == region);
        curCount--;
        if (curCount == 0)
        {
            cond.notify_all();
        }
    }
};

class CCorralLock
{
protected:
    CThreadCorral &corral;
    int region;

public:
    CCorralLock(CThreadCorral &corralp, int regionp) : corral(corralp), region(regionp) { corral.Enter(region); }
    ~CCorralLock() { corral.Exit(region); }
};

#define CORRAL(cral, region) CCorralLock UNIQUIFY(corral)(cral, region);

#endif // BITCOIN_SYNC_H
