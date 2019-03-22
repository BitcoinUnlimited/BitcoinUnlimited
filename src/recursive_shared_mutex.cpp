#include "recursive_shared_mutex.h"

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

void recursive_shared_mutex::lock(const std::thread::id &locking_thread_id)
{
    std::unique_lock<std::mutex> _lock(_mutex);
    if(_write_owner_id == locking_thread_id && _write_counter < SANE_LOCK_LIMIT)
    {
        _write_counter++;
    }
    else
    {
        // Wait until we can set the write-entered.
        _read_gate.wait(_lock, [=]
            {
                return _write_counter == 0 && (_read_counter < SANE_LOCK_LIMIT);
            }
        );

        _write_counter++;
        // Then wait until there are no more readers.
        _write_gate.wait(_lock, [=]
            {
                return _read_counter == 0 && (_write_counter < SANE_LOCK_LIMIT);
            }
        );

        _write_owner_id = locking_thread_id;
    }
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
    else if (_lock.owns_lock() && _write_counter == 0 && _read_counter == 0)
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
    assert( _write_counter != 0 && _write_owner_id == locking_thread_id );
    _write_counter--;
    if(_write_counter == 0)
    {
        // reset the write owner id back to a non thread id once we unlock all write locks
        _write_owner_id = NON_THREAD_ID;
        // call notify_all() while mutex is held so that another thread can't
        // lock and unlock the mutex then destroy *this before we make the call.
        _read_gate.notify_all();
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
            return (_write_counter == 0) && (_read_counter < SANE_LOCK_LIMIT);
        }
    );
    _read_counter++;
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
    if (_write_counter == 0 && _read_counter < SANE_LOCK_LIMIT)
    {
        _read_counter++;
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
    assert( _read_counter > 0 );
    _read_counter--;
    if (_write_counter != 0)
    {
        if (_read_counter == 0)
        {
            _write_gate.notify_one();
        }
        else
        {
            _read_gate.notify_one();
        }
    }
}
