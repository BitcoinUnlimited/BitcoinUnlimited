
#include "recursive_shared_mutex.h"
#include "utiltime.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(rsm_promotion_tests, BasicTestingSetup)

recursive_shared_mutex rsm;
std::vector<int> rsm_guarded_vector;

void alpha()
{
    // lock and add a number
    rsm.lock();
    rsm_guarded_vector.push_back(0);

    // recursive lock and add another number
    rsm.lock();
    rsm_guarded_vector.push_back(1);

    // lock shared and add a number while holding write lock
    // the lock should internally interpret this as a 3rd exclusive lock
    rsm.lock_shared();
    rsm_guarded_vector.push_back(2);
    // sleep 3 seconds
    MilliSleep(3000);

    // our third lock is a shared but because we had write lock it should
    // have internally converted to a write lock so we should be able to unlock
    // it as such
    rsm.unlock_shared();
    rsm.unlock();
    rsm.unlock();
}

void beta()
{
    rsm.lock_shared();
    MilliSleep(100);
    // should be false because we have locked shared already
    BOOST_CHECK_EQUAL(rsm.try_lock(), false);
    rsm.unlock_shared();
    // should still be false because gamma has a shared lock
    BOOST_CHECK_EQUAL(rsm.try_lock(), false);
}

void gamma()
{
    // we lock shared here, we are waiting for alpha to release exclusive lock
    rsm.lock_shared();
    MilliSleep(5000);
    // at this point alpha and beta should be done
    rsm.unlock_shared();
    rsm.lock();
    rsm_guarded_vector.push_back(3);
    rsm.unlock();
}

/*
 * if a thread locks shared while it has exclusive it will
 * internally add another exclusive lock instead.
 *
 * This tests tests internal shared lock conversion to exclusive when exclusive is
 * already held and some basic blocking between threads
 */
BOOST_AUTO_TEST_CASE(rsm_unlock_shared_with_unlock)
{
    // clear the data vector at test start
    rsm_guarded_vector.clear();

    std::thread first(alpha);
    // sleep to ensure alpha gets the lock first
    MilliSleep(500);
    std::thread third(gamma);
    MilliSleep(10);
    std::thread second(beta);
    // wait for all threads to join
    first.join();
    second.join();
    third.join();
    // verify everything locked in order
    rsm.lock_shared();
    for (size_t i = 0; i < rsm_guarded_vector.size(); i++)
    {
        BOOST_CHECK_EQUAL(i, rsm_guarded_vector[i]);
    }
    rsm.unlock_shared();
}

void zeta()
{
    rsm.lock_shared();
    // give time for theta to lock shared, eta to lock, and theta to ask for promotion
    MilliSleep(4000);
    rsm.unlock_shared();
}

void eta()
{
    rsm.lock();
    rsm_guarded_vector.push_back(4);
    rsm.unlock();
}

void theta()
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
 * exclusive lock even if aother threads are waiting using lock()
 *
 * This test covers lock promotion from shared to exclusive.
 *
 */

BOOST_AUTO_TEST_CASE(rsm_try_promotion)
{
    // clear the data vector at test start
    rsm_guarded_vector.clear();
    // test promotions
    std::thread sixth(zeta);
    MilliSleep(250);
    std::thread eighth(theta);
    MilliSleep(250);
    std::thread seventh(eta);

    sixth.join();
    seventh.join();
    eighth.join();

    // 7 was added by the promoted thread, it should appear first in the vector
    rsm.lock_shared();
    BOOST_CHECK_EQUAL(7, rsm_guarded_vector[0]);
    BOOST_CHECK_EQUAL(4, rsm_guarded_vector[1]);
    rsm.unlock_shared();
}

BOOST_AUTO_TEST_SUITE_END()
