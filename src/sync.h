// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SYNC_H
#define BITCOIN_SYNC_H

#include "threadsafety.h"
#include "utiltime.h"
#include "util.h"

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/thread.hpp>
#include <condition_variable>

////////////////////////////////////////////////
//                                            //
// THE SIMPLE DEFINITION, EXCLUDING DEBUG CODE //
//                                            //
////////////////////////////////////////////////

/*
CCriticalSection mutex;
    boost::recursive_mutex mutex;

LOCK(mutex);
    boost::unique_lock<boost::recursive_mutex> criticalblock(mutex);

LOCK2(mutex1, mutex2);
    boost::unique_lock<boost::recursive_mutex> criticalblock1(mutex1);
    boost::unique_lock<boost::recursive_mutex> criticalblock2(mutex2);

TRY_LOCK(mutex, name);
    boost::unique_lock<boost::recursive_mutex> name(mutex, boost::try_to_lock_t);

ENTER_CRITICAL_SECTION(mutex); // no RAII
    mutex.lock();

LEAVE_CRITICAL_SECTION(mutex); // no RAII
    mutex.unlock();
 */

///////////////////////////////
//                           //
// THE ACTUAL IMPLEMENTATION //
//                           //
///////////////////////////////

/**
 * Template mixin that adds -Wthread-safety locking
 * annotations to a subset of the mutex API.
 */
template <typename PARENT>
class LOCKABLE AnnotatedMixin : public PARENT
{
public:
    void lock() EXCLUSIVE_LOCK_FUNCTION()
    {
        PARENT::lock();
    }

    void unlock() UNLOCK_FUNCTION()
    {
        PARENT::unlock();
    }

    bool try_lock() EXCLUSIVE_TRYLOCK_FUNCTION(true)
    {
        return PARENT::try_lock();
    }
};

/**
 * Wrapped boost mutex: supports recursive locking, but no waiting
 * TODO: We should move away from using the recursive lock by default.
 */
#ifndef DEBUG_LOCKORDER
typedef AnnotatedMixin<boost::recursive_mutex> CCriticalSection;
#define CRITSEC(x) CCriticalSection x
#else  // BU we need to remove the critical section from the lockorder map when destructed
class CCriticalSection:public AnnotatedMixin<boost::recursive_mutex>
{
public:
  const char* name;
  CCriticalSection(const char* name);
  CCriticalSection();
  ~CCriticalSection();
};
#define CRITSEC(zzname) CCriticalSection zzname(#zzname)
#endif

#ifndef DEBUG_LOCKORDER
typedef AnnotatedMixin<boost::shared_mutex> CSharedCriticalSection;
#define SCRITSEC(x) CSharedCriticalSection x
#else  // BU we need to remove the critical section from the lockorder map when destructed
class CSharedCriticalSection:public AnnotatedMixin<boost::shared_mutex>
{
public:

    class LockInfo
    {
    public:
        const char* file;
        unsigned int line;
    LockInfo():file(""), line(0) {}
    LockInfo(const char* f, unsigned int l): file(f), line(l) {}
    };

  boost::mutex setlock;
  std::map<uint64_t, LockInfo> sharedowners;
  const char* name;
  uint64_t exclusiveOwner;
  CSharedCriticalSection(const char* name);
  CSharedCriticalSection();
  ~CSharedCriticalSection();
  void lock_shared();
  bool try_lock_shared();
  void unlock_shared();
  void lock();
  void unlock();
  bool try_lock();
};
#define SCRITSEC(zzname) CSharedCriticalSection zzname(#zzname)
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
    }
    ~CDeferredSharedLocker() { unlock(); }
};

/** Wrapped boost mutex: supports waiting but not recursive locking */
typedef AnnotatedMixin<boost::mutex> CWaitableCriticalSection;

/** Just a typedef for boost::condition_variable, can be wrapped later if desired */
typedef boost::condition_variable CConditionVariable;

/** Just a typedef for boost::condition_variable_any, can be wrapped later if desired */
typedef std::condition_variable_any CCond;

#ifdef DEBUG_LOCKORDER
void EnterCritical(const char* pszName, const char* pszFile, int nLine, void* cs, bool fTry = false);
void LeaveCritical();
void DeleteCritical(const void* cs); // BU if a CCriticalSection is allocated on the heap we need to clean it from the lockorder map upon destruction because another CCriticalSection could be created on top of it.
std::string LocksHeld();
void AssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, void* cs);
void AssertWriteLockHeldInternal(const char* pszName, const char* pszFile, int nLine, CSharedCriticalSection *cs);
#else
void static inline EnterCritical(const char* pszName, const char* pszFile, int nLine, void* cs, bool fTry = false) {}
void static inline LeaveCritical() {}
void static inline AssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, void* cs) {}
void static inline AssertWriteLockHeldInternal(const char* pszName, const char* pszFile, int nLine, CSharedCriticalSection *cs) {}
#endif
#define AssertLockHeld(cs) AssertLockHeldInternal(#cs, __FILE__, __LINE__, &cs)
#define AssertWriteLockHeld(cs) AssertWriteLockHeldInternal(#cs, __FILE__, __LINE__, &cs)

