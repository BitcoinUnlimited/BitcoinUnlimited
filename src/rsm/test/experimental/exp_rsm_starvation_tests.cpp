// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "exp_recursive_shared_mutex.h"
#include "test_cxx_rsm.h"
#include "timer.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(exp_rsm_starvation_tests, TestSetup)

class exp_rsm_watcher : public exp_recursive_shared_mutex
{
public:
    size_t get_shared_owners_count() { return _read_owner_ids.size(); }
};

exp_rsm_watcher rsm;
std::vector<int> rsm_guarded_vector;

void shared_only()
{
    rsm.lock_shared();
    // give time for theta to lock shared, eta to lock, and theta to ask for promotion
    MilliSleep(2000);
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
    MilliSleep(100);
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
 * This test covers blocking of additional shared ownership aquisitions while
 * a thread is waiting for promotion.
 *
 */

BOOST_AUTO_TEST_CASE(rsm_test_starvation)
{
    // clear the data vector at test start
    rsm_guarded_vector.clear();

    // start up intial shared thread to block immidiate exclusive grabbing
    std::thread one(shared_only);
    std::thread two(shared_only);
    MilliSleep(50);
    std::thread three(promoting_thread);
    MilliSleep(50);
    std::thread four(exclusive_only);
    MilliSleep(75);
    // we should always get 3 because five, six, and seven should be blocked by
    // three promotion request leaving only one, two, and three with shared ownership
    BOOST_CHECK_EQUAL(rsm.get_shared_owners_count(), 3);
    std::thread five(shared_only);
    BOOST_CHECK_EQUAL(rsm.get_shared_owners_count(), 3);
    std::thread six(shared_only);
    BOOST_CHECK_EQUAL(rsm.get_shared_owners_count(), 3);
    std::thread seven(shared_only);
    BOOST_CHECK_EQUAL(rsm.get_shared_owners_count(), 3);

    one.join();
    two.join();
    three.join();
    four.join();
    five.join();
    six.join();
    seven.join();

    // 7 was added by the promoted thread, it should appear first in the vector
    rsm.lock_shared();
    BOOST_CHECK_EQUAL(7, rsm_guarded_vector[0]);
    BOOST_CHECK_EQUAL(4, rsm_guarded_vector[1]);
    rsm.unlock_shared();
}

BOOST_AUTO_TEST_SUITE_END()
