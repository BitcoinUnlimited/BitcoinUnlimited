// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "threaddeadlock.h"
#include "util.h"

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

// removes 1 lock for a critical section
void _remove_lock_critical_exit(void *cs)
{
    if (!lockdata.available)
    {
        // lockdata was already deleted
        return;
    }
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
    OwnershipType ownership = it->second.back().second.GetExclusive();
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
    // remove from the other maps
    if (ownership == OwnershipType::EXCLUSIVE)
    {
        auto iter = lockdata.writelocksheld.find(cs);
        if (iter != lockdata.writelocksheld.end())
        {
            if (iter->second.empty())
                return;
            if (iter->second.count(tid) != 0)
            {
                iter->second.erase(tid);
            }
        }
    }
    else // !isExclusive
    {
        auto iter = lockdata.readlocksheld.find(cs);
        if (iter != lockdata.readlocksheld.end())
        {
            if (iter->second.empty())
                return;
            if (iter->second.count(tid) != 0)
            {
                iter->second.erase(tid);
            }
        }
    }
}

void potential_deadlock_detected(LockStackEntry now, LockStack &deadlocks, std::set<uint64_t> &threads)
{
    LOGA("POTENTIAL DEADLOCK DETECTED\n");
    LOGA("This occurred while trying to lock: %s ", now.second.ToString().c_str());
    LOGA("which has:\n");

    auto rlw = lockdata.readlockswaiting.find(now.first);
    if (rlw != lockdata.readlockswaiting.end())
    {
        for (auto &entry : rlw->second)
        {
            LOGA("Read Lock Waiting for thread with id %" PRIu64 "\n", entry);
        }
    }

    auto wlw = lockdata.writelockswaiting.find(now.first);
    if (wlw != lockdata.writelockswaiting.end())
    {
        for (auto &entry : wlw->second)
        {
            LOGA("Write Lock Waiting for thread with id %" PRIu64 "\n", entry);
        }
    }

    auto rlh = lockdata.readlocksheld.find(now.first);
    if (rlh != lockdata.readlocksheld.end())
    {
        for (auto &entry : rlh->second)
        {
            LOGA("Read Lock Held for thread with id %" PRIu64 "\n", entry);
        }
    }

    auto wlh = lockdata.writelocksheld.find(now.first);
    if (wlh != lockdata.writelocksheld.end())
    {
        for (auto &entry : wlh->second)
        {
            LOGA("Write Lock Held for thread with id %" PRIu64 "\n", entry);
        }
    }

    LOGA("\nThe locks involved are:\n");
    for (auto &lock : deadlocks)
    {
        LOGA(" %s\n", lock.second.ToString().c_str());
    }
    for (auto &thread : threads)
    {
        LOGA("\nThread with tid %" PRIu64 " was involved. It held locks:\n", thread);
        auto iterheld = lockdata.locksheldbythread.find(thread);
        if (iterheld != lockdata.locksheldbythread.end())
        {
            for (auto &lockentry : iterheld->second)
            {
                LOGA(" %s\n", lockentry.second.ToString().c_str());
            }
        }
    }
    // clean up the lock before throwing
    _remove_lock_critical_exit(now.first);
    throw std::logic_error("potential deadlock detected");
}

void potential_lock_order_issue_detected(std::string thisLock, std::string otherLock)
{
    LOGA("POTENTIAL LOCK ORDER ISSUE DETECTED\n");
    LOGA("This occurred while trying to lock: %s after %s", thisLock.c_str(), otherLock.c_str());
    LOGA("We have previously locked this locks in the reverse order\n");
    throw std::logic_error("potential lock order issue detected");
}

bool HasAnyOwners(void *c)
{
    auto iter = lockdata.writelocksheld.find(c);
    if (iter != lockdata.writelocksheld.end())
    {
        if (!iter->second.empty())
        {
            return true;
        }
    }
    auto iter2 = lockdata.readlocksheld.find(c);
    if (iter2 != lockdata.readlocksheld.end())
    {
        if (!iter2->second.empty())
        {
            return true;
        }
    }

    return false;
}

