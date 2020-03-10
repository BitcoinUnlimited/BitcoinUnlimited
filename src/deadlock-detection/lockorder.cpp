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
    LOGA("\n\nOur historical lock orderings containing these two locks by thread %" PRIu64 " include: \n", tid);

    std::pair<std::string, std::string> key = std::make_pair(otherLock.GetMutexName(), thisLock.GetMutexName());
    auto iter = seenLockLocations.find(key);
    if (iter != seenLockLocations.end())
    {
        for (auto &entry : iter->second)
        {
            if (std::get<2>(entry) == tid)
            {
                LOGA("This thread previously locked %s on %s after locking %s on %s\n", key.first.c_str(),
                    std::get<0>(entry).c_str(), key.second.c_str(), std::get<1>(entry).c_str());
            }
        }
        LOGA("\n\n");
        LOGA("Our historical lock orderings containing these two locks by other threads include: \n");
        for (auto &entry : iter->second)
        {
            const uint64_t storedTid = std::get<2>(entry);
            if (storedTid != tid)
            {
                LOGA("Thread with id %" PRIu64 " previously locked %s on %s after locking %s on %s\n", storedTid,
                    key.first.c_str(), std::get<0>(entry).c_str(), key.second.c_str(), std::get<1>(entry).c_str());
            }
        }
        LOGA("\n\n");
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
    const std::vector<LockStackEntry> &heldLocks,
    const uint64_t &tid)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    std::string key1 = locklocation.GetMutexName();
    std::string value1 = locklocation.GetFileName() + ":" + std::to_string(locklocation.GetLineNumber());
    for (auto &heldLock : heldLocks)
    {
        std::string key2 = heldLock.second.GetMutexName();
        std::string value2 = heldLock.second.GetFileName() + ":" + std::to_string(heldLock.second.GetLineNumber());
        auto iter = seenLockLocations.find(std::make_pair(key1, key2));
        if (iter == seenLockLocations.end())
        {
            seenLockLocations.emplace(std::make_pair(key1, key2),
                std::set<std::tuple<std::string, std::string, uint64_t> >{std::make_tuple(value1, value2, tid)});
        }
        else
        {
            iter->second.emplace(std::make_tuple(value1, value2, tid));
        }
    }
}

#endif // end DEBUG_LOCKORDER
