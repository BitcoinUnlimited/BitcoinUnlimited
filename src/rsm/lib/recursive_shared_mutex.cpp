// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "include/recursive_shared_mutex.h"

////////////////////////
///
/// Private Functions
///

bool recursive_shared_mutex::end_of_exclusive_ownership()
{
    return (_shared_while_exclusive_counter == 0 && _write_counter == 0);
}

bool recursive_shared_mutex::check_for_write_lock(const std::thread::id &locking_thread_id)
{
    return (_write_owner_id == locking_thread_id);
}

bool recursive_shared_mutex::check_for_write_unlock(const std::thread::id &locking_thread_id)
{
    if (_write_owner_id == locking_thread_id)
    {
        if (_shared_while_exclusive_counter == 0)
        {
#ifdef RSM_DEBUG_ASSERTION
            throw std::logic_error("can not unlock_shared more times than we locked for shared ownership while holding "
                                   "exclusive ownership");
#else
            return true;
#endif
        }
        return true;
    }
    return false;
}

bool recursive_shared_mutex::already_has_lock_shared(const std::thread::id &locking_thread_id)
{
    return (_read_owner_ids.find(locking_thread_id) != _read_owner_ids.end());
}

void recursive_shared_mutex::lock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count)
{
    auto it = _read_owner_ids.find(locking_thread_id);
    if (it == _read_owner_ids.end())
    {
        _read_owner_ids.emplace(locking_thread_id, count);
    }
    else
    {
        it->second = it->second + count;
    }
}

void recursive_shared_mutex::unlock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count)
{
    auto it = _read_owner_ids.find(locking_thread_id);
    if (it == _read_owner_ids.end())
    {
#ifdef RSM_DEBUG_ASSERTION
        throw std::logic_error("can not unlock_shared more times than we locked for shared ownership");
#else
        return;
#endif
    }
    it->second = it->second - count;
    if (it->second == 0)
    {
        _read_owner_ids.erase(it);
    }
}

////////////////////////
///
/// Public Functions
///

void recursive_shared_mutex::lock()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::unique_lock<std::mutex> _lock(_mutex);
    if (_write_owner_id == locking_thread_id)
    {
        _write_counter++;
    }
    else
    {
        // Wait until we can set the write-entered.
        _read_gate.wait(_lock, [this] { return end_of_exclusive_ownership(); });

        _write_counter++;
        // Then wait until there are no more readers.
        _write_gate.wait(
            _lock, [this] { return _read_owner_ids.size() == 0 && _promotion_candidate_id == NON_THREAD_ID; });
        _write_owner_id = locking_thread_id;
    }
}

bool recursive_shared_mutex::try_promotion()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::unique_lock<std::mutex> _lock(_mutex);

    if (_write_owner_id == locking_thread_id)
    {
        _write_counter++;
        return true;
    }
    // checking _write_owner_id might be redundant here with the mutex already being locked
    // check if write_counter == 0 to ensure data consistency after promotion
    else if (_promotion_candidate_id == NON_THREAD_ID)
    {
        _promotion_candidate_id = locking_thread_id;
        // Then wait until there are no more readers.
        _promotion_write_gate.wait(_lock,
            [this] { return _read_owner_ids.size() == 1 && already_has_lock_shared(std::this_thread::get_id()); });
        _write_owner_id = locking_thread_id;
        // it is possible that if we cut the line, another thread could have incremented the _write_counter
        // already, so we should check this and decrement + save what they did
        if (_write_counter != 0)
        {
            _write_counter_reserve = _write_counter;
            _write_counter = 0;
        }
        // now increment the _write_counter for our own use
        _write_counter++;
        return true;
    }
    return false;
}

bool recursive_shared_mutex::try_lock()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::unique_lock<std::mutex> _lock(_mutex, std::try_to_lock);

    if (_write_owner_id == locking_thread_id)
    {
        _write_counter++;
        return true;
    }
    else if (_lock.owns_lock() && end_of_exclusive_ownership() && _read_owner_ids.size() == 0 &&
             _promotion_candidate_id == NON_THREAD_ID)
    {
        _write_counter++;
        _write_owner_id = locking_thread_id;
        return true;
    }
    return false;
}

