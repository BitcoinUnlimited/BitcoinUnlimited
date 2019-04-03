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
This mutex has two levels of access, shared and exclusive. Multiple threads can own this mutex in shared mode but only
one can own it in exclusive mode.
A thread is considered to have ownership when it successfully calls either lock or try_lock.
A thread may recusively call lock for ownership and must call a matching number of unlock calls to end ownership.
A thread MAY call for shared ownership if it already has exclusive ownership. This should just add an additional lock on
top of write counter
and not actually lock. If a thread has exclusive ownership and checks for shared ownership this should return true.
*/

/*
TODO
- A thread MAY obtain exclusive ownership if no threads excluding itself has shared ownership. (this might need to check
for another write lock already
    queued up so we dont jump the line)
*/

static const std::thread::id NON_THREAD_ID = std::thread::id();
static const uint64_t SANE_LOCK_LIMIT = 1000; // this instead of uint64_t max value

class recursive_shared_mutex
{
private:
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
    // holds a list of owner ids that have been auto unlocked due to promoting and the number of
    // times they were auto unlocked
    std::map<std::thread::id, uint64_t> _auto_unlocked_ids;

    uint64_t _write_counter;
    uint64_t _write_promotion_counter;
    std::thread::id _write_owner_id;
    std::thread::id _promotion_candidate_id;

private:
    bool check_for_write_lock(const std::thread::id &locking_thread_id);
    bool unlock_if_write_lock(const std::thread::id &locking_thread_id);

    void lock_shared_internal(const std::thread::id &locking_thread_id);
    void lock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count);
    void unlock_shared_internal(const std::thread::id &locking_thread_id);
    void unlock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count);
    uint64_t get_shared_lock_count(const std::thread::id &locking_thread_id);

    void lock_auto_locks(const std::thread::id &locking_thread_id, const uint64_t &count);
    uint64_t get_auto_lock_count(const std::thread::id &locking_thread_id);
    void unlock_auto_locks(const std::thread::id &locking_thread_id);

public:
    recursive_shared_mutex()
    {
        _read_owner_ids.clear();
        _auto_unlocked_ids.clear();
        _write_counter = 0;
        _write_promotion_counter = 0;
        _write_owner_id = NON_THREAD_ID;
        _promotion_candidate_id = NON_THREAD_ID;
    }

    ~recursive_shared_mutex() {}
    recursive_shared_mutex(const recursive_shared_mutex &) = delete;
    recursive_shared_mutex &operator=(const recursive_shared_mutex &) = delete;

    void lock(const std::thread::id &locking_thread_id);
    bool try_promotion(const std::thread::id &locking_thread_id);
    bool try_lock(const std::thread::id &locking_thread_id);
    void unlock(const std::thread::id &locking_thread_id);
    void lock_shared(const std::thread::id &locking_thread_id);
    bool try_lock_shared(const std::thread::id &locking_thread_id);
    void unlock_shared(const std::thread::id &locking_thread_id);
};


#endif // _RECURSIVE_SHARED_MUTEX_H
