// Copyright (c) 2019-2020 Greg Griffith
// Copyright (c) 2019-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "threaddeadlock.h"
#include "util.h"

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

// removes 1 lock for a critical section
void _remove_lock_critical_exit(void *cs)
{
    uint64_t tid = getTid();
    auto it = lockdata.locksheldbythread.find(tid);
    if (it == lockdata.locksheldbythread.end())
    {
        throw std::logic_error("unlocking non-existant lock");
    }
    if (it->second.back().first != cs)
    {
        LOGA("got %s but was not expecting it\n", it->second.back().second.ToString().c_str());
        throw std::logic_error("unlock order inconsistant with lock order");
    }
    LockType type = it->second.back().second.GetLockType();
    // assuming we unlock in the reverse order of locks, we can simply pop back
    it->second.pop_back();
    // if we have no more locks on this critical section...
    if (type != LockType::SHARED_MUTEX)
    {
        for (auto entry : it->second)
        {
            if (entry.first == cs)
            {
                // we have another lock on this critical section
                return;
            }
        }
    }
}

// for recrusive locking issues with a non recrusive mutex
static void self_deadlock_detected(LockStackEntry now, LockStackEntry previous)
{
    LOGA("SELF DEADLOCK DETECTED FOR SHARED MUTEX\n");
    LOGA("Previous lock was: %s\n", previous.second.ToString());
    LOGA("Current lock is: %s\n", now.second.ToString());
    throw std::logic_error("self_deadlock_detected");
}

void AddNewLock(LockStackEntry newEntry, const uint64_t &tid)
{
    auto it = lockdata.locksheldbythread.find(tid);
    if (it == lockdata.locksheldbythread.end())
    {
        LockStack newLockStack;
        newLockStack.push_back(newEntry);
        lockdata.locksheldbythread.emplace(tid, newLockStack);
    }
    else
    {
        it->second.push_back(newEntry);
    }
}

// c = the cs
// isExclusive = is the current lock exclusive, for a recursive mutex (CCriticalSection) this value should always be
// true
void push_lock(void *c, const CLockLocation &locklocation, LockType locktype, OwnershipType ownership, bool fTry)
{
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);

    LockStackEntry now = std::make_pair(c, locklocation);
    // tid of the originating request
    const uint64_t tid = getTid();
    // If this is a blocking lock operation, we want to make sure that the locking order between 2 mutexes is consistent
    // across the program
    if (fTry)
    {
        // a try lock will either get it, or it wont. so just add it for now.
        // if we dont get the lock these added locks will be removed before the end of the TRY_LOCK function
        AddNewLock(now, tid);
        return;
    }
    // first check lock specific issues
    if (locktype == LockType::SHARED_MUTEX)
    {
        // shared mutexs cant recursively lock at all, check if we already have a lock on the mutex
        auto it = lockdata.locksheldbythread.find(tid);
        if (it != lockdata.locksheldbythread.end() && !it->second.empty())
        {
            for (auto &lockStackLock : it->second)
            {
                // if it is c we are recursively locking a non recursive mutex, there is a deadlock
                if (lockStackLock.first == c)
                {
                    self_deadlock_detected(now, lockStackLock);
                }
            }
        }
    }
    else if (locktype == LockType::RECURSIVE_SHARED_MUTEX)
    {
        // we cannot lock exclusive if we already hold shared, check for this scenario
        if (ownership == OwnershipType::EXCLUSIVE)
        {
            auto it = lockdata.locksheldbythread.find(tid);
            if (it != lockdata.locksheldbythread.end() && !it->second.empty())
            {
                for (auto &lockStackLock : it->second)
                {
                    // if we have a lock and it isnt exclusive and we are attempting to get exclusive
                    // then we will deadlock ourself
                    if (lockStackLock.first == c && lockStackLock.second.GetExclusive() == OwnershipType::SHARED)
                    {
                        self_deadlock_detected(now, lockStackLock);
                    }
                }
            }
        }
        else
        {
            // intentionally left blank
            // a single thread taking an exclusive lock then shared lock wont deadlock, only shared then exclusive
        }
    }
    else if (locktype == LockType::RECURSIVE_MUTEX)
    {
        // this lock can not deadlock itself
        // intentionally left blank
    }
    else
    {
        DbgAssert(!"unsupported lock type", return );
    }

    // Begin general deadlock checks for all lock types
    bool lockingRecursively = false;
    // if lock not shared mutex, check if we are doing a recursive lock
    if (locktype != LockType::SHARED_MUTEX)
    {
        auto it = lockdata.locksheldbythread.find(tid);
        if (it != lockdata.locksheldbythread.end())
        {
            // check if we have locked this lock before
            for (auto &entry : it->second)
            {
                // if we have locked this lock before...
                if (entry.first == c)
                {
                    // then we are locking recursively
                    lockingRecursively = true;
                    break;
                }
            }
        }
    }

    // check for lock ordering issues, skip if locking recursively because we already have an order
    if (lockingRecursively == false)
    {
        // caluclate this locks name
        std::vector<LockStackEntry> heldLocks;
        // get a list of locks we have locked using this threads id
        auto holdingIter = lockdata.locksheldbythread.find(tid);
        if (holdingIter != lockdata.locksheldbythread.end())
        {
            // get the names of those locks
            for (auto &entry : holdingIter->second)
            {
                heldLocks.push_back(entry);
            }
            lockdata.ordertracker.AddNewLockInfo(now, heldLocks);
            // track this locks exactly locking order info
            lockdata.ordertracker.TrackLockOrderHistory(locklocation, heldLocks);
            // we have seen the lock we are trying to lock before, check ordering
            lockdata.ordertracker.CheckForConflict(now, heldLocks, tid);
        }
        else
        {
            lockdata.ordertracker.AddNewLockInfo(now, heldLocks);
            // track this locks exactly locking order info
            lockdata.ordertracker.TrackLockOrderHistory(locklocation, heldLocks);
        }
    }
    AddNewLock(now, tid);
    if (lockingRecursively == true)
    {
        // we can skip the deadlock detection checks because it is a recursive lock
        // and self deadlocks were checked earlier
        return;
    }
}

