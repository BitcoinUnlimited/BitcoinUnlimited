// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "lockorder.h"
#include "util.h"

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

extern std::atomic<bool> lockdataDestructed;

void CLockOrderTracker::potential_lock_order_issue_detected(LockStackEntry &this_lock,
    LockStackEntry &other_lock,
    const uint64_t &tid)
{
    bool possible_misname = false;
    if (mapMutexToName.count(this_lock.first) == 0)
    {
        throw std::logic_error("critical deadlock order error somewhere");
    }
    possible_misname = possible_misname || mapMutexToName[this_lock.first].second;

    if (mapMutexToName.count(other_lock.first) == 0)
    {
        throw std::logic_error("critical deadlock order error somewhere");
    }
    possible_misname = possible_misname || mapMutexToName[other_lock.first].second;

    CLockLocation &thisLock = this_lock.second;
    CLockLocation &otherLock = other_lock.second;

    LOGA("POTENTIAL LOCK ORDER ISSUE DETECTED\n");
    if (possible_misname == true)
    {
        LOGA("either %s or %s was passed by a pointer, the lock names might not be accurate \n",
            thisLock.GetMutexName().c_str(), otherLock.GetMutexName().c_str());
    }
    LOGA("This occurred while trying to lock: %s after %s \n", thisLock.GetMutexName().c_str(),
        otherLock.GetMutexName().c_str());
    LOGA("Thread with id %" PRIu64
         " attempted to lock %s on line %i in file %s after locking %s on line %i in file %s\n",
        tid, thisLock.GetMutexName().c_str(), thisLock.GetLineNumber(), thisLock.GetFileName().c_str(),
        otherLock.GetMutexName().c_str(), otherLock.GetLineNumber(), otherLock.GetFileName().c_str());
    LOGA("We have previously locked these locks in the reverse order\n");
    LOGA("full lock order dump: \n");
    for (auto &set : seenLockLocations)
    {
        for (auto &entry : set.second)
        {
            LOGA("locked %s then locked %s\n", set.first.c_str(), entry.c_str());
        }
    }
    throw std::logic_error("potential lock order issue detected");
}


// this function assumes you already checked if lockname exists
void CLockOrderTracker::CheckForConflict(LockStackEntry &this_lock,
    std::vector<LockStackEntry> &heldLocks,
    const uint64_t &tid)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    if (seenLockOrders.find(this_lock.first) == seenLockOrders.end())
    {
        return;
    }
    for (auto &heldLock : heldLocks)
    {
        if (this_lock.first == heldLock.first)
        {
            // if they are the same then continue
            continue;
        }
        if (seenLockOrders[this_lock.first].count(heldLock.first))
        {
            potential_lock_order_issue_detected(this_lock, heldLock, tid);
        }
    }
}

void CLockOrderTracker::AddNewLockInfo(const LockStackEntry &this_lock, const std::vector<LockStackEntry> &heldLocks)
{
    std::lock_guard<std::mutex> lock(lot_mutex);

    auto name_iter = mapMutexToName.find(this_lock.first);
    if (name_iter == mapMutexToName.end())
    {
        mapMutexToName.emplace(this_lock.first, std::make_pair(this_lock.second.GetMutexName(), true));
    }
    else
    {
        if (name_iter->second.first != this_lock.second.GetMutexName())
        {
            name_iter->second.second = false;
        }
    }
    for (auto &heldLock : heldLocks)
    {
        auto heldLockIter = seenLockOrders.find(heldLock.first);
        if (heldLockIter == seenLockOrders.end())
        {
            continue;
        }
        // add information about this lock
        for (auto &otherLock : seenLockOrders)
        {
            if (otherLock.first != this_lock.first)
            {
                if (otherLock.second.count(heldLock.first) != 0 || otherLock.first == heldLock.first)
                {
                    otherLock.second.emplace(this_lock.first);
                    auto this_lock_iter = seenLockOrders.find(this_lock.first);
                    if (this_lock_iter != seenLockOrders.end())
                    {
                        for (auto &element : this_lock_iter->second)
                        {
                            otherLock.second.emplace(element);
                        }
                    }
                }
            }
        }
    }
    // add a new key to track locks locked after this one
    if (seenLockOrders.find(this_lock.first) == seenLockOrders.end())
    {
        seenLockOrders.emplace(this_lock.first, std::set<void *>());
    }
}

void CLockOrderTracker::TrackLockOrderHistory(const CLockLocation &locklocation,
    const std::vector<LockStackEntry> &heldLocks)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    // build the new key
    std::string new_key = locklocation.GetMutexName() + " on " + locklocation.GetFileName() + ":" +
                          std::to_string(locklocation.GetLineNumber());
    // add newest key to map if it does not exist
    if (seenLockLocations.count(new_key) == 0)
    {
        seenLockLocations.emplace(new_key, std::set<std::string>());
    }
    // add held locks
    for (auto &heldLock : heldLocks)
    {
        std::string held_key = heldLock.second.GetMutexName() + " on " + heldLock.second.GetFileName() + ":" +
                               std::to_string(heldLock.second.GetLineNumber());
        auto held_iter = seenLockLocations.find(held_key);
        if (held_iter == seenLockLocations.end())
        {
            // this happens on recursive locks
        }
        else
        {
            held_iter->second.emplace(new_key);
        }
    }
}

void CLockOrderTracker::DeleteCritical(void *cs)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    std::map<void *, std::set<void *> >::iterator iter;
    iter = seenLockOrders.find(cs);
    if (iter != seenLockOrders.end())
    {
        iter->second.clear();
        seenLockOrders.erase(iter);
    }
    for (auto &entry : seenLockOrders)
    {
        entry.second.erase(cs);
    }
}

#endif // end DEBUG_LOCKORDER