static bool RecursiveCheck(const uint64_t &tid,
    const void *c,
    uint64_t lastTid,
    void *lastLock,
    bool firstRun,
    LockStack &deadlocks,
    std::set<uint64_t> &threads)
{
    if (!firstRun && c == lastLock && tid == lastTid)
    {
        // we are back where we started, infinite loop means there is a deadlock
        return true;
    }
    // first check if we currently have any other ownerships
    auto self_iter = lockdata.locksheldbythread.find(lastTid);
    if (self_iter != lockdata.locksheldbythread.end())
    {
        if (self_iter->second.size() == 0)
        {
            // we cant deadlock if we dont own any other mutexs
            return false;
        }
    }
    // at this point we have at least 1 lock for a mutex somewhere

    // check if a thread has an ownership of c
    auto writeiter = lockdata.writelocksheld.find(lastLock);
    auto readiter = lockdata.readlocksheld.find(lastLock);

    // NOTE: be careful when adjusting these booleans, the order of the checks is important
    bool isReadLocked = !((readiter == lockdata.readlocksheld.end()) || readiter->second.empty());
    bool isWriteLocked = !((writeiter == lockdata.writelocksheld.end()) || writeiter->second.empty());
    if (!isWriteLocked && !isReadLocked)
    {
        // no owners, no deadlock possible
        return false;
    }
    // we have other locks, so check if we have any in common with the holder(s) of the other lock
    std::set<uint64_t> otherLocks;
    if (isWriteLocked)
    {
        otherLocks.insert(writeiter->second.begin(), writeiter->second.end());
    }
    if (isReadLocked)
    {
        otherLocks.insert(readiter->second.begin(), readiter->second.end());
    }
    for (auto &threadId : otherLocks)
    {
        if (threadId == lastTid)
        {
            // this continue fixes an infinite looping problem
            continue;
        }
        auto other_iter = lockdata.locksheldbythread.find(threadId);
        // we dont need to check empty here, other thread has at least 1 lock otherwise we wouldnt be checking it
        if (other_iter->second.size() == 1)
        {
            // it does not have any locks aside from known exclusive, no deadlock possible
            // we can just wait until that exclusive lock is released
            return false;
        }
        // if the other thread has 1+ other locks aside from the known exclusive, check them for matches with our own
        // locks
        for (auto &lock : other_iter->second)
        {
            // if they have a lock that is on a lock that someone has a lock on
            if (HasAnyOwners(lock.first))
            {
                // and their lock is waiting...
                if (lock.second.GetWaiting() == true)
                {
                    deadlocks.push_back(lock);
                    threads.emplace(other_iter->first);
                    if (other_iter->first == tid && lock.first == c)
                    {
                        // we are back where we started and there is a deadlock
                        return true;
                    }
                    if (RecursiveCheck(tid, c, other_iter->first, lock.first, false, deadlocks, threads))
                    {
                        return true;
                    }
                }
                // no deadlock, other lock is not waiting, we simply have to wait until they release that lock
            }
        }
    }
    return false;
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

void AddNewHeldLock(void *c, const uint64_t &tid, OwnershipType ownership)
{
    if (ownership == OwnershipType::EXCLUSIVE)
    {
        auto it = lockdata.writelocksheld.find(c);
        if (it == lockdata.writelocksheld.end())
        {
            std::set<uint64_t> holders;
            holders.emplace(tid);
            lockdata.writelocksheld.emplace(c, holders);
        }
        else
        {
            it->second.emplace(tid);
        }
    }
    else //  !isExclusive
    {
        auto it = lockdata.readlocksheld.find(c);
        if (it == lockdata.readlocksheld.end())
        {
            std::set<uint64_t> holders;
            holders.emplace(tid);
            lockdata.readlocksheld.emplace(c, holders);
        }
        else
        {
            it->second.emplace(tid);
        }
    }
}

void AddNewWaitingLock(void *c, const uint64_t &tid, OwnershipType ownership)
{
    if (ownership == OwnershipType::EXCLUSIVE)
    {
        auto it = lockdata.writelockswaiting.find(c);
        if (it == lockdata.writelockswaiting.end())
        {
            std::set<uint64_t> holders;
            holders.emplace(tid);
            lockdata.writelockswaiting.emplace(c, holders);
        }
        else
        {
            it->second.emplace(tid);
        }
    }
    else //  !isExclusive
    {
        auto it = lockdata.readlockswaiting.find(c);
        if (it == lockdata.readlockswaiting.end())
        {
            std::set<uint64_t> holders;
            holders.emplace(tid);
            lockdata.readlockswaiting.emplace(c, holders);
        }
        else
        {
            it->second.emplace(tid);
        }
    }
}

void SetWaitingToHeld(void *c, OwnershipType ownership)
{
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);

    const uint64_t tid = getTid();
    // we do this first so changes are still made for trylocks which dont have
    // a waiting lock to be edited
    auto itheld = lockdata.locksheldbythread.find(tid);
    if (itheld != lockdata.locksheldbythread.end())
    {
        for (auto rit = itheld->second.rbegin(); rit != itheld->second.rend(); ++rit)
        {
            if (rit->first == c)
            {
                rit->second.ChangeWaitingToHeld();
                break;
            }
        }
    }


    if (ownership == OwnershipType::EXCLUSIVE)
    {
        auto it = lockdata.writelockswaiting.find(c);
        if (it != lockdata.writelockswaiting.end())
        {
            it->second.erase(tid);
        }
        else
        {
            DbgAssert(!"Missing write lock waiting", );
        }
        auto iter = lockdata.writelocksheld.find(c);
        if (iter == lockdata.writelocksheld.end())
        {
            std::set<uint64_t> holders;
            holders.emplace(tid);
            lockdata.writelocksheld.emplace(c, holders);
        }
        else
        {
            iter->second.emplace(tid);
        }
    }
    else //  !isExclusive
    {
        auto it = lockdata.readlockswaiting.find(c);
        if (it != lockdata.readlockswaiting.end())
        {
            it->second.erase(tid);
        }
        else
        {
            DbgAssert(!"Missing read lock waiting", );
        }
        auto iter = lockdata.readlocksheld.find(c);
        if (iter == lockdata.readlocksheld.end())
        {
            std::set<uint64_t> holders;
            holders.emplace(tid);
            lockdata.readlocksheld.emplace(c, holders);
        }
        else
        {
            iter->second.emplace(tid);
        }
    }
}

