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

BOOST_FIXTURE_TEST_SUITE(self_deadlock_tests, EmptySuite)

BOOST_AUTO_TEST_CASE(TEST_1_SM)
{
    CSharedCriticalSection shared_mutex;
    READLOCK(shared_mutex);
    BOOST_CHECK_THROW(WRITELOCK(shared_mutex), std::logic_error);
}

BOOST_AUTO_TEST_CASE(TEST_1_RSM)
{
    CRecursiveSharedCriticalSection rsm;
    RECURSIVEREADLOCK(rsm);
    BOOST_CHECK_THROW(RECURSIVEWRITELOCK(rsm), std::logic_error);
}

BOOST_AUTO_TEST_CASE(TEST_2)
{
    CSharedCriticalSection shared_mutex;
    WRITELOCK(shared_mutex);
    BOOST_CHECK_THROW(READLOCK(shared_mutex), std::logic_error);
}

BOOST_AUTO_TEST_CASE(TEST_3)
{
    CSharedCriticalSection shared_mutex;
    READLOCK(shared_mutex);
    BOOST_CHECK_THROW(READLOCK(shared_mutex), std::logic_error);
}

BOOST_AUTO_TEST_CASE(TEST_4)
{
    CSharedCriticalSection shared_mutex;
    WRITELOCK(shared_mutex);
    BOOST_CHECK_THROW(WRITELOCK(shared_mutex), std::logic_error);
}

BOOST_AUTO_TEST_SUITE_END()
