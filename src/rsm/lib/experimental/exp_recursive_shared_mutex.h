// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _EXP_RECURSIVE_SHARED_MUTEX_H
#define _EXP_RECURSIVE_SHARED_MUTEX_H

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>


/**
 * This mutex has two levels of access, shared and exclusive. Multiple threads can own this mutex in shared mode but
 * only one can own it in exclusive mode.
 * - A thread is considered to have ownership when it successfully calls either lock or try_lock.
 * - A thread may recusively call lock for ownership and must call a matching number of unlock calls to end ownership.
 * - A thread MAY call for shared ownership if it already has exclusive ownership. This should just increment the
 * _shared_while_exclusive_counter instead of actually locking anything
 * - A thread MAY obtain exclusive ownership if no threads excluding itself has shared ownership. (this might need to
 * check for another write lock already queued up so we dont jump the line)
 */


static const std::thread::id NON_THREAD_ID = std::thread::id();

class exp_recursive_shared_mutex
{
protected:
    // Only locked when accessing counters, ids, or waiting on condition variables.
    std::mutex _mutex;

    // the read_gate is locked (blocked) when threads have write ownership
    std::condition_variable _read_gate;

    // the write_gate is locked (blocked) when threads have read ownership or someone is waiting for promotion
    std::condition_variable _write_gate;

    // // the write_gate is locked (blocked) when threads have read ownership
    std::condition_variable _promotion_write_gate;

    // holds a list of owner ids that have shared ownership and the number of times they locked it
    std::map<std::thread::id, uint64_t> _read_owner_ids;

    // holds the number of shared locks the thread with exclusive ownership has
    // this is used to allow the thread with exclusive ownership to lock_shared
    uint64_t _shared_while_exclusive_counter;

    // _write_counter tracks how many times exclusive ownership has been recursively locked
    uint64_t _write_counter;
    // _write_owner_id is the id of the thread with exclusive ownership
    std::thread::id _write_owner_id;
    // _promotion_candidate_id is the id of the thread waiting for a promotion
    std::thread::id _promotion_candidate_id;

    // used to keep track of normal thread exclusive line if a thread has promoted
    uint64_t _write_counter_reserve;

private:
    bool end_of_exclusive_ownership();
    bool check_for_write_lock(const std::thread::id &locking_thread_id);
    bool check_for_write_unlock(const std::thread::id &locking_thread_id);

    bool already_has_lock_shared(const std::thread::id &locking_thread_id);
    void lock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count = 1);
    void unlock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count = 1);

public:
    exp_recursive_shared_mutex()
    {
        _read_owner_ids.clear();
        _write_counter = 0;
        _shared_while_exclusive_counter = 0;
        _write_owner_id = NON_THREAD_ID;
        _promotion_candidate_id = NON_THREAD_ID;
        _write_counter_reserve = 0;
    }

    ~exp_recursive_shared_mutex() {}
    exp_recursive_shared_mutex(const exp_recursive_shared_mutex &) = delete;
    exp_recursive_shared_mutex &operator=(const exp_recursive_shared_mutex &) = delete;

    /**
     * "Wait in line" for exclusive ownership of the mutex.
     *
     * This call is blocking when waiting for exclusive ownership.
     * When exclusive ownership is obtained the id of the thread that made this call
     * is stored in _write_ownder_id and _write_counter is incremeneted by 1.
     * When called by a thread that already has exclusive ownership,t
     * the _write_counter is incremeneted by 1 and call does not block.
     *
     *
     * @param none
     * @return none
     */
    void lock();

    /**
     * Become "next in line" for exclusive ownership of the mutex if the promotion
     * slot is not already occupied by another thread.
     *
     * When called by a thread that has shared ownership or no ownership, attempt to
     * obtain the promotion slot. Only one thread can hold the promotion slot at a time.
     * While promotion slot is obtained and waiting for exclusive ownership this
     * call is blocking.
     * When called by a thread that already has exclusive ownership,
     * _write_counter is incremeneted by 1 and call does not block
     *
     *
     * @param none
     * @return: false on failure to be put in the promotion slot because
     * it is already occupied by another thread.
     * true when _write_counter has been incremented or exclusive ownership has been
     * obtained
     */
    bool try_promotion();

    /**
     * Attempt to claim exclusive ownership of the mutex if no threads
     * have exclusive or shared ownership of the mutex including this one.
     *
     * This call never blocks.
     * When called by a thread that already has exclusive ownership,
     * _write_counter is incremeneted by 1
     *
     *
     * @param none
     * @return: false on failure to obtain exclusive ownership.
     * true when _write_counter has been incremented or exclusive ownership has been
     * obtained
     */
    bool try_lock();

    /**
     * Release 1 count of exclusive ownership.
     *
     * This call never blocks.
     * When called by a thread that has exclusive ownership, either _write_counter is
     * decremented by 1. When both write_counter and _shared_while_exclusive_counter
     * are 0, exclusive ownership is released.
     *
     *
     * @param none
     * @return: none
     */
    void unlock();

    /**
     * Attempt to claim shared ownership
     *
     * This call is blocking when waiting for shared ownership due to a thread having
     * exclusive ownership.
     * When shared ownership is obtained the id of the thread that made this call
     * is stored in _read_owner_ids with a value of 1. Recursively locking for shared
     * ownership increments the threads value in _read_owner_ids by 1.
     * If this is called by a thread with exclusive ownership, increment the _shared_while_exclusive_counter
     * by 1 instead of making an entry in _read_owner_ids
     *
     *
     * @param none
     * @return none
     */
    void lock_shared();

    /**
     * Attempt to claim shared ownership of the mutex if no threads
     * have exclusive ownership of the mutex.
     *
     * This call never blocks.
     * When called by a thread that already has shared ownership, the threads
     * _read_owner_ids value is incremeneted by 1
     * When called by a thread that has exclusive ownership, _shared_while_exclusive_counter is incremeneted by 1
     *
     *
     * @param none
     * @return: false on failure to obtain shared ownership.
     * true when the threads _read_owner_ids has been incremented or shared ownership has been
     * obtained
     */
    bool try_lock_shared();

    /**
     * Release 1 count of ownership
     *
     * This call never blocks.
     * When called by a thread that has shared ownership, decrement the value of that thread in
     * _read_owner_ids by 1. When that threads value reaches 0, remove it from _read_owner_ids signifying the
     * end of shared ownership.
     * When called by a thread with exclusive ownership decrement _shared_while_exclusive_counter by 1.
     *
     *
     * @param none
     * @return none
     */
    void unlock_shared();
};


#endif // _EXP_RECURSIVE_SHARED_MUTEX_H