// c = the cs
// isExclusive = is the current lock exclusive, for a recursive mutex (CCriticalSection) this value should always be
// true
void push_lock(void *c, const CLockLocation &locklocation, LockType locktype, OwnershipType ownership, bool fTry)
{
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);

    LockStackEntry now = std::make_pair(c, locklocation);
    if (fTry)
    {
        // try locks can not be waiting
        now.second.ChangeWaitingToHeld();
    }
    // tid of the originating request
    const uint64_t tid = getTid();
    // If this is a blocking lock operation, we want to make sure that the locking order between 2 mutexes is consistent
    // across the program
    if (fTry)
    {
        // a try lock will either get it, or it wont. so just add it.
        // if we dont get the lock this will be undone in the destructor of the read or write block
        // for whichever critical section made this function call.
        AddNewLock(now, tid);
        // we need to add a held lock
        AddNewHeldLock(c, tid, ownership);
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

    // check for lock ordering issues
    // caluclate this locks name
    std::string lockname = locklocation.GetMutexName();
    // get a list of locks we have locked using this threads id
    auto holdingIter = lockdata.locksheldbythread.find(tid);
    if (holdingIter != lockdata.locksheldbythread.end())
    {
        // get the names of those locks
        std::vector<std::string> nameHeldLocks;
        for (auto &entry : holdingIter->second)
        {
            std::string entryLockName = entry.second.GetMutexName();
            nameHeldLocks.push_back(entryLockName);
        }
        // check for lock ordering issues using the held locks list
        auto locknameIter = lockdata.seenlockorders.find(lockname);
        if (locknameIter != lockdata.seenlockorders.end())
        {
            // we have seen the lock we are trying to lock before, check ordering
            for (auto &heldLock : nameHeldLocks)
            {
                if (locknameIter->second.count(heldLock))
                {
                    potential_lock_order_issue_detected(lockname, heldLock);
                }
            }
        }
        else
        {
            // we have not seen the lock we are trying to lock before, add data for it
            for (auto &heldLock : nameHeldLocks)
            {
                auto heldLockIter = lockdata.seenlockorders.find(heldLock);
                if (heldLockIter != lockdata.seenlockorders.end())
                {
                    // add information about this lock
                    heldLockIter->second.emplace(lockname);
                }
            }
            // add a new key to track locks locked after this one
            lockdata.seenlockorders.emplace(lockname, std::set<std::string>());
        }
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
    AddNewLock(now, tid);
    if (lockingRecursively == true)
    {
        // we can skip the deadlock detection checks because it is a recursive lock
        // and self deadlocks were checked earlier
        return;
    }
    AddNewWaitingLock(c, tid, ownership);
    std::vector<LockStackEntry> deadlocks;
    std::set<uint64_t> threads;
    if (RecursiveCheck(tid, c, tid, c, true, deadlocks, threads))
    {
        potential_deadlock_detected(now, deadlocks, threads);
    }
}

// removes all instances of the critical section
void DeleteCritical(void *cs)
{
    // remove all instances of the critical section from lockdata
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);
    if (!lockdata.available)
    {
        // lockdata was already deleted
        return;
    }
    lockdata.readlockswaiting.erase(cs);
    lockdata.writelockswaiting.erase(cs);
    lockdata.readlocksheld.erase(cs);
    lockdata.writelocksheld.erase(cs);
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
    if (self_iter == lockdata.locksheldbythread.end())
    {
        return;
    }
    if (self_iter->second.empty())
    {
        return;
    }
    for (auto &entry : self_iter->second)
    {
        if (entry.first == cs)
        {
            // found the lock so return
            return;
        }
    }
    fprintf(stderr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
        _LocksHeld().c_str());
    abort();
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
                fprintf(stderr, "Assertion failed: lock %s held in %s:%i; locks held:\n%s", pszName, pszFile, nLine,
                    _LocksHeld().c_str());
                abort();
            }
        }
    }
}

#endif // DEBUG_LOCKORDER
