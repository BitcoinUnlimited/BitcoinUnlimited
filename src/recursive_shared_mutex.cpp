#include "recursive_shared_mutex.h"

////////////////////////
///
/// Private Functions
///

bool recursive_shared_mutex::check_for_write_lock(const std::thread::id &locking_thread_id)
{
    if (_write_owner_id == locking_thread_id)
    {
        _shared_while_exclusive_counter++;
        return true;
    }
    return false;
}

bool recursive_shared_mutex::check_for_write_unlock(const std::thread::id &locking_thread_id)
{
    if (_write_owner_id == locking_thread_id)
    {
        if (_shared_while_exclusive_counter == 0)
        {
            throw std::logic_error("can not unlock_shared more times than we locked for shared ownership while holding "
                                   "exclusive ownership");
        }
        _shared_while_exclusive_counter--;
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
    // assert(it != _read_owner_ids.end());
    if (it == _read_owner_ids.end())
    {
        throw std::logic_error("can not unlock_shared more times than we locked for shared ownership");
    }
    it->second = it->second - count;
    if (it->second == 0)
    {
        _read_owner_ids.erase(it);
    }
}

uint64_t recursive_shared_mutex::get_shared_lock_count(const std::thread::id &locking_thread_id)
{
    auto it = _read_owner_ids.find(locking_thread_id);
    if (it == _read_owner_ids.end())
    {
        return 0;
    }
    return it->second;
}

void recursive_shared_mutex::lock_auto_locks(const std::thread::id &locking_thread_id, const uint64_t &count)
{
    if (_auto_unlock_id != NON_THREAD_ID)
    {
        throw std::logic_error("lock_auto_locks incorrectly called while already occupied by another thread");
    }
    _auto_unlock_id = locking_thread_id;
    _auto_unlock_count = count;
}

uint64_t recursive_shared_mutex::get_auto_lock_count(const std::thread::id &locking_thread_id)
{
    if (_auto_unlock_id == locking_thread_id)
    {
        return _auto_unlock_count;
    }
#ifdef DEBUG
    throw std::logic_error("get_auto_lock_count incorrectly called on a thread with no auto locks");
#else
    return 0;
#endif
}

void recursive_shared_mutex::unlock_auto_locks(const std::thread::id &locking_thread_id)
{
    if (_auto_unlock_id == locking_thread_id)
    {
        _auto_unlock_count = 0;
        _auto_unlock_id = NON_THREAD_ID;
        return;
    }
    throw std::logic_error("unlock_auto_locks incorrectly called on a thread with no auto locks");
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
        _read_gate.wait(_lock, [=] { return _write_counter == 0; });

        _write_counter++;
        // Then wait until there are no more readers.
        _write_gate.wait(_lock, [=] { return _read_owner_ids.size() == 0; });

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
        // unlock our shared locks and add to the auto list that if did this
        uint64_t lock_count = get_shared_lock_count(locking_thread_id);
        if (lock_count > 0)
        {
            lock_auto_locks(locking_thread_id, lock_count);
            unlock_shared_internal(locking_thread_id, lock_count);
        }
        // Wait until we can set the write-entered.
        _promotion_read_gate.wait(_lock, [=] { return _write_promotion_counter == 0; });
        _write_promotion_counter++;
        // Then wait until there are no more readers.
        _promotion_write_gate.wait(_lock, [=] { return _read_owner_ids.size() == 0; });
        _write_owner_id = locking_thread_id;
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
    // checking _write_owner_id might be redundant here with the mutex already being locked
    else if (_lock.owns_lock() && _write_counter == 0 && _read_owner_ids.size() == 0)
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
    if (_promotion_candidate_id != NON_THREAD_ID)
    {
        // assert(_write_promotion_counter != 0 && _write_owner_id == locking_thread_id &&
        //       _write_owner_id == _promotion_candidate_id);
        if (_write_promotion_counter == 0 || _write_owner_id != locking_thread_id ||
            _write_owner_id != _promotion_candidate_id)
        {
            throw std::logic_error("unlock(promotion) incorrectly called on a thread with no exclusive lock");
        }
    }
    else
    {
        // assert(_write_counter != 0 && _write_owner_id == locking_thread_id);
        if (_write_counter == 0 || _write_owner_id != locking_thread_id)
        {
            throw std::logic_error("unlock(standard) incorrectly called on a thread with no exclusive lock");
        }
    }

    if (_promotion_candidate_id != NON_THREAD_ID)
    {
        if (_write_promotion_counter > 0)
        {
            _write_promotion_counter--;
        }
        if (_write_promotion_counter == 0)
        {
            // restore auto unlocked locks if they exist
            uint64_t auto_lock_count = get_auto_lock_count(locking_thread_id);
            if (auto_lock_count > 0)
            {
                lock_shared_internal(locking_thread_id, auto_lock_count);
                unlock_auto_locks(locking_thread_id);
            }
            // reset the write owner id back to a non thread id once we unlock all write locks
            _write_owner_id = NON_THREAD_ID;
            _promotion_candidate_id = NON_THREAD_ID;
            // call notify_all() while mutex is held so that another thread can't
            // lock and unlock the mutex then destroy *this before we make the call.

            // we dont do anything with the promotion read gate here because you cant try to promote
            // if another thread has an exclusive lock
            _read_gate.notify_all();
        }
    }
    else
    {
        if (_write_counter > 0)
        {
            _write_counter--;
        }
        if (_write_counter == 0)
        {
            // reset the write owner id back to a non thread id once we unlock all write locks
            _write_owner_id = NON_THREAD_ID;
            // call notify_all() while mutex is held so that another thread can't
            // lock and unlock the mutex then destroy *this before we make the call.

            // we dont do anything with the promotion read gate here because you cant try to promote
            // if another thread has an exclusive lock
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
        return;
    }
    if (already_has_lock_shared(locking_thread_id))
    {
        lock_shared_internal(locking_thread_id);
    }
    else
    {
        _read_gate.wait(_lock, [=] { return _write_counter == 0 && _write_promotion_counter == 0; });
        lock_shared_internal(locking_thread_id);
    }
}

bool recursive_shared_mutex::try_lock_shared()
{
    const std::thread::id &locking_thread_id = std::this_thread::get_id();
    std::unique_lock<std::mutex> _lock(_mutex, std::try_to_lock);
    if (check_for_write_lock(locking_thread_id))
    {
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
    if (_write_counter == 0 && _write_promotion_counter == 0)
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
        return;
    }
    // assert(_read_owner_ids.size() > 0);
    if (_read_owner_ids.size() == 0)
    {
        throw std::logic_error("unlock_shared incorrectly called on a thread with no shared lock");
    }
    unlock_shared_internal(locking_thread_id);
    if (_write_promotion_counter != 0 && _promotion_candidate_id != NON_THREAD_ID)
    {
        if (_read_owner_ids.size() == 0)
        {
            _promotion_write_gate.notify_one();
        }
        else
        {
            _promotion_read_gate.notify_one();
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
