// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "lockorder.h"
#include "util.h"

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

void CLockOrderTracker::potential_lock_order_issue_detected(const CLockLocation &thisLock,
    const CLockLocation &otherLock,
    const uint64_t &tid)
{
    LOGA("POTENTIAL LOCK ORDER ISSUE DETECTED\n");
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

bool CLockOrderTracker::CanCheckForConflicts(const std::string &lockname)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    return (seenLockOrders.find(lockname) != seenLockOrders.end());
}

// this function assumes you already checked if lockname exists
void CLockOrderTracker::CheckForConflict(const CLockLocation &locklocation,
    const std::vector<CLockLocation> &heldLocks,
    const uint64_t &tid)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    std::string newlock = locklocation.GetMutexName();
    for (auto &heldLock : heldLocks)
    {
        std::string lockheldname = heldLock.GetMutexName();
        if (newlock == lockheldname)
        {
            // if they are the same then continue
            continue;
        }
        if (seenLockOrders[newlock].count(lockheldname))
        {
            potential_lock_order_issue_detected(locklocation, heldLock, tid);
        }
        seenLockOrders[lockheldname].emplace(newlock);
    }
}

void CLockOrderTracker::AddNewLockInfo(const std::string &lockname, const std::vector<CLockLocation> &heldLocks)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    // we have not seen the lock we are trying to lock before, add data for it
    for (auto &heldLock : heldLocks)
    {
        std::string heldLockName = heldLock.GetMutexName();
        auto heldLockIter = seenLockOrders.find(heldLockName);
        if (heldLockIter == seenLockOrders.end())
        {
            continue;
        }
        // add information about this lock
        heldLockIter->second.emplace(lockname);
        for (auto &otherLock : seenLockOrders)
        {
            if (otherLock.first != lockname)
            {
                if (otherLock.second.count(heldLockName) != 0)
                {
                    otherLock.second.emplace(lockname);
                }
            }
            else if (otherLock.first == lockname)
            {
                for (auto &other_element : otherLock.second)
                {
                    heldLockIter->second.emplace(other_element);
                }
            }
        }
    }
    // add a new key to track locks locked after this one
    if (seenLockOrders.find(lockname) == seenLockOrders.end())
    {
        seenLockOrders.emplace(lockname, std::set<std::string>());
    }
}

void CLockOrderTracker::TrackLockOrderHistory(const CLockLocation &locklocation,
    const std::vector<CLockLocation> &heldLocks,
    const uint64_t &tid)
{
    std::lock_guard<std::mutex> lock(lot_mutex);
    std::string key1 = locklocation.GetMutexName();
    std::string value1 = locklocation.GetFileName() + ":" + std::to_string(locklocation.GetLineNumber());
    for (auto &heldLock : heldLocks)
    {
        std::string key2 = heldLock.GetMutexName();
        std::string value2 = heldLock.GetFileName() + ":" + std::to_string(heldLock.GetLineNumber());
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
