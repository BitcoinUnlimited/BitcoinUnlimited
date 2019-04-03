
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
    rsm.lock(std::this_thread::get_id());
    rsm_guarded_vector.push_back(0);

    // recursive lock and add another number
    rsm.lock(std::this_thread::get_id());
    rsm_guarded_vector.push_back(1);

    // lock shared and add a number while holding write lock
    // the lock should internally interpret this as a 3rd exclusive lock
    rsm.lock_shared(std::this_thread::get_id());
    rsm_guarded_vector.push_back(2);
    // sleep 3 seconds
    MilliSleep(3000);

    // our third lock is a shared but because we had write lock it should
    // have internally converted to a write lock so we should be able to unlock
    // it as such
    rsm.unlock(std::this_thread::get_id());
    rsm.unlock(std::this_thread::get_id());
    rsm.unlock(std::this_thread::get_id());
}

void beta()
{
    rsm.lock_shared(std::this_thread::get_id());
    MilliSleep(100);
    // should be false because we have locked shared already
    BOOST_CHECK_EQUAL(rsm.try_lock(std::this_thread::get_id()), false);
    rsm.unlock_shared(std::this_thread::get_id());
    // should still be false because gamma has a shared lock
    BOOST_CHECK_EQUAL(rsm.try_lock(std::this_thread::get_id()), false);
}

void gamma()
{
    // we lock shared here, we are waiting for alpha to release exclusive lock
    rsm.lock_shared(std::this_thread::get_id());
    MilliSleep(5000);
    // at this point alpha and beta should be done
    rsm.unlock_shared(std::this_thread::get_id());
    rsm.lock(std::this_thread::get_id());
    rsm_guarded_vector.push_back(3);
    rsm.unlock(std::this_thread::get_id());
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
    rsm.lock_shared(std::this_thread::get_id());
    for (size_t i = 0; i < rsm_guarded_vector.size(); i++)
    {
        BOOST_CHECK_EQUAL(i, rsm_guarded_vector[i]);
    }
    rsm.unlock_shared(std::this_thread::get_id());
}


void delta()
{
    // sleep to ensure epsilon got a shared lock in. we can test this
    // by trying to share lock, should return false
    MilliSleep(200);
    BOOST_CHECK_EQUAL(rsm.try_lock(std::this_thread::get_id()), false);
    // lock shared ourselves, there should be 2 shared locks at this time
    rsm.lock_shared(std::this_thread::get_id());
    // we sleep to ensure epsilon unlocked
    MilliSleep(3000);
    // try to request a lock change which should be handled internally
    rsm.lock(std::this_thread::get_id());
    // if this was successful we test that we have this lock by checking the
    // size in the other thread
    rsm_guarded_vector.push_back(0);
    rsm_guarded_vector.push_back(1);
    rsm_guarded_vector.push_back(2);
    rsm_guarded_vector.push_back(3);
    // we have a exclusive lock, unlock it
    rsm.unlock(std::this_thread::get_id());
    // we should only have a shared lock now. sleep while epsilon catches up
    MilliSleep(3000);
    // we should be able to unlock our shared mutex now with no errors
    BOOST_CHECK_NO_THROW(rsm.unlock_shared(std::this_thread::get_id()));
}

void epsilon()
{
    rsm.lock_shared(std::this_thread::get_id());
    // give time for dela to lock shared
    MilliSleep(5000);
    rsm.unlock_shared(std::this_thread::get_id());
    // sleep for 500 to give delta time for exclusive
    MilliSleep(500);
    // try to lock shared, we should be able to, delta should be waiting for catch up
    bool lockresult = rsm.try_lock_shared(std::this_thread::get_id());
    BOOST_CHECK_EQUAL(lockresult, true);
    // check size
    BOOST_CHECK_EQUAL(rsm_guarded_vector.size(), 4);
    // we only have one lock, but try to unlock twice.
    rsm.unlock_shared(std::this_thread::get_id());
    // on the second unlock we should get a logic error
    BOOST_CHECK_THROW(rsm.unlock_shared(std::this_thread::get_id()), std::logic_error);
    // at this point we should be all unlocked. before we finish the thread lets
    // make sure delta properly restored shared lock when it unlocked exclusive by
    // trying to lock exclusive right now. it should fail if delta has shared lock
    BOOST_CHECK_EQUAL(rsm.try_lock(std::this_thread::get_id()), false);
}

/*
 * if a thread locks exclusive while it has shared it will
 * internally unlock shared and lock exclusive and once that exclusive is unlocked
 * the shared lock will automatically resume.
 *
 * This test covers moving from shared to exclusive locks with no promotion and
 * unlock of shared before trying to lock exclusive.
 *
 */

BOOST_AUTO_TEST_CASE(rsm_shared_to_exclusive_no_promotion)
{
    // clear the data vector at test start
    rsm_guarded_vector.clear();

    // test automatic internal lock and unlock while requesting
    // exclusive lock when we have a shared lock
    std::thread fourth(delta);
    std::thread fifth(epsilon);

    fifth.join();
    fourth.join();
    // double check vector size is 4 and holds correct elements
    rsm.lock_shared(std::this_thread::get_id());
    for (size_t i = 0; i < rsm_guarded_vector.size(); i++)
    {
        BOOST_CHECK_EQUAL(i, rsm_guarded_vector[i]);
    }
    rsm.unlock_shared(std::this_thread::get_id());
}


void zeta()
{
    rsm.lock_shared(std::this_thread::get_id());
    // give time for theta to lock shared, eta to lock, and theta to ask for promotion
    MilliSleep(4000);
    rsm.unlock_shared(std::this_thread::get_id());
}

void eta()
{
    rsm.lock(std::this_thread::get_id());
    rsm_guarded_vector.push_back(4);
    rsm.unlock(std::this_thread::get_id());
}

void theta()
{
    rsm.lock_shared(std::this_thread::get_id());
    // give time for eta to get in line to lock exclusive
    MilliSleep(500);
    bool promoted = rsm.try_promotion(std::this_thread::get_id());
    BOOST_CHECK_EQUAL(promoted, true);
    rsm_guarded_vector.push_back(7);
    rsm.unlock(std::this_thread::get_id());
    rsm.unlock_shared(std::this_thread::get_id());
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
    rsm.lock_shared(std::this_thread::get_id());
    BOOST_CHECK_EQUAL(7, rsm_guarded_vector[0]);
    BOOST_CHECK_EQUAL(4, rsm_guarded_vector[1]);
    rsm.unlock_shared(std::this_thread::get_id());
}

BOOST_AUTO_TEST_SUITE_END()
