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

// tracks the globally seen lock ordering
class CLockOrderTracker
{
private:
    std::mutex lot_mutex;
    // key is lockname, value is vector of locknames that have ever been locked while key was locked
    std::map<std::string, std::set<std::string> > SeenLockOrders;
    // we track every time a lock ordering has taken place, key is the two locknames
    // value is the lock file+lines respectivly and the id of the thread that locked them
    std::map<std::pair<std::string, std::string>, std::set<std::tuple<std::string, std::string, uint64_t> > >
        SeenLockLocations;

    void potential_lock_order_issue_detected(const CLockLocation &thisLock,
        const CLockLocation &otherLock,
        const uint64_t &tid);

public:
    bool CanCheckForConflicts(const std::string &lockname);

    void CheckForConflict(const CLockLocation &locklocation,
        const std::vector<CLockLocation> &heldLocks,
        const uint64_t &tid);

    void AddNewLockInfo(const std::string &lockname, const std::vector<CLockLocation> &heldLocks);

    void TrackLockOrderHistory(const CLockLocation &locklocation,
        const std::vector<CLockLocation> &heldLocks,
        const uint64_t &tid);
};

#endif // END DEBUG_LOCKORDER

#endif
