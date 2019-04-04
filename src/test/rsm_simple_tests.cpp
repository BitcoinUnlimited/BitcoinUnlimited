
#include "recursive_shared_mutex.h"
#include "utiltime.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(rsm_simple_tests, BasicTestingSetup)

recursive_shared_mutex rsm;

// basic lock and unlock tests
BOOST_AUTO_TEST_CASE(rsm_lock_unlock)
{
    // exclusive lock once
    rsm.lock(std::this_thread::get_id());

    // try to unlock_shared an exclusive lock
    // we should error here because exclusive locks can
    // be not be unlocked by shared_ unlock method
    BOOST_CHECK_THROW(rsm.unlock_shared(std::this_thread::get_id()), std::logic_error);

    // unlock exclusive lock

    BOOST_CHECK_NO_THROW(rsm.unlock(std::this_thread::get_id()));

    // exclusive lock once
    rsm.lock(std::this_thread::get_id());

    // try to unlock exclusive lock
    BOOST_CHECK_NO_THROW(rsm.unlock(std::this_thread::get_id()));

    // try to unlock exclusive lock more times than we locked
    BOOST_CHECK_THROW(rsm.unlock(std::this_thread::get_id()), std::logic_error);

    // test complete
}

// basic lock_shared and unlock_shared tests
BOOST_AUTO_TEST_CASE(rsm_lock_shared_unlock_shared)
{
    // lock shared
    rsm.lock_shared(std::this_thread::get_id());

    // try to unlock exclusive when we only have shared
    BOOST_CHECK_THROW(rsm.unlock(std::this_thread::get_id()), std::logic_error);

    // unlock shared
    rsm.unlock_shared(std::this_thread::get_id());

    // we should error here because we are unlocking more times than we locked
    BOOST_CHECK_THROW(rsm.unlock_shared(std::this_thread::get_id()), std::logic_error);

    // test complete
}

// basic try_lock tests
BOOST_AUTO_TEST_CASE(rsm_try_lock)
{
    // try lock
    rsm.try_lock(std::this_thread::get_id());

    // try to unlock_shared an exclusive lock
    // we should error here because exclusive locks can
    // be not be unlocked by shared_ unlock method
    BOOST_CHECK_THROW(rsm.unlock_shared(std::this_thread::get_id()), std::logic_error);

    // unlock exclusive lock
    BOOST_CHECK_NO_THROW(rsm.unlock(std::this_thread::get_id()));

    // try lock
    rsm.try_lock(std::this_thread::get_id());

    // try to unlock exclusive lock
    BOOST_CHECK_NO_THROW(rsm.unlock(std::this_thread::get_id()));

    // try to unlock exclusive lock more times than we locked
    BOOST_CHECK_THROW(rsm.unlock(std::this_thread::get_id()), std::logic_error);

    // test complete
}

// basic try_lock_shared tests
BOOST_AUTO_TEST_CASE(rsm_try_lock_shared)
{
    // try lock shared
    rsm.try_lock_shared(std::this_thread::get_id());

    // unlock exclusive while we have shared lock
    BOOST_CHECK_THROW(rsm.unlock(std::this_thread::get_id()), std::logic_error);

    // unlock shared
    BOOST_CHECK_NO_THROW(rsm.unlock_shared(std::this_thread::get_id()));

    // we should error here because we are unlocking more times than we locked
    BOOST_CHECK_THROW(rsm.unlock_shared(std::this_thread::get_id()), std::logic_error);

    // test complete
}

// test locking recursively 100 times for each lock type
BOOST_AUTO_TEST_CASE(rsm_100_lock_test)
{
    uint8_t i = 0;
    // lock
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.lock(std::this_thread::get_id()));
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock(std::this_thread::get_id()));
        --i;
    }

    // lock_shared
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.lock_shared(std::this_thread::get_id()));
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock_shared(std::this_thread::get_id()));
        --i;
    }

    // try_lock
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.try_lock(std::this_thread::get_id()));
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock(std::this_thread::get_id()));
        --i;
    }

    // try_lock_shared
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.try_lock_shared(std::this_thread::get_id()));
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock_shared(std::this_thread::get_id()));
        --i;
    }

    // test complete
}


BOOST_AUTO_TEST_SUITE_END()
