#ifndef _RECURSIVE_SHARED_MUTEX_H
#define _RECURSIVE_SHARED_MUTEX_H

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


/*
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

class recursive_shared_mutex
{
protected:
    // Only locked when accessing counters, ids, or waiting on condition variables.
    std::mutex _mutex;

    // the read_gate is locked (blocked) when threads have write ownership
    std::condition_variable _read_gate;

    // the write_gate is locked (blocked) when threads have read ownership
    std::condition_variable _write_gate;

    // promotion_gates
    std::condition_variable _promotion_read_gate;
    std::condition_variable _promotion_write_gate;

    // holds a list of owner ids that have shared ownership and the number of times they locked it
    std::map<std::thread::id, uint64_t> _read_owner_ids;

    // holds the owner id and count of shared locks when a thread was promoted
    // this data is used to restore proper shared locks when promoted thread
    // releases exclusive ownership and "demotes" back to only shared ownership
    std::thread::id _auto_unlock_id;
    uint64_t _auto_unlock_count;

    // holds the number of shared locks the thread with exclusive ownership has
    // this is used to allow the thread with exclusive ownership to lock_shared
    uint64_t _shared_while_exclusive_counter;

    uint64_t _write_counter;
    uint64_t _write_promotion_counter;
    std::thread::id _write_owner_id;
    std::thread::id _promotion_candidate_id;

private:
    bool check_for_write_lock(const std::thread::id &locking_thread_id);
    bool check_for_write_unlock(const std::thread::id &locking_thread_id);

    bool already_has_lock_shared(const std::thread::id &locking_thread_id);
    void lock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count = 1);
    void unlock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count = 1);
    uint64_t get_shared_lock_count(const std::thread::id &locking_thread_id);

    void lock_auto_locks(const std::thread::id &locking_thread_id, const uint64_t &count);
    uint64_t get_auto_lock_count(const std::thread::id &locking_thread_id);
    void unlock_auto_locks(const std::thread::id &locking_thread_id);

public:
    recursive_shared_mutex()
    {
        _read_owner_ids.clear();
        _auto_unlock_id = NON_THREAD_ID;
        _auto_unlock_count = 0;
        _write_counter = 0;
        _write_promotion_counter = 0;
        _shared_while_exclusive_counter = 0;
        _write_owner_id = NON_THREAD_ID;
        _promotion_candidate_id = NON_THREAD_ID;
    }

    ~recursive_shared_mutex() {}
    recursive_shared_mutex(const recursive_shared_mutex &) = delete;
    recursive_shared_mutex &operator=(const recursive_shared_mutex &) = delete;

    void lock();
    bool try_promotion();
    bool try_lock();
    void unlock();
    void lock_shared();
    bool try_lock_shared();
    void unlock_shared();
};


#endif // _RECURSIVE_SHARED_MUTEX_H
