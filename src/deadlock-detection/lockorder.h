// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOCK_ORDER_H
#define BITCOIN_LOCK_ORDER_H

#include <inttypes.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include "locklocation.h"

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

/**
 * CLockOrderTracker tracks the globally seen lock ordering for all locks and threads
 */
class CLockOrderTracker
{
protected:
    /// @var std::mutex lot_mutex
    /// mutex that is required to be locked before any method accesses any of this classes data members
    std::mutex lot_mutex;
    /// @var std::map<void*, std::pair<std::string, boolean> >
    /// map for attempting to track the name of the mutex based on its reference. might not always be accurate, boolean
    /// will denote this
    std::map<void *, std::pair<std::string, bool> > mapMutexToName;
    /// @var std::map<void*, std::set<void*> > seenLockOrders
    /// key is mutex, value is set of mutex that have ever been locked while key was locked
    std::map<void *, std::set<void *> > seenLockOrders;
    /// @var std::map<std::string, std::set<std::string> > seenLockLocations
    /// we track every time a lock ordering has taken place, key is the lockname+file+line
    /// value is the set of locks we locked after this one with the entry lockname+file+line
    std::map<std::string, std::set<std::string> > seenLockLocations;

private:
    void potential_lock_order_issue_detected(LockStackEntry &this_lock,
        LockStackEntry &other_lock,
        const uint64_t &tid);

public:
    size_t size()
    {
        uint64_t lockorderssize = 0;
        for (auto &entry : seenLockOrders)
        {
            lockorderssize += entry.second.size() + 1;
        }
        return lockorderssize;
    }
    /**
     * Checks for ordering conflicts between a given lock and a vector of other locks
     *
     * @param a LockStackEntry that is a new lock being locked
     * @param a std::vector of LockStackEntry that are the held locks held by a thread
     * @param a uitn64_t that is the thread id of the calling thread
     */
    void CheckForConflict(LockStackEntry &this_lock, std::vector<LockStackEntry> &heldLocks, const uint64_t &tid);

    /**
     * Adds information to seenLockOrders about an ordering seen by a given thread
     *
     * @param LockStackEntry that is the name of the lock being added
     * @param a std::vector of LockStackEntry that are the held locks held by this thread
     */
    void AddNewLockInfo(const LockStackEntry &this_lock, const std::vector<LockStackEntry> &heldLocks);

    /**
     * Adds information to seenLockLocations about an ordering seen by a given thread
     *
     * This method also records information about where the lock was locked and by what thread,
     * not just the order
     *
     * @param a CLockLocation struct that is a new lock being locked
     * @param a std::vector of LockStackEntry that are the held locks held by this thread
     * @param a uitn64_t that is the thread id of the calling thread
     */
    void TrackLockOrderHistory(const CLockLocation &locklocation, const std::vector<LockStackEntry> &heldLocks);

    /**
     * Removes lock order information for a mutex that has been deleted
     *
     * @param a void pointer to the mutex that is being deleted
     */
    void DeleteCritical(void *cs);

    /**
     * clears all historical lock ordering data from this
     *
     * should only be called in the test suite
     */
    void clear()
    {
        mapMutexToName.clear();
        seenLockOrders.clear();
        seenLockLocations.clear();
    }
};

#endif // END DEBUG_LOCKORDER

#endif
