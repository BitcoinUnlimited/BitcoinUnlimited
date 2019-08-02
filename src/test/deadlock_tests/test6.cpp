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

BOOST_FIXTURE_TEST_SUITE(test6, EmptySuite)

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

CSharedCriticalSection mutexA;
CSharedCriticalSection mutexB;

void Thread1()
{
    READLOCK(mutexA);
    MilliSleep(100);
    READLOCK(mutexB);
}

void Thread2()
{
    MilliSleep(50);
    WRITELOCK(mutexB);
    MilliSleep(100);
    BOOST_CHECK_THROW(WRITELOCK(mutexA), std::logic_error);
}

// Thread 1 shared lock A
// Thread 2 exclusive lock B
// Thread 1 request shared lock on B
// Thread 2 request exclusive lock on A, should deadlock here
BOOST_AUTO_TEST_CASE(TEST_6)
{
    std::thread thread1(Thread1);
    std::thread thread2(Thread2);
    thread1.join();
    thread2.join();
}

#else

BOOST_AUTO_TEST_CASE(EMPTY_TEST_6)
{

}

#endif

BOOST_AUTO_TEST_SUITE_END()