#ifdef DEBUG_LOCKCONTENTION
void PrintLockContention(const char* pszName, const char* pszFile, int nLine);
#endif

#define LOCK_WARN_TIME (500ULL*1000ULL*1000ULL)

/** Wrapper around boost::unique_lock<Mutex> */
template <typename Mutex>
class SCOPED_LOCKABLE CMutexLock
{
private:
    boost::unique_lock<Mutex> lock;
#ifdef DEBUG_LOCKTIME
    uint64_t lockedTime;
#endif
    const char* name;
    const char* file;
    int line;

    void Enter(const char* pszName, const char* pszFile, int nLine)
    {
#ifdef DEBUG_LOCKTIME
        uint64_t startWait = GetStopwatch();
#endif
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void*)(lock.mutex()));
#ifdef DEBUG_LOCKCONTENTION
        if (!lock.try_lock()) {
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
            LogPrint("lck", "Lock %s at %s:%d waited for %d ms\n", pszName, pszFile, nLine,(lockedTime - startWait));
          }
#endif
    }

    bool TryEnter(const char* pszName, const char* pszFile, int nLine)
    {
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void*)(lock.mutex()), true);
        lock.try_lock();
        if (!lock.owns_lock())
        {
#ifdef DEBUG_LOCKTIME
            lockedTime = 0;
#endif
            LeaveCritical();
        }
#ifdef DEBUG_LOCKTIME
        else
            lockedTime = GetStopwatch();
#endif
        return lock.owns_lock();
    }

public:
    CMutexLock(Mutex& mutexIn, const char* pszName, const char* pszFile, int nLine, bool fTry = false) EXCLUSIVE_LOCK_FUNCTION(mutexIn) : lock(mutexIn, boost::defer_lock)
    {
        if (fTry)
            TryEnter(pszName, pszFile, nLine);
        else
            Enter(pszName, pszFile, nLine);
    }

    CMutexLock(Mutex* pmutexIn, const char* pszName, const char* pszFile, int nLine, bool fTry = false) EXCLUSIVE_LOCK_FUNCTION(pmutexIn)
    {
        if (!pmutexIn) return;

        lock = boost::unique_lock<Mutex>(*pmutexIn, boost::defer_lock);
        if (fTry)
            TryEnter(pszName, pszFile, nLine);
        else
            Enter(pszName, pszFile, nLine);
    }

    ~CMutexLock() UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
          {
            LeaveCritical();
#ifdef DEBUG_LOCKTIME
            uint64_t doneTime = GetStopwatch();
            if (doneTime - lockedTime > LOCK_WARN_TIME)
            {
                LogPrint(
                    "lck", "Lock %s at %s:%d remained locked for %d ms\n", name, file, line, doneTime - lockedTime);
            }
#endif
          }
    }

    operator bool()
    {
        return lock.owns_lock();
    }
};

/** Wrapper around boost::unique_lock<Mutex> */
template <typename Mutex>
class SCOPED_LOCKABLE CMutexReadLock
{
private:
    boost::shared_lock<Mutex> lock;
    uint64_t lockedTime;
    const char* name;
    const char* file;
    int line;

    void Enter(const char* pszName, const char* pszFile, int nLine)
    {
#ifdef DEBUG_LOCKTIME
        uint64_t startWait = GetStopwatch();
#endif
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void*)(lock.mutex()));
        //LogPrint("lck","try ReadLock %p %s by %d\n", lock.mutex(), name ? name : "", boost::this_thread::get_id());
#ifdef DEBUG_LOCKCONTENTION
        if (!lock.try_lock()) {
            PrintLockContention(pszName, pszFile, nLine);
#endif
            lock.lock();
#ifdef DEBUG_LOCKCONTENTION
        }
#endif
        //LogPrint("lck","ReadLock %p %s taken by %d\n", lock.mutex(), name ? name : "", boost::this_thread::get_id());
#ifdef DEBUG_LOCKTIME
        lockedTime = GetStopwatch();
        if (lockedTime - startWait > LOCK_WARN_TIME)
          {
            LogPrint("lck", "Lock %s at %s:%d waited for %d ms\n", pszName, pszFile, nLine,(lockedTime - startWait));
          }
#endif
    }

    bool TryEnter(const char* pszName, const char* pszFile, int nLine)
    {
        name = pszName;
        file = pszFile;
        line = nLine;
        EnterCritical(pszName, pszFile, nLine, (void*)(lock.mutex()), true);
        if (!lock.try_lock())
          {
#ifdef DEBUG_LOCKTIME
              lockedTime = 0;
#endif
              LeaveCritical();
          }
#ifdef DEBUG_LOCKTIME
          else
              lockedTime = GetStopwatch();
#endif
          return lock.owns_lock();
    }

