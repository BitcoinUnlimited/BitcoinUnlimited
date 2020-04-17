// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "prevector.h"
#include <vector>

#include "serialize.h"
#include "streams.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(prevector_tests, TestingSetup) // BU harmonize suite name with filename

template <unsigned int N, typename T>
class prevector_tester
{
    typedef std::vector<T> realtype;
    realtype real_vector;
    realtype real_vector_alt;

    typedef prevector<N, T> pretype;
    pretype pre_vector;
    pretype pre_vector_alt;

    typedef typename pretype::size_type Size;

    void test()
    {
        const pretype &const_pre_vector = pre_vector;
        BOOST_CHECK_EQUAL(real_vector.size(), pre_vector.size());
        BOOST_CHECK_EQUAL(real_vector.empty(), pre_vector.empty());
        for (Size s = 0; s < real_vector.size(); s++)
        {
            if (real_vector[s] != pre_vector[s])
                BOOST_CHECK(real_vector[s] == pre_vector[s]);
            if (&(pre_vector[s]) != &(pre_vector.begin()[s]))
                BOOST_CHECK(&(pre_vector[s]) == &(pre_vector.begin()[s]));
            if (&(pre_vector[s]) != &*(pre_vector.begin() + s))
                BOOST_CHECK(&(pre_vector[s]) == &*(pre_vector.begin() + s));
            if (&(pre_vector[s]) != &*((pre_vector.end() + s) - real_vector.size()))
                BOOST_CHECK(&(pre_vector[s]) == &*((pre_vector.end() + s) - real_vector.size()));
        }
        // BOOST_CHECK(realtype(pre_vector) == real_vector);
        BOOST_CHECK(pretype(real_vector.begin(), real_vector.end()) == pre_vector);
        BOOST_CHECK(pretype(pre_vector.begin(), pre_vector.end()) == pre_vector);
        size_t pos = 0;
        for (const T &v : pre_vector)
        {
            if (v != real_vector[pos++])
                BOOST_CHECK(v == real_vector[pos++]);
        }
        for (auto i = pre_vector.rbegin(); i != pre_vector.rend(); i++)
        {
            const T &v = *i;
            if (v != real_vector[--pos])
                BOOST_CHECK(v == real_vector[--pos]);
        }
        for (const T &v : const_pre_vector)
        {
            if (v != real_vector[pos++])
                BOOST_CHECK(v == real_vector[pos++]);
        }
        for (auto j = const_pre_vector.rbegin(); j != const_pre_vector.rend(); j++)
        {
            const T &v = *j;
            if (v != real_vector[--pos])
                BOOST_CHECK(v == real_vector[--pos]);
        }
        CDataStream ss1(SER_DISK, 0);
        CDataStream ss2(SER_DISK, 0);
        ss1 << real_vector;
        ss2 << pre_vector;
        BOOST_CHECK_EQUAL(ss1.size(), ss2.size());
        for (Size s = 0; s < ss1.size(); s++)
        {
            if (ss1[s] != ss2[s])
                BOOST_CHECK_EQUAL(ss1[s], ss2[s]);
        }
    }

public:
    void resize(Size s)
    {
        real_vector.resize(s);
        BOOST_CHECK_EQUAL(real_vector.size(), s);
        pre_vector.resize(s);
        BOOST_CHECK_EQUAL(pre_vector.size(), s);
        test();
    }

    void reserve(Size s)
    {
        real_vector.reserve(s);
        BOOST_CHECK(real_vector.capacity() >= s);
        pre_vector.reserve(s);
        BOOST_CHECK(pre_vector.capacity() >= s);
        test();
    }

    void insert(Size position, const T &value)
    {
        real_vector.insert(real_vector.begin() + position, value);
        pre_vector.insert(pre_vector.begin() + position, value);
        test();
    }

    void insert(Size position, Size count, const T &value)
    {
        real_vector.insert(real_vector.begin() + position, count, value);
        pre_vector.insert(pre_vector.begin() + position, count, value);
        test();
    }

    template <typename I>
    void insert_range(Size position, I first, I last)
    {
        real_vector.insert(real_vector.begin() + position, first, last);
        pre_vector.insert(pre_vector.begin() + position, first, last);
        test();
    }

    void erase(Size position)
    {
        real_vector.erase(real_vector.begin() + position);
        pre_vector.erase(pre_vector.begin() + position);
        test();
    }

    void erase(Size first, Size last)
    {
        real_vector.erase(real_vector.begin() + first, real_vector.begin() + last);
        pre_vector.erase(pre_vector.begin() + first, pre_vector.begin() + last);
        test();
    }

