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

BOOST_FIXTURE_TEST_SUITE(deadlock_test10, EmptySuite)

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

CSharedCriticalSection mutexA;
CSharedCriticalSection mutexB;

void Thread1()
{
    {
    WRITELOCK(mutexA);
    WRITELOCK(mutexB);
    }
}

void Thread2()
{
    {
    WRITELOCK(mutexB);
    BOOST_CHECK_THROW(WRITELOCK(mutexA), std::logic_error);
    }
}


BOOST_AUTO_TEST_CASE(TEST_10)
{
    std::thread thread1(Thread1);
    thread1.join();
    std::thread thread2(Thread2);
    thread2.join();
    lockdata.ordertracker.clear();
}

#else

BOOST_AUTO_TEST_CASE(EMPTY_TEST_10)
{

}

#endif

BOOST_AUTO_TEST_SUITE_END()
