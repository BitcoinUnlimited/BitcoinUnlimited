// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "recursive_shared_mutex.h"
#include "test_cxx_rsm.h"
#include "timer.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(rsm_promotion_tests, TestSetup)

recursive_shared_mutex rsm;
std::vector<int> rsm_guarded_vector;

void helper_fail() { BOOST_CHECK_EQUAL(rsm.try_lock(), false); }
void helper_pass()
{
    BOOST_CHECK_EQUAL(rsm.try_lock(), true);
    // unlock the try_lock
    rsm.unlock();
}

// test locking shared while holding exclusive ownership
// we should require an equal number of unlock_shared for each lock_shared
BOOST_AUTO_TEST_CASE(rsm_lock_shared_while_exclusive_owner)
{
    // lock exclusive 3 times
    rsm.lock();
    rsm.lock();
    rsm.lock();

    // lock_shared twice
    rsm.lock_shared();
    rsm.lock_shared();

    // it should require 3 unlocks and 2 unlock_shareds to have another thread lock exclusive

    // dont unlock exclusive enough times
    rsm.unlock();
    rsm.unlock();
    rsm.unlock_shared();
    rsm.unlock_shared();

    // we expect helper_fail to fail
    std::thread one(helper_fail);
    one.join();

    // relock
    rsm.lock();
    rsm.lock();
    rsm.lock_shared();
    rsm.lock_shared();

    // now try not unlocking shared enough times
    rsm.unlock();
    rsm.unlock();
    rsm.unlock();
    rsm.unlock_shared();

    // again we expect helper fail to fail
    std::thread two(helper_fail);
    two.join();

    // unlock the last shared
    rsm.unlock_shared();

    // helper pass should pass now
    std::thread three(helper_pass);
    three.join();
}

void shared_only()
{
    rsm.lock_shared();
    // give time for theta to lock shared, eta to lock, and theta to ask for promotion
    MilliSleep(4000);
    rsm.unlock_shared();
}

void exclusive_only()
{
    rsm.lock();
    rsm_guarded_vector.push_back(4);
    rsm.unlock();
}

void promoting_thread()
{
    rsm.lock_shared();
    // give time for eta to get in line to lock exclusive
    MilliSleep(500);
    bool promoted = rsm.try_promotion();
    BOOST_CHECK_EQUAL(promoted, true);
    rsm_guarded_vector.push_back(7);
    rsm.unlock();
    rsm.unlock_shared();
}

/*
 * if a thread askes for a promotion while no other thread
 * is currently asking for a promotion it will be put in line to grab the next
 * exclusive lock even if another threads are waiting using lock()
 *
 * This test covers lock promotion from shared to exclusive.
 *
 */

BOOST_AUTO_TEST_CASE(rsm_try_promotion)
{
    // clear the data vector at test start
    rsm_guarded_vector.clear();
    // test promotions
    std::thread one(shared_only);
    MilliSleep(250);
    std::thread two(promoting_thread);
    MilliSleep(250);
    std::thread three(exclusive_only);

    one.join();
    two.join();
    three.join();

    // 7 was added by the promoted thread, it should appear first in the vector
    rsm.lock_shared();
    BOOST_CHECK_EQUAL(7, rsm_guarded_vector[0]);
    BOOST_CHECK_EQUAL(4, rsm_guarded_vector[1]);
    rsm.unlock_shared();
}

BOOST_AUTO_TEST_SUITE_END()
