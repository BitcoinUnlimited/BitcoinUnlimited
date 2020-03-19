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

BOOST_FIXTURE_TEST_SUITE(self_deadlock_tests, EmptySuite)

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
#endif
// shared lock a shared mutex
// then try to exclusive lock the same shared mutex while holding shared lock,
// should self deadlock
BOOST_AUTO_TEST_CASE(TEST_1_SM)
{
    CSharedCriticalSection shared_mutex;
    READLOCK(shared_mutex);
    BOOST_CHECK_THROW(WRITELOCK(shared_mutex), std::logic_error);
}

// shared lock a RSM
// then try to exclusive lock the RSM while holding shared lock, no promotion,
// should self deadlock
BOOST_AUTO_TEST_CASE(TEST_1_RSM)
{
    CRecursiveSharedCriticalSection rsm;
    RECURSIVEREADLOCK(rsm);
    BOOST_CHECK_THROW(RECURSIVEWRITELOCK(rsm), std::logic_error);
}

// exclusive lock a shared mutex
// then try to shared lock the same shared mutex while holding the exclusive
// lock, should self deadlock
BOOST_AUTO_TEST_CASE(TEST_2)
{
    CSharedCriticalSection shared_mutex;
    WRITELOCK(shared_mutex);
    BOOST_CHECK_THROW(READLOCK(shared_mutex), std::logic_error);
}

// shared lock a shared mutex
// then try to shared lock the same shared mutex while holding the original
// shared lock, should self deadlock, no recursion allowed in a shared mutex
BOOST_AUTO_TEST_CASE(TEST_3)
{
    CSharedCriticalSection shared_mutex;
    READLOCK(shared_mutex);
    BOOST_CHECK_THROW(READLOCK(shared_mutex), std::logic_error);
}

// exclusive lock a shared mutex
// then try to exclusive likc the same shared mutex while holding the original
// exclusive lock, should self deadlock, no recursion allowed in a shared mutex
BOOST_AUTO_TEST_CASE(TEST_4)
{
    CSharedCriticalSection shared_mutex;
    WRITELOCK(shared_mutex);
    BOOST_CHECK_THROW(WRITELOCK(shared_mutex), std::logic_error);
    lockdata.ordertracker.clear();
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif // end clang ifdef

#else

BOOST_AUTO_TEST_CASE(EMPTY_TEST_1_4)
{

}

#endif

BOOST_AUTO_TEST_SUITE_END()
