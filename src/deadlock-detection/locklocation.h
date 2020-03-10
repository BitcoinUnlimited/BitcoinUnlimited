// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOCK_LOCATION_H
#define BITCOIN_LOCK_LOCATION_H

#include <inttypes.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

/// @enum LockType
enum LockType
{
    RECURSIVE_MUTEX, /// CCriticalSection
    SHARED_MUTEX, /// CSharedCriticalSection
    RECURSIVE_SHARED_MUTEX, /// CRecursiveSharedCriticalSection
};

/// @enum OwnershipType
enum OwnershipType
{
    SHARED, /// shared ownership
    EXCLUSIVE /// exclusive ownership
};

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

/**
 * The CLockLocation class represents where and what type of a lock was made on a critical section
 */

struct CLockLocation
{
    /**
     * Constructor that sets the
     *
     * @param pszName is a C String that is the name of the critical section being locked
     * @param pszFile is a C String that is the name of the file in which the critical sction was locked
     * @param nLine is a number that is the line of the file on which this lock occured
     * @param fTryIn is a boolean that indicates if this was a lock() or a try_lock()
     * @param eOwnershipIn is an OwnershipType enum value that describes if the lock is shared or exclusive in ownership
     * @param eLockTypeIn is a LockType enum value that describes what type of critical section has been locked
     */
    CLockLocation(const char *pszName,
        const char *pszFile,
        int nLine,
        bool fTryIn,
        OwnershipType eOwnershipIn,
        LockType eLockTypeIn);

    /**
     * Returns this as a descriptive readable string
     *
     * @return std::string
     */
    std::string ToString() const;

    /**
     * Returns a boolean that describes if ownership of this mutex was taken by a lock or a try lock
     *
     * @return boolean
     */
    bool GetTry() const;

    /**
     * Returns the ownership type of the lock used to lock the mutex
     *
     * @return OwnershipType
     */
    OwnershipType GetExclusive() const;

    /**
     * Returns the tpye of lock being locked
     *
     * @return LockType
     */
    LockType GetLockType() const;

    /**
     * Returns the file name this mutex was locked in as a string
     *
     * @return std::string
     */
    std::string GetFileName() const;

    /**
     * Returns the line number of the file in which this mutex was locked as a string
     *
     * @return std::string
     */
    int GetLineNumber() const;

    /**
     * Returns the name of this mutex as a string
     *
     * @return std::string
     */
    std::string GetMutexName() const;

protected:
    /// @var boolean fTry
    /// Determines if this CLockLocation was created by locking or try locking a critical section
    bool fTry;
    /// @var std::string mutexName
    //. The name of the mutex that was locked
    std::string mutexName;
    /// @var std::string sourceFile
    /// The source file in which this mutex was locked
    std::string sourceFile;
    /// @var int sourceLine
    /// The line of the source file in which this mutex was locked
    int sourceLine;
    //. @var LockType eLockType
    /// The type of mutex that was locked
    LockType eLockType;
    /// @var OwnershipType eOwnership
    /// determines if lock has shared or exclusive ownership, LockType::RECURSIVE_MUTEX is always exclusive
    OwnershipType eOwnership;
    /// @var boolean fWaiting
    /// determines if lock is held or is waiting to be held
    bool fWaiting;
};

/// @typedef std::pair <cs : CLockLocation struct holding info about the cs>
typedef std::pair<void *, CLockLocation> LockStackEntry;
/// @typedef std::vector of LockStackEntry
typedef std::vector<LockStackEntry> LockStack;

/// @typedef std::map <cs : set of thread ids that have shared ownership of the cs>
typedef std::map<void *, std::set<uint64_t> > ReadLocksHeld;
/// @typedef std::map <cs : set of thread ids that have exclusive ownership of the cs>
typedef std::map<void *, std::set<uint64_t> > WriteLocksHeld;

/// @typedef std::map<cs : set of thread ids that are waiting for shared ownership of the cs>
typedef std::map<void *, std::set<uint64_t> > ReadLocksWaiting;
/// @typedef std::map<cs : set of thread ids that are waiting for exclusive ownership of the cs>
typedef std::map<void *, std::set<uint64_t> > WriteLocksWaiting;

// @typedef std::map<thread id : vector of locks held (both shared and exclusive, waiting and held>
typedef std::map<uint64_t, LockStack> LocksHeldByThread;

#endif // end DEBUG_LOCKORDER

#endif