public:
    CMutexReadLock(Mutex& mutexIn, const char* pszName, const char* pszFile, int nLine, bool fTry = false) SHARED_LOCK_FUNCTION(mutexIn) : lock(mutexIn, boost::defer_lock)
    {
        if (fTry)
            TryEnter(pszName, pszFile, nLine);
        else
            Enter(pszName, pszFile, nLine);
    }

    CMutexReadLock(Mutex* pmutexIn, const char* pszName, const char* pszFile, int nLine, bool fTry = false) SHARED_LOCK_FUNCTION(pmutexIn)
    {
        if (!pmutexIn) return;

        lock = boost::shared_lock<Mutex>(*pmutexIn, boost::defer_lock);
        if (fTry)
            TryEnter(pszName, pszFile, nLine);
        else
            Enter(pszName, pszFile, nLine);
    }

    ~CMutexReadLock() UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
          {
            LeaveCritical();
#ifdef DEBUG_LOCKTIME
            int64_t doneTime = GetStopwatch();
            if (doneTime - lockedTime > LOCK_WARN_TIME)
            {
                LogPrint(
                    "lck", "Lock %s at %s:%d remained locked for %d ms\n", name, file, line, doneTime - lockedTime);
            }
#endif
          }
          // When lock is destructed it will release
    }

    operator bool()
    {
        return lock.owns_lock();
    }
};

typedef CMutexReadLock<CSharedCriticalSection> CReadBlock;
typedef CMutexLock<CSharedCriticalSection> CWriteBlock;
typedef CMutexLock<CCriticalSection> CCriticalBlock;

#define READLOCK(cs) CReadBlock UNIQUIFY(readblock)(cs, #cs, __FILE__, __LINE__)
#define WRITELOCK(cs) CWriteBlock UNIQUIFY(writeblock)(cs, #cs, __FILE__, __LINE__)
#define READLOCK2(cs1, cs2) CReadBlock readblock1(cs1, #cs1, __FILE__, __LINE__), readblock2(cs2, #cs2, __FILE__, __LINE__)
#define TRY_READ_LOCK(cs, name) CReadBlock name(cs, #cs, __FILE__, __LINE__, true)

#define LOCK(cs) CCriticalBlock UNIQUIFY(criticalblock)(cs, #cs, __FILE__, __LINE__)
#define LOCK2(cs1, cs2) CCriticalBlock criticalblock1(cs1, #cs1, __FILE__, __LINE__), criticalblock2(cs2, #cs2, __FILE__, __LINE__)
#define TRY_LOCK(cs, name) CCriticalBlock name(cs, #cs, __FILE__, __LINE__, true)

#define ENTER_CRITICAL_SECTION(cs)                            \
    {                                                         \
        EnterCritical(#cs, __FILE__, __LINE__, (void*)(&cs)); \
        (cs).lock();                                          \
    }

#define LEAVE_CRITICAL_SECTION(cs) \
    {                              \
        (cs).unlock();             \
        LeaveCritical();           \
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
        while (value < 1) {
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
    CSemaphore* sem;
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

    void MoveTo(CSemaphoreGrant& grant)
    {
        grant.Release();
        grant.sem = sem;
        grant.fHaveGrant = fHaveGrant;
        sem = NULL;
        fHaveGrant = false;
    }

    CSemaphoreGrant() : sem(NULL), fHaveGrant(false) {}

    CSemaphoreGrant(CSemaphore& sema, bool fTry = false) : sem(&sema), fHaveGrant(false)
    {
        if (fTry)
            TryAcquire();
        else
            Acquire();
    }

    ~CSemaphoreGrant()
    {
        Release();
    }

    operator bool()
    {
        return fHaveGrant;
    }
};

// BU move from sync.c because I need to create these in globals.cpp
struct CLockLocation
{
    CLockLocation(const char* pszName, const char* pszFile, int nLine, bool fTryIn);
    std::string ToString() const;
    std::string MutexName() const;

    bool fTry;
private:
    std::string mutexName;
    std::string sourceFile;
    int sourceLine;
};

typedef std::vector<std::pair<void*, CLockLocation> > LockStack;
typedef std::map<std::pair<void*, void*>, LockStack> LockStackMap;


class CThreadCorral
{
protected:
    int curRegion;
    int curCount;
    int maxRequestedRegion;
    CCriticalSection mutex;
    CCond cond;
public:
    CThreadCorral():curRegion(0),curCount(0),maxRequestedRegion(0) {}

    void Enter(int region)
    {
        LOCK(mutex);
        while (1)
        {
            // If no region is running and I'm the biggest requested region, then run my region
            if ((curCount == 0)&&(region>=maxRequestedRegion))
            {
                curRegion = region;
                maxRequestedRegion=0;
                curCount = 1;
                return;
            }
            // If the current region is mine, and no higher priority regions want to run, then I can run
            else if ((curRegion == region)&&(region>=maxRequestedRegion))
            {
                curCount++;
                return;
            }
            else  // I can't run now.
            {
                if (region > maxRequestedRegion) maxRequestedRegion = region;
                cond.wait(mutex);
            }
        }
    }

    void Exit(int region)
    {
        LOCK(mutex);
        assert(curRegion == region);
        curCount--;
        if (curCount==0)
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

#define CORRAL(cral, region) CCorralLock UNIQUIFY(corral)(cral,region);

#endif // BITCOIN_SYNC_H
