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

// this test is the same as test 5 but using pointers instead of global locks

BOOST_FIXTURE_TEST_SUITE(deadlock_test9, EmptySuite)

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

std::atomic<bool> done{false};
std::atomic<int> lock_exceptions{0};
std::atomic<int> writelocks{0};

void TestThread(CSharedCriticalSection *mutexA, CSharedCriticalSection *mutexB)
{
    WRITELOCK(*mutexA);
    writelocks++;
    while(writelocks != 2) ;
    try
    {
        READLOCK(*mutexB);
    }
    catch (const std::logic_error&)
    {
        lock_exceptions++;
    }
    while (!done) ;
}

BOOST_AUTO_TEST_CASE(TEST_9)
{
    CSharedCriticalSection mutexA;
    CSharedCriticalSection mutexB;
    std::thread thread1(TestThread, &mutexA, &mutexB);
    std::thread thread2(TestThread, &mutexB, &mutexA);
    while(!lock_exceptions) ;
    done = true;
    thread1.join();
    thread2.join();
    BOOST_CHECK(lock_exceptions == 1);
    lockdata.ordertracker.clear();
}

#else

BOOST_AUTO_TEST_CASE(EMPTY_TEST_9)
{

}

#endif

BOOST_AUTO_TEST_SUITE_END()