void recursive_shared_mutex::unlock()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::lock_guard<std::mutex> _lock(_mutex);
    // you cannot unlock if you are not the write owner so check that here
    // this might be redundant with the mutex being locked
    if (_write_counter == 0 || _write_owner_id != locking_thread_id)
    {
#ifdef RSM_DEBUG_ASSERTION
        throw std::logic_error("unlock(standard logic) incorrectly called on a thread with no exclusive lock");
#else
        return;
#endif
    }
    if (_promotion_candidate_id != NON_THREAD_ID && _write_owner_id != _promotion_candidate_id)
    {
#ifdef RSM_DEBUG_ASSERTION
        throw std::logic_error("unlock(promotion logic) incorrectly called on a thread with no exclusive lock");
#else
        return;
#endif
    }
    if (_promotion_candidate_id != NON_THREAD_ID)
    {
        _write_counter--;
        if (_write_counter == 0)
        {
#ifdef RSM_DEBUG_ASSERTION
            assert(_shared_while_exclusive_counter == 0);
#endif
            if (_shared_while_exclusive_counter > 0)
            {
                lock_shared_internal(locking_thread_id, _shared_while_exclusive_counter);
                _shared_while_exclusive_counter = 0;
            }
            // reset the write owner id back to a non thread id once we unlock all write locks
            _write_owner_id = NON_THREAD_ID;
            _promotion_candidate_id = NON_THREAD_ID;
            // call notify_all() while mutex is held so that another thread can't
            // lock and unlock the mutex then destroy *this before we make the call.

            // it is possible that if we cut the line, another thread could have incremented the _write_counter
            // already, restore what they did
            if (_write_counter_reserve != 0)
            {
                _write_counter = _write_counter_reserve;
                _write_counter_reserve = 0;
            }

            _read_gate.notify_all();
        }
    }
    else
    {
        _write_counter--;
#ifdef RSM_DEBUG_ASSERTION
        assert(_write_counter_reserve == 0);
#endif
        if (end_of_exclusive_ownership())
        {
            // reset the write owner id back to a non thread id once we unlock all write locks
            _write_owner_id = NON_THREAD_ID;
            // call notify_all() while mutex is held so that another thread can't
            // lock and unlock the mutex then destroy *this before we make the call.

            _read_gate.notify_all();
        }
    }
}

void recursive_shared_mutex::lock_shared()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::unique_lock<std::mutex> _lock(_mutex);
    if (check_for_write_lock(locking_thread_id))
    {
        _shared_while_exclusive_counter++;
        return;
    }
    if (already_has_lock_shared(locking_thread_id))
    {
        lock_shared_internal(locking_thread_id);
    }
    else
    {
        _read_gate.wait(
            _lock, [this] { return end_of_exclusive_ownership() && _promotion_candidate_id == NON_THREAD_ID; });
        lock_shared_internal(locking_thread_id);
    }
}

bool recursive_shared_mutex::try_lock_shared()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::unique_lock<std::mutex> _lock(_mutex, std::try_to_lock);
    if (check_for_write_lock(locking_thread_id))
    {
        _shared_while_exclusive_counter++;
        return true;
    }
    if (already_has_lock_shared(locking_thread_id))
    {
        lock_shared_internal(locking_thread_id);
        return true;
    }
    if (!_lock.owns_lock())
    {
        return false;
    }
    if (end_of_exclusive_ownership() && _promotion_candidate_id == NON_THREAD_ID)
    {
        lock_shared_internal(locking_thread_id);
        return true;
    }
    return false;
}

void recursive_shared_mutex::unlock_shared()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::lock_guard<std::mutex> _lock(_mutex);
    if (check_for_write_unlock(locking_thread_id))
    {
        _shared_while_exclusive_counter--;
        return;
    }
    if (_read_owner_ids.size() == 0)
    {
#ifdef RSM_DEBUG_ASSERTION
        throw std::logic_error("unlock_shared incorrectly called on a thread with no shared lock");
#else
        return;
#endif
    }
    unlock_shared_internal(locking_thread_id);
    if (_promotion_candidate_id != NON_THREAD_ID)
    {
        if (_read_owner_ids.size() == 1 && already_has_lock_shared(_promotion_candidate_id))
        {
            _promotion_write_gate.notify_one();
        }
        else
        {
            _read_gate.notify_one();
        }
    }
    else if (_write_counter != 0 && _promotion_candidate_id == NON_THREAD_ID)
    {
        if (_read_owner_ids.size() == 0)
        {
            _write_gate.notify_one();
        }
        else
        {
            _read_gate.notify_one();
        }
    }
}
