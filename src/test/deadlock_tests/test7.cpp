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

BOOST_FIXTURE_TEST_SUITE(deadlock_test7, EmptySuite)

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

CSharedCriticalSection mutexA;
CSharedCriticalSection mutexB;
CSharedCriticalSection mutexC;

std::atomic<bool> done{false};
std::atomic<int> lock_exceptions{0};
std::atomic<int> readlocks{0};


void Thread1()
{
    READLOCK(mutexA); // 1
    readlocks++;
    while(readlocks != 3) ;
    try
    {
        WRITELOCK(mutexB);
    }
    catch (const std::logic_error&)
    {
        lock_exceptions++;
    }
    while (!done) ;

}

void Thread2()
{
    while(readlocks != 1) ;
    READLOCK(mutexB); // 2
    readlocks++;
    while(readlocks != 3) ;
    try
    {
        WRITELOCK(mutexC);
    }
    catch (const std::logic_error&)
    {
        lock_exceptions++;
    }
    while (!done) ;

}

void Thread3()
{
    while(readlocks != 2) ;
    READLOCK(mutexC); // 3
    readlocks++;
    while(readlocks != 3) ;
    try
    {
        WRITELOCK(mutexA);
    }
    catch (const std::logic_error&)
    {
        lock_exceptions++;
    }
    while (!done) ;
}

BOOST_AUTO_TEST_CASE(TEST_7)
{
    std::thread thread1(Thread1);
    std::thread thread2(Thread2);
    std::thread thread3(Thread3);
    while(!lock_exceptions) ;
    done = true;
    thread1.join();
    thread2.join();
    thread3.join();
    BOOST_CHECK(lock_exceptions == 1);
    lockdata.ordertracker.clear();
}

#else

BOOST_AUTO_TEST_CASE(EMPTY_TEST_7)
{

}

#endif

BOOST_AUTO_TEST_SUITE_END()
