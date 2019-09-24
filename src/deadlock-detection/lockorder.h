// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOCK_ORDER_H
#define BITCOIN_LOCK_RODER_H

#include <inttypes.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>

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
    /// @var std::map<std::string, std::set<std::string> > seenLockOrders
    /// key is lockname, value is vector of locknames that have ever been locked while key was locked
    std::map<std::string, std::set<std::string> > seenLockOrders;
     /// @var std::map<std::pair<std::string, std::string>, std::set<std::tuple<std::string, std::string, uint64_t> > > seenLockLocations
     /// we track every time a lock ordering has taken place, key is the two locknames
     /// value is the lock file+lines respectivly and the id of the thread that locked them
    std::map<std::pair<std::string, std::string>, std::set<std::tuple<std::string, std::string, uint64_t> > >
        seenLockLocations;

private:
    void potential_lock_order_issue_detected(const CLockLocation &thisLock,
        const CLockLocation &otherLock,
        const uint64_t &tid);

public:
    /**
     * Determines if we have enough information to check for a conflict with a given lock
     *
     * @param std::string that is the name of the lock we want to check for conflicts with
     */
    bool CanCheckForConflicts(const std::string &lockname);

    /**
     * Checks for ordering conflicts between a given lock and a vector of other locks
     *
     * @param a CLockLocation struct that is a new lock being locked
     * @param a std::vector of CLockLocation structs that are the held locks held by a thread
     * @param a uitn64_t that is the thread id of the calling thread
     */
    void CheckForConflict(const CLockLocation &locklocation,
        const std::vector<CLockLocation> &heldLocks,
        const uint64_t &tid);

    /**
     * Adds information to seenLockOrders about an ordering seen by a given thread
     *
     * @param std::string that is the name of the lock being added
     * @param a std::vector of CLockLocation structs that are the held locks held by this thread
     */
    void AddNewLockInfo(const std::string &lockname, const std::vector<CLockLocation> &heldLocks);

    /**
     * Adds information to seenLockLocations about an ordering seen by a given thread
     *
     * This method also records information about where the lock was locked and by what thread,
     * not just the order
     *
     * @param a CLockLocation struct that is a new lock being locked
     * @param a std::vector of CLockLocation structs that are the held locks held by this thread
     * @param a uitn64_t that is the thread id of the calling thread
     */
    void TrackLockOrderHistory(const CLockLocation &locklocation,
        const std::vector<CLockLocation> &heldLocks,
        const uint64_t &tid);

    /**
     * clears all historical lock ordering data from this
     *
     * should only be called in the test suite
     */
    void clear()
    {
        seenLockOrders.clear();
        seenLockLocations.clear();
    }
};

#endif // END DEBUG_LOCKORDER

#endif
