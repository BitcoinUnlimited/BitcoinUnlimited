// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THREAD_DEADLOCK_H
#define BITCOIN_THREAD_DEADLOCK_H

#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <memory>
#include <mutex>

#include "lockorder.h"
#include "utilstrencodings.h"

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file
#ifdef __linux__
#include <sys/syscall.h>
inline uint64_t getTid(void)
{
    // "native" thread id used so the number correlates with what is shown in gdb
    pid_t tid = (pid_t)syscall(SYS_gettid);
    return tid;
}
#else
#include <functional>
inline uint64_t getTid(void)
{
    // Note: there is no guaranteed way to turn the thread-id into an int
    // since it's an opaque type. Just about the only operation it supports
    // is std::hash (so that thread id's may be placed in maps).
    // So we just do this.
    static std::hash<std::thread::id> hasher;
    return uint64_t(hasher(std::this_thread::get_id()));
}
#endif

// In your app, declare lockdata and all global lock variables in a single module so destruction order is controlled.
// But for unit tests, these might be declared in separate files.  In this case we use a global boolean to indicate
// whether lockdata has been destructed.
extern std::atomic<bool> lockdataDestructed;

struct LockData
{
    /// @var ReadLocksWaiting readlockswaiting
    /// holds information about threads waiting on shared ownership of mutexes
    ReadLocksWaiting readlockswaiting;
    /// @var WriteLocksWaiting writelockswaiting
    /// holds information about threads waiting on exclusive ownership of mutexes
    WriteLocksWaiting writelockswaiting;

    /// @var ReadLocksHeld readlocksheld
    /// holds information about threads holding shared ownership of mutexes
    ReadLocksHeld readlocksheld;
    /// @var WriteLocksHeld writelocksheld
    /// holds information about threads holding exclusive ownership of mutexes
    WriteLocksHeld writelocksheld;

    /// @var LocksHeldByThread locksheldbythread
    /// holds information about which locks are held by which threads
    LocksHeldByThread locksheldbythread;

    /// @var CLockOrderTracker ordertracker
    /// holds information about the global ordering of locking
    CLockOrderTracker ordertracker;

    /// @var std::mutex dd_mutex
    /// a mutex that protects all other data members of this struct
    std::mutex dd_mutex;

    ~LockData() { lockdataDestructed.store(true); }
};
extern LockData lockdata;


/**
 * Adds a new lock to LockData tracking
 *
 * Should only be called by EnterCritical
 *
 * @param void pointer to the critical section that was locked
 * @param CLockLocation struct containing information about the critical section that was locked
 * @param LockType enum value that describes what type of critical section was locked
 * @param OwnershipType enum value that describes what type of ownership was claimed on the critical section
 * @param boolean that describes if the lock was made by a lock() or try_lock() call
 */
void push_lock(void *c, const CLockLocation &locklocation, LockType locktype, OwnershipType ownership, bool fTry);

/**
 * Removes a critical section and all locks related to it from LockData
 *
 * Should only be called by a critical section destructor
 *
 * @param void pointer to the critical section that is to be removed
 */
void DeleteCritical(void *cs);

/**
 * Removes the most recent instance of locks from LockData
 *
 * Should only be called by LeaveCritical
 *
 * @param void pointer to the critical section that is to be removed
 */
void remove_lock_critical_exit(void *cs);

/**
 * Prints all of the locks held by the calling thread
 *
 * @return std::string of all locks held by the calling thread
 */
std::string LocksHeld();

/**
 * Moves a lock that is currently in one of the waiting maps to the corresponding held map
 *
 * @param void pointer to the critical section that is to be moved from waiting to held
 * @param OwnershipType enum value that is the OwnershipType for the critical section
 */
void SetWaitingToHeld(void *c, OwnershipType ownership);

#else // NOT DEBUG_LOCKORDER

static inline void SetWaitingToHeld(void *c, OwnershipType ownership) {}

#endif // END DEBUG_LOCKORDER

#endif
