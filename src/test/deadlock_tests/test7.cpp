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

BOOST_FIXTURE_TEST_SUITE(test7, EmptySuite)

CSharedCriticalSection mutexA;
CSharedCriticalSection mutexB;
CSharedCriticalSection mutexC;

void Thread1()
{
    READLOCK(mutexA); // 1
    MilliSleep(150);
    WRITELOCK(mutexB); // 4
    MilliSleep(1000);
}

void Thread2()
{
    MilliSleep(50);
    READLOCK(mutexB); // 2
    MilliSleep(150);
    WRITELOCK(mutexC); // 5
    MilliSleep(1000);
}

void Thread3()
{
    MilliSleep(100);
    READLOCK(mutexC); // 3
    MilliSleep(150);
    BOOST_CHECK_THROW(WRITELOCK(mutexA), std::logic_error); // 6
}

// Thread 1 shared lock A
// Thread 2 shared lock B
// Thread 3 shared lock C
// Thread 1 request exclusive lock on B
// Thread 2 request exclusive lock on C
// Thread 3 request exclusive lock on A, should deadlock here
BOOST_AUTO_TEST_CASE(TEST_7)
{
    std::thread thread1(Thread1);
    std::thread thread2(Thread2);
    std::thread thread3(Thread3);
    thread1.join();
    thread2.join();
    thread3.join();
}

BOOST_AUTO_TEST_SUITE_END()
