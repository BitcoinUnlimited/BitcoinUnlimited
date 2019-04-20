#include "test/test_bitcoin.h"
#include "test/test_random.h"
#include "persistent_map.h"
#include <boost/test/unit_test.hpp>
#include <map>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(persistent_map_tests, BasicTestingSetup)

typedef persistent_map<int, int> pmii;

BOOST_AUTO_TEST_CASE(simple_cases)
{
    pmii empty;
    BOOST_CHECK_EQUAL(empty.size(), 0);
    BOOST_CHECK(empty.empty());

    pmii one(3, 4);
    BOOST_CHECK_EQUAL(one.size(), 1);
    BOOST_CHECK(! one.empty());
    BOOST_CHECK_EQUAL(one.at(3), 4);
    BOOST_CHECK(one.contains(3));
    BOOST_CHECK(!one.contains(4));

    pmii two = one.insert(5, 6);
    BOOST_CHECK_EQUAL(one.size(), 1);
    BOOST_CHECK(! one.empty());
    BOOST_CHECK_EQUAL(one.at(3), 4);
    BOOST_CHECK(one.contains(3));
    BOOST_CHECK(!one.contains(4));

    BOOST_CHECK_EQUAL(two.size(), 2);
    BOOST_CHECK(! two.empty());
    BOOST_CHECK_EQUAL(two.at(3), 4);
    BOOST_CHECK(two.contains(3));
    BOOST_CHECK(!one.contains(4));
    BOOST_CHECK(two.contains(5));
    BOOST_CHECK_EQUAL(two.at(5), 6);

    BOOST_CHECK_EQUAL(two.rank_of(3), 0);
    BOOST_CHECK_EQUAL(two.rank_of(5), 1);

    pmii::const_iterator iter1 = two.by_rank(0);
    BOOST_CHECK(iter1.key_ptr() != nullptr);
    BOOST_CHECK(iter1.value_ptr() != nullptr);
    BOOST_CHECK_EQUAL(*iter1.key_ptr(), 3);
    BOOST_CHECK_EQUAL(*iter1.value_ptr(), 4);

    pmii::const_iterator iter2 = two.by_rank(1);
    BOOST_CHECK(iter2.key_ptr() != nullptr);
    BOOST_CHECK(iter2.value_ptr() != nullptr);
    BOOST_CHECK_EQUAL(*iter2.key_ptr(), 5);
    BOOST_CHECK_EQUAL(*iter2.value_ptr(), 6);

    BOOST_CHECK(two.by_rank(2) == two.end());
    BOOST_CHECK_THROW(two.rank_of(2), out_of_range);
}

BOOST_AUTO_TEST_CASE(pm_iterator)
{
    BOOST_CHECK(pmii::const_iterator(nullptr, false).value_ptr() == nullptr);
    BOOST_CHECK(pmii::const_iterator(nullptr, false).key_ptr() == nullptr);
    BOOST_CHECK(pmii::const_iterator(nullptr, true).value_ptr() == nullptr);
    BOOST_CHECK(pmii::const_iterator(nullptr, true).key_ptr() == nullptr);
}

void assert_equal1000(const std::map<int, int> &std_map,
                      const pmii& per_map) {
    BOOST_CHECK_EQUAL(std_map.empty(), per_map.empty());
    BOOST_CHECK_EQUAL(std_map.size(), per_map.size());

    std::set<int> ranks_seen;

    BOOST_CHECK(per_map.by_rank(0)!=per_map.end());
    BOOST_CHECK(per_map.by_rank(per_map.size()-1)!=per_map.end());
    BOOST_CHECK(per_map.by_rank(per_map.size()) == per_map.end());
    for (size_t i = 0; i < 1000; i++) {
        bool scontains = std_map.count(i) != 0;
        bool pcontains = per_map.contains(i);
        BOOST_CHECK_EQUAL(scontains, pcontains);
        if (scontains) {
            BOOST_CHECK_EQUAL(std_map.at(i), per_map.at(i));

            size_t rank = per_map.rank_of(i);
            ranks_seen.insert(rank);
            BOOST_CHECK(rank < per_map.size());
            pmii::const_iterator iter = per_map.by_rank(rank);
            BOOST_CHECK(iter.key_ptr() != nullptr);
            BOOST_CHECK(iter.value_ptr() != nullptr);
            BOOST_CHECK_EQUAL(*iter.key_ptr(), i);
            BOOST_CHECK_EQUAL(*iter.value_ptr(), per_map.at(i));
        } else {
            BOOST_CHECK_THROW(std_map.at(i), out_of_range);
            BOOST_CHECK_THROW(per_map.at(i), out_of_range);
            BOOST_CHECK_THROW(per_map.rank_of(i), out_of_range);
            BOOST_CHECK(per_map.at_iter(i) == per_map.end());
        }
    }
    BOOST_CHECK_EQUAL(per_map.size(), ranks_seen.size());
    for (size_t i=0; i < per_map.size(); i++)
        BOOST_CHECK(ranks_seen.count(i)==1);
}

