
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
}

BOOST_AUTO_TEST_SUITE_END()
