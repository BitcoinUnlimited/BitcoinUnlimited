// Copyright (c) 2019 Greg Griffith
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

BOOST_FIXTURE_TEST_SUITE(test5, EmptySuite)

CSharedCriticalSection mutexA;
CSharedCriticalSection mutexB;

void Thread1()
{
    WRITELOCK(mutexA);
    MilliSleep(100);
    READLOCK(mutexB);
}

void Thread2()
{
    MilliSleep(50);
    WRITELOCK(mutexB);
    MilliSleep(100);
    BOOST_CHECK_THROW(READLOCK(mutexA), std::logic_error);
}

// Thread 1 exclusive lock A
// Thread 2 exclusive lock B
// Thread 1 request shared on B
// Thread 2 request shared on A, should deadlock here
BOOST_AUTO_TEST_CASE(TEST_5)
{
    std::thread thread1(Thread1);
    std::thread thread2(Thread2);
    thread1.join();
    thread2.join();
}

BOOST_AUTO_TEST_SUITE_END()