BOOST_AUTO_TEST_CASE(compare_std_map)
{
    std::map<int, int> map500, map1000;
    pmii pm500, pm1000;

    for (size_t i=0; i < 500; i++) {
        int k=insecure_rand() % 1000;
        int v=insecure_rand() % 1000;
        map500[k]=v;
        pm500 = pm500.insert(k, v);
    }
    assert_equal1000(map500, pm500);

    map1000 = map500;
    pm1000 = pm500;
    for (size_t i=0; i < 500; i++) {
        int k=insecure_rand() % 1000;
        int v=insecure_rand() % 1000;
        map1000[k]=v;
        pm1000 = pm1000.insert(k, v);
    }
    assert_equal1000(map1000, pm1000);
    assert_equal1000(map500, pm500);
}

void iterate1_check_for_size(size_t n) {
    BOOST_TEST_MESSAGE("iterate1 check for size:" << n);
    std::vector<int> a;
    BOOST_CHECK(a.begin()==a.end());
    for (size_t i = 0; i < n; i++)
        a.push_back(i);
    std::random_shuffle(a.begin(), a.end());

    pmii pm;
    BOOST_CHECK_THROW(*pm.begin(), out_of_range);
    BOOST_CHECK_THROW(*pm.end(), out_of_range);
    BOOST_CHECK(pm.begin() == pm.end());
    for (auto none : pm) {
        // this should not be executed as persistent_map is empty
        // adding none check to squelch compiler warning about unused variable
        BOOST_CHECK((none!=pmii::const_iterator::value_type()) && false);
    }

    for (auto x : a)
        pm = pm.insert(x, x+100);
    BOOST_CHECK_THROW(*pm.end(), out_of_range);

    if (n>10) {
        // crude tests for log scaling of tree depth
        double expected_height = log2(n);
        BOOST_CHECK(pm.max_depth() < 10 * expected_height);
    }

    int c=0;
    pm.begin();
    pm.end();
    for (pmii::const_iterator i = pm.begin(); i!=pm.end(); ++i) {
        auto pair = *i;
        BOOST_CHECK_EQUAL(pair.first, c);
        BOOST_CHECK_EQUAL(pair.second, c+100);
        BOOST_CHECK_EQUAL(pm.rank_of(c), c);
        pmii::const_iterator iter_rank = pm.by_rank(c);
        BOOST_CHECK(i == iter_rank);
        c++;
    }
    c=0;
    for (auto pair : pm) {
        BOOST_CHECK_EQUAL(pair.first, c);
        BOOST_CHECK_EQUAL(pair.second, c+100);
        c++;
    }
    c=0;
    for (pmii::const_iterator i=pm.begin(); i!=pm.end(); ++i) {
        BOOST_CHECK_EQUAL(*i.key_ptr(), c);
        BOOST_CHECK_EQUAL(*i.value_ptr(), c+100);
        c++;
    }
}

BOOST_AUTO_TEST_CASE(iterate1)
{
    for (size_t i=0; i<20; i++)
        iterate1_check_for_size(i);
    iterate1_check_for_size(1000);
}

BOOST_AUTO_TEST_CASE(removing)
{
    // FIXME: some code dup
    for (size_t size = 0; size < 30; size++) {
        BOOST_TEST_MESSAGE("remove check for size:" << size);
        std::vector<int> a;
        for (size_t i = 0; i < size; i++)
            a.push_back(i);
        std::random_shuffle(a.begin(), a.end());

        pmii pm;

        for (auto x : a)
            pm = pm.insert(x, x+100);

        std::random_shuffle(a.begin(), a.end());

        pmii rm_all = pm;
        size_t i = pm.size();
        size_t j = 0;
        for (auto x : a) {
            BOOST_CHECK(rm_all.contains(x));
            for (size_t k=0; k<a.size(); k++) {
                if (k<j) {
                    BOOST_CHECK(!rm_all.contains(a[k]));
                } else {
                    BOOST_CHECK(rm_all.contains(a[k]));
                }
            }
            BOOST_CHECK_EQUAL(rm_all.at(x), x + 100);
            rm_all = rm_all.remove(x);
            i--;
            BOOST_CHECK(! rm_all.contains(x));
            BOOST_CHECK_EQUAL(rm_all.size(), i);
            j++;
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
