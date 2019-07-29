// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "recursive_shared_mutex.h"
#include "test_cxx_rsm.h"
#include "timer.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(rsm_simple_tests, TestSetup)

recursive_shared_mutex rsm;

// basic lock and unlock tests
BOOST_AUTO_TEST_CASE(rsm_lock_unlock)
{
    // exclusive lock once
    rsm.lock();

// try to unlock_shared an exclusive lock
// we should error here because exclusive locks can
// be not be unlocked by shared_ unlock method
#ifdef RSM_DEBUG_ASSERTION
    BOOST_CHECK_THROW(rsm.unlock_shared(), std::logic_error);
#endif

    // unlock exclusive lock

    BOOST_CHECK_NO_THROW(rsm.unlock());

    // exclusive lock once
    rsm.lock();

    // try to unlock exclusive lock
    BOOST_CHECK_NO_THROW(rsm.unlock());

#ifdef RSM_DEBUG_ASSERTION
    // try to unlock exclusive lock more times than we locked
    BOOST_CHECK_THROW(rsm.unlock(), std::logic_error);
#endif

    // test complete
}

// basic lock_shared and unlock_shared tests
BOOST_AUTO_TEST_CASE(rsm_lock_shared_unlock_shared)
{
    // lock shared
    rsm.lock_shared();

#ifdef RSM_DEBUG_ASSERTION
    // try to unlock exclusive when we only have shared
    BOOST_CHECK_THROW(rsm.unlock(), std::logic_error);
#endif

    // unlock shared
    rsm.unlock_shared();

#ifdef RSM_DEBUG_ASSERTION
    // we should error here because we are unlocking more times than we locked
    BOOST_CHECK_THROW(rsm.unlock_shared(), std::logic_error);
#endif

    // test complete
}

// basic try_lock tests
BOOST_AUTO_TEST_CASE(rsm_try_lock)
{
    // try lock
    rsm.try_lock();

#ifdef RSM_DEBUG_ASSERTION
    // try to unlock_shared an exclusive lock
    // we should error here because exclusive locks can
    // be not be unlocked by shared_ unlock method
    BOOST_CHECK_THROW(rsm.unlock_shared(), std::logic_error);
#endif

    // unlock exclusive lock
    BOOST_CHECK_NO_THROW(rsm.unlock());

    // try lock
    rsm.try_lock();

    // try to unlock exclusive lock
    BOOST_CHECK_NO_THROW(rsm.unlock());

#ifdef RSM_DEBUG_ASSERTION
    // try to unlock exclusive lock more times than we locked
    BOOST_CHECK_THROW(rsm.unlock(), std::logic_error);
#endif

    // test complete
}

// basic try_lock_shared tests
BOOST_AUTO_TEST_CASE(rsm_try_lock_shared)
{
    // try lock shared
    rsm.try_lock_shared();

#ifdef RSM_DEBUG_ASSERTION
    // unlock exclusive while we have shared lock
    BOOST_CHECK_THROW(rsm.unlock(), std::logic_error);
#endif

    // unlock shared
    BOOST_CHECK_NO_THROW(rsm.unlock_shared());

#ifdef RSM_DEBUG_ASSERTION
    // we should error here because we are unlocking more times than we locked
    BOOST_CHECK_THROW(rsm.unlock_shared(), std::logic_error);
#endif

    // test complete
}

// test locking recursively 100 times for each lock type
BOOST_AUTO_TEST_CASE(rsm_100_lock_test)
{
    uint8_t i = 0;
    // lock
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.lock());
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock());
        --i;
    }

    // lock_shared
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.lock_shared());
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock_shared());
        --i;
    }

    // try_lock
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.try_lock());
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock());
        --i;
    }

    // try_lock_shared
    while (i < 100)
    {
        BOOST_CHECK_NO_THROW(rsm.try_lock_shared());
        ++i;
    }

    while (i > 0)
    {
        BOOST_CHECK_NO_THROW(rsm.unlock_shared());
        --i;
    }

    // test complete
}


BOOST_AUTO_TEST_SUITE_END()
