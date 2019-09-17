// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "suite.h"

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <mutex>
#include <shared_mutex>
#include <thread>

BOOST_FIXTURE_TEST_SUITE(deadlock_test9, EmptySuite)

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

CSharedCriticalSection mutexA;
CSharedCriticalSection mutexB;

BOOST_AUTO_TEST_CASE(TEST_9)
{
    /*
    {
    WRITELOCK(mutexA);
    WRITELOCK(mutexB);
    }

    {
    WRITELOCK(mutexB);
    BOOST_CHECK_NO_THROW(WRITELOCK(mutexA));
    }
    lockdata.ordertracker.clear();
    */
}

#else

BOOST_AUTO_TEST_CASE(EMPTY_TEST_9)
{

}

#endif

BOOST_AUTO_TEST_SUITE_END()
