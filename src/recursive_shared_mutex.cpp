#include "recursive_shared_mutex.h"

////////////////////////
///
/// Private Functions
///

bool recursive_shared_mutex::check_for_write_lock(const std::thread::id &locking_thread_id)
{
    if(_write_owner_id == locking_thread_id && _write_counter < SANE_LOCK_LIMIT)
    {
        _write_counter++;
        return true;
    }
    return false;
}

bool recursive_shared_mutex::unlock_if_write_lock(const std::thread::id &locking_thread_id)
{
    if(_write_owner_id == locking_thread_id)
    {
        unlock(locking_thread_id);
        return true;
    }
    return false;
}

void recursive_shared_mutex::lock_shared_internal(const std::thread::id &locking_thread_id)
{
    auto it = _read_owner_ids.find(locking_thread_id);
    if (it == _read_owner_ids.end())
    {
        _read_owner_ids.emplace(locking_thread_id, 1);
    }
    else
    {
        it->second = it->second + 1;
    }
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

void recursive_shared_mutex::unlock_shared_internal(const std::thread::id &locking_thread_id)
{
    auto it = _read_owner_ids.find(locking_thread_id);
    //assert(it != _read_owner_ids.end());

    if (it == _read_owner_ids.end())
    {
        throw std::logic_error( "can not unlock_shared more times than we locked for shared ownership" );
    }

    it->second = it->second - 1;
    if (it->second == 0)
    {
        _read_owner_ids.erase(it);
    }
}

void recursive_shared_mutex::unlock_shared_internal(const std::thread::id &locking_thread_id, const uint64_t &count)
{
    auto it = _read_owner_ids.find(locking_thread_id);
    assert(it != _read_owner_ids.end());
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
    auto it = _auto_unlocked_ids.find(locking_thread_id);
    if (it == _auto_unlocked_ids.end())
    {
        _auto_unlocked_ids.emplace(locking_thread_id, count);
    }
    else
    {
        it->second = it->second + count;
    }
}

uint64_t recursive_shared_mutex::get_auto_lock_count(const std::thread::id &locking_thread_id)
{
    auto it = _auto_unlocked_ids.find(locking_thread_id);
    if (it == _auto_unlocked_ids.end())
    {
        return 0;
    }
    return it->second;
}

void recursive_shared_mutex::unlock_auto_locks(const std::thread::id &locking_thread_id)
{
    auto it = _auto_unlocked_ids.find(locking_thread_id);
    assert(it != _auto_unlocked_ids.end());
    _auto_unlocked_ids.erase(it);
}

////////////////////////
///
/// Public Functions
///

void recursive_shared_mutex::lock(const std::thread::id &locking_thread_id)
{
    std::unique_lock<std::mutex> _lock(_mutex);
    if(_write_owner_id == locking_thread_id && _write_counter < SANE_LOCK_LIMIT)
    {
        _write_counter++;
    }
    else
    {
        //unlock our shared locks and add to the auto list that if did this
        uint64_t lock_count = get_shared_lock_count(locking_thread_id);
        if(lock_count > 0)
        {
            lock_auto_locks(locking_thread_id, lock_count);
            unlock_shared_internal(locking_thread_id, lock_count);
        }

        // Wait until we can set the write-entered.
        _read_gate.wait(_lock, [=]
            {
                return _write_counter == 0;
            }
        );

        _write_counter++;
        // Then wait until there are no more readers.
        _write_gate.wait(_lock, [=]
            {
                return _read_owner_ids.size() == 0;
            }
        );

        _write_owner_id = locking_thread_id;
    }
}

bool recursive_shared_mutex::try_promotion(const std::thread::id &locking_thread_id)
{
    std::unique_lock<std::mutex> _lock(_mutex);

    if(_write_owner_id == locking_thread_id && _write_counter < SANE_LOCK_LIMIT)
    {
        _write_counter++;
        return true;
    }
    // checking _write_owner_id might be redundant here with the mutex already being locked
    // check if write_counter == 0 to ensure data consistency after promotion
    else if (_promotion_candidate_id == NON_THREAD_ID)
    {
        _promotion_candidate_id = locking_thread_id;
        //unlock our shared locks and add to the auto list that if did this
        uint64_t lock_count = get_shared_lock_count(locking_thread_id);
        if(lock_count > 0)
        {
            lock_auto_locks(locking_thread_id, lock_count);
            unlock_shared_internal(locking_thread_id, lock_count);
        }
        // Wait until we can set the write-entered.
        _promotion_read_gate.wait(_lock, [=]
            {
                return _write_promotion_counter == 0;
            }
        );
        _write_promotion_counter++;
        // Then wait until there are no more readers.
        _promotion_write_gate.wait(_lock, [=]
            {
                return _read_owner_ids.size() == 0;
            }
        );
        _write_owner_id = locking_thread_id;
        return true;
    }
    return false;
}

bool recursive_shared_mutex::try_lock(const std::thread::id &locking_thread_id)
{
    std::unique_lock<std::mutex> _lock(_mutex, std::try_to_lock);

    if(_write_owner_id == locking_thread_id && _write_counter < SANE_LOCK_LIMIT)
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

void recursive_shared_mutex::unlock(const std::thread::id &locking_thread_id)
{
    std::lock_guard<std::mutex> _lock(_mutex);
    // you cannot unlock if you are not the write owner so check that here
    // this might be redundant with the mutex being locked
    if (_promotion_candidate_id != NON_THREAD_ID)
    {
        assert(_write_promotion_counter != 0 &&
            _write_owner_id == locking_thread_id &&
            _write_owner_id == _promotion_candidate_id);
    }
    else
    {
        assert(_write_counter != 0 && _write_owner_id == locking_thread_id );
    }

    if (_promotion_candidate_id != NON_THREAD_ID)
    {
        if (_write_promotion_counter > 0)
        {
            _write_promotion_counter--;
        }
        if(_write_promotion_counter == 0)
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
        if(_write_counter == 0)
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
            // call notify_all() while mutex is held so that another thread can't
            // lock and unlock the mutex then destroy *this before we make the call.

            // we dont do anything with the promotion read gate here because you cant try to promote
            // if another thread has an exclusive lock
            _read_gate.notify_all();
        }
    }
}

void recursive_shared_mutex::lock_shared(const std::thread::id &locking_thread_id)
{
    std::unique_lock<std::mutex> _lock(_mutex);
    if (check_for_write_lock(locking_thread_id))
    {
        return;
    }
    _read_gate.wait(_lock, [=]
        {
            return _write_counter == 0;
        }
    );
    lock_shared_internal(locking_thread_id);
}

bool recursive_shared_mutex::try_lock_shared(const std::thread::id &locking_thread_id)
{
    std::unique_lock<std::mutex> _lock(_mutex, std::try_to_lock);
    if (check_for_write_lock(locking_thread_id))
    {
        return true;
    }
    if (!_lock.owns_lock())
    {
        return false;
    }
    if (_write_counter == 0)
    {
        lock_shared_internal(locking_thread_id);
        return true;
    }
    return false;
}

void recursive_shared_mutex::unlock_shared(const std::thread::id &locking_thread_id)
{
    if(unlock_if_write_lock(locking_thread_id))
    {
        return;
    }
    std::lock_guard<std::mutex> _lock(_mutex);
    assert( _read_owner_ids.size() > 0 );
    unlock_shared_internal(locking_thread_id);
    if(_write_promotion_counter != 0 && _promotion_candidate_id != NON_THREAD_ID)
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