    void update(Size pos, const T &value)
    {
        real_vector[pos] = value;
        pre_vector[pos] = value;
        test();
    }

    void push_back(const T &value)
    {
        real_vector.push_back(value);
        pre_vector.push_back(value);
        test();
    }

    void pop_back()
    {
        real_vector.pop_back();
        pre_vector.pop_back();
        test();
    }

    void clear()
    {
        real_vector.clear();
        pre_vector.clear();
    }

    void assign(Size n, const T &value)
    {
        real_vector.assign(n, value);
        pre_vector.assign(n, value);
    }

    Size size() { return real_vector.size(); }
    Size capacity() { return pre_vector.capacity(); }
    void shrink_to_fit()
    {
        pre_vector.shrink_to_fit();
        test();
    }

    void swap()
    {
        real_vector.swap(real_vector_alt);
        pre_vector.swap(pre_vector_alt);
        test();
    }

    void move()
    {
        real_vector = std::move(real_vector_alt);
        real_vector_alt.clear();
        pre_vector = std::move(pre_vector_alt);
        pre_vector_alt.clear();
    }

    void copy()
    {
        real_vector = real_vector_alt;
        pre_vector = pre_vector_alt;
    }

    void resize_uninitialized(realtype values)
    {
        size_t r = values.size();
        size_t s = real_vector.size() / 2;
        if (real_vector.capacity() < s + r)
        {
            real_vector.reserve(s + r);
        }
        real_vector.resize(s);
        pre_vector.resize_uninitialized(s);
        for (auto v : values)
        {
            real_vector.push_back(v);
        }
        auto p = pre_vector.size();
        pre_vector.resize_uninitialized(p + r);
        for (auto v : values)
        {
            pre_vector[p] = v;
            ++p;
        }
        test();
    }
};

BOOST_AUTO_TEST_CASE(PrevectorTestInt)
{
    for (int j = 0; j < 64; j++)
    {
        prevector_tester<8, int> test;
        for (int i = 0; i < 2048; i++)
        {
            if (InsecureRandBits(2) == 0)
            {
                test.insert(InsecureRandRange(test.size() + 1), InsecureRand32());
            }
            if (test.size() > 0 && InsecureRandBits(2) == 1)
            {
                test.erase(InsecureRandRange(test.size()));
            }
            if (InsecureRandBits(3) == 2)
            {
                int new_size = std::max<int>(0, std::min<int>(30, test.size() + (InsecureRandRange(5)) - 2));
                test.resize(new_size);
            }
            if (InsecureRandBits(3) == 3)
            {
                test.insert(InsecureRandRange(test.size() + 1), 1 + InsecureRandBool(), InsecureRand32());
            }
            if (InsecureRandBits(3) == 4)
            {
                int del = std::min<int>(test.size(), 1 + (InsecureRandBool()));
                int beg = InsecureRandRange(test.size() + 1 - del);
                test.erase(beg, beg + del);
            }
            if (InsecureRandBits(4) == 5)
            {
                test.push_back(InsecureRand32());
            }
            if (test.size() > 0 && InsecureRandBits(4) == 6)
            {
                test.pop_back();
            }
            if (InsecureRandBits(5) == 7)
            {
                int values[4];
                int num = 1 + (InsecureRandBits(2));
                for (int k = 0; k < num; k++)
                {
                    values[k] = InsecureRand32();
                }
                test.insert_range(InsecureRandRange(test.size() + 1), values, values + num);
            }
            if (InsecureRandBits(5) == 8)
            {
                int del = std::min<int>(test.size(), 1 + (InsecureRandBits(2)));
                int beg = InsecureRandRange(test.size() + 1 - del);
                test.erase(beg, beg + del);
            }
            if (InsecureRandBits(5) == 9)
            {
                test.reserve(InsecureRandBits(5));
            }
            if (InsecureRandBits(6) == 10)
            {
                test.shrink_to_fit();
            }
            if (test.size() > 0)
            {
                test.update(InsecureRandRange(test.size()), InsecureRand32());
            }
            if (InsecureRandBits(10) == 11)
            {
                test.clear();
            }
            if (InsecureRandBits(9) == 12)
            {
                test.assign(InsecureRandBits(5), InsecureRand32());
            }
            if (InsecureRandBits(3) == 3)
            {
                test.swap();
            }
            if (InsecureRandBits(4) == 8)
            {
                test.copy();
            }
            if (InsecureRandBits(5) == 18)
            {
                test.move();
            }
            if (InsecureRandBits(5) == 19)
            {
                unsigned int num = 1 + (InsecureRandBits(4));
                std::vector<int> values(num);
                for (auto &v : values)
                {
                    v = InsecureRand32();
                }
                test.resize_uninitialized(values);
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
