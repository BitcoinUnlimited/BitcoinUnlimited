
#include "recursive_shared_mutex.h"
#include "utiltime.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(rsm_tests, BasicTestingSetup)

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
    // should still be false because we gamma (third thread)
    BOOST_CHECK_EQUAL(rsm.try_lock(std::this_thread::get_id()), false);
}

void gamma()
{
    rsm.lock_shared(std::this_thread::get_id());
    MilliSleep(5000);
    rsm.unlock_shared(std::this_thread::get_id());
    rsm.lock(std::this_thread::get_id());
    rsm_guarded_vector.push_back(3);
    rsm.unlock(std::this_thread::get_id());
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
    rsm.unlock(std::this_thread::get_id());
    // we should only have a shared lock now. sleep while epsilon catches up
    MilliSleep(3000);
    // we should be able to unlock our shared mutex now with no errors
    BOOST_CHECK_NO_THROW(rsm.unlock_shared(std::this_thread::get_id()));
}

void epsilon()
{
    rsm.lock_shared(std::this_thread::get_id());
    // give time for dela to lock shared (67)
    MilliSleep(5000);
    rsm.unlock_shared(std::this_thread::get_id());
    // sleep for 500 to give delta time to do its thing (71)
    MilliSleep(500);
    // try to lock shared, we should be able to
    bool lockresult = rsm.try_lock_shared(std::this_thread::get_id());
    BOOST_CHECK_EQUAL(lockresult, true);
    // check size
    BOOST_CHECK_EQUAL(rsm_guarded_vector.size(), 4);
    // we only have one lock, but try to unlock twice.
    rsm.unlock_shared(std::this_thread::get_id());
    // on the second unlock we should get an assertion (std::abort)
    BOOST_CHECK_THROW(rsm.unlock_shared(std::this_thread::get_id()), std::logic_error);
    // at this point we should be all unlocked. before we finish the thread lets
    // make sure delta still has it locked by trying to lock
    BOOST_CHECK_EQUAL(rsm.try_lock(std::this_thread::get_id()), false);
}

BOOST_AUTO_TEST_CASE(RsmTest)
{
    std::thread first (alpha);
    // sleep to ensure alpha gets the lock first
    MilliSleep(500);
    std::thread third (gamma);
    MilliSleep(10);
    std::thread second (beta);


    first.join();
    second.join();
    third.join();

    rsm.lock_shared(std::this_thread::get_id());
    for(size_t i = 0; i < rsm_guarded_vector.size(); i++)
    {
        BOOST_CHECK_EQUAL(i, rsm_guarded_vector[i]);
    }
    rsm.unlock_shared(std::this_thread::get_id());

    // end alpha beta gamma thread test. the next test only uses dela and epsilon
    // first reset the vector
    rsm.lock(std::this_thread::get_id());
    rsm_guarded_vector.clear();
    rsm.unlock(std::this_thread::get_id());

    // test automatic internal lock and unlock while requesting
    // exclusive lock when we have a shared lock
    std::thread fourth (delta);
    std::thread fifth (epsilon);

    fifth.join();
    fourth.join();
    // double check vector size is 4 and holds correct elements
    rsm.lock_shared(std::this_thread::get_id());
    for(size_t i = 0; i < rsm_guarded_vector.size(); i++)
    {
        BOOST_CHECK_EQUAL(i, rsm_guarded_vector[i]);
    }
    rsm.unlock_shared(std::this_thread::get_id());
}

BOOST_AUTO_TEST_SUITE_END()