// removes all instances of the critical section
void DeleteCritical(void *cs)
{
    if (lockdataDestructed.load())
        return;
    // remove all instances of the critical section from lockdata
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);
    for (auto &iter : lockdata.locksheldbythread)
    {
        LockStack newStack;
        for (auto &iter2 : iter.second)
        {
            if (iter2.first != cs)
            {
                newStack.emplace_back(std::make_pair(iter2.first, iter2.second));
            }
        }
        std::swap(iter.second, newStack);
    }
    lockdata.ordertracker.DeleteCritical(cs);
}

// removes 1 lock for a critical section
void remove_lock_critical_exit(void *cs)
{
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);
    _remove_lock_critical_exit(cs);
}

std::string _LocksHeld()
{
    std::string result;
    uint64_t tid = getTid();
    auto self_iter = lockdata.locksheldbythread.find(tid);
    if (self_iter != lockdata.locksheldbythread.end())
    {
        for (auto &entry : self_iter->second)
        {
            result += entry.second.ToString() + std::string("\n");
        }
    }
    return result;
}

std::string LocksHeld()
{
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);
    return _LocksHeld();
}

void AssertLockHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs)
{
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);
    uint64_t tid = getTid();
    auto self_iter = lockdata.locksheldbythread.find(tid);
    if (self_iter == lockdata.locksheldbythread.end() || self_iter->second.empty() == true)
    {
        // this thread is not holding any locks
        fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
            _LocksHeld().c_str());
        throw std::logic_error("AssertLockHeld failure");
    }
    for (auto &entry : self_iter->second)
    {
        if (entry.first == cs)
        {
            // found the lock so return
            return;
        }
    }
    // this thread is holding locks but none are the one we are checking for
    fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
        _LocksHeld().c_str());
    throw std::logic_error("AssertLockHeld failure");
}

void AssertLockNotHeldInternal(const char *pszName, const char *pszFile, unsigned int nLine, void *cs)
{
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);
    uint64_t tid = getTid();
    auto self_iter = lockdata.locksheldbythread.find(tid);
    if (self_iter != lockdata.locksheldbythread.end() && self_iter->second.empty() == false)
    {
        for (auto &entry : self_iter->second)
        {
            if (entry.first == cs)
            {
                // this thread is holding the lock when it should not be
                fprintf(stderr, "Assertion failed: lock %s held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
                    _LocksHeld().c_str());
                throw std::logic_error("AssertLockNotHeld failure");
            }
        }
    }
}

#endif // DEBUG_LOCKORDER
