// Copyright (c) 2015-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <prevector.h>
#include <type_traits>

#include <bench/bench.h>

// GCC 4.8 is missing some C++11 type_traits,
// https://www.gnu.org/software/gcc/gcc-5/changes.html
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 5
#define IS_TRIVIALLY_CONSTRUCTIBLE std::has_trivial_default_constructor
#else
#define IS_TRIVIALLY_CONSTRUCTIBLE std::is_trivially_default_constructible
#endif

struct nontrivial_t {
    int x;
    nontrivial_t() :x(-1) {}
};
static_assert(!IS_TRIVIALLY_CONSTRUCTIBLE<nontrivial_t>::value,
              "expected nontrivial_t to not be trivially constructible");

typedef unsigned char trivial_t;
static_assert(IS_TRIVIALLY_CONSTRUCTIBLE<trivial_t>::value,
              "expected trivial_t to be trivially constructible");

template <typename T>
static void PrevectorDestructor(benchmark::State& state)
{
    while (state.KeepRunning()) {
        for (auto x = 0; x < 1000; ++x) {
            prevector<28, T> t0;
            prevector<28, T> t1;
            t0.resize(28);
            t0.resize(28);
            t1.resize(29);
        }
    }
}

template <typename T>
static void PrevectorClear(benchmark::State& state)
{

    while (state.KeepRunning()) {
        for (auto x = 0; x < 1000; ++x) {
            prevector<28, T> t0;
            prevector<28, T> t1;
            t0.resize(28);
            t0.clear();
            t1.resize(29);
            t0.clear();
        }
    }
}

template <typename T>
void PrevectorResize(benchmark::State& state)
{
    while (state.KeepRunning()) {
        prevector<28, T> t0;
        prevector<28, T> t1;
        for (auto x = 0; x < 1000; ++x) {
            t0.resize(28);
            t0.resize(0);
            t1.resize(29);
            t1.resize(0);
        }
    }
}

#define PREVECTOR_TEST(name)                                            \
    static void Prevector ## name ## Nontrivial(benchmark::State& state) { \
        PrevectorResize<nontrivial_t>(state);                           \
    }                                                                   \
    BENCHMARK(Prevector ## name ## Nontrivial);                         \
    static void Prevector ## name ## Trivial(benchmark::State& state) { \
        PrevectorResize<trivial_t>(state);                              \
    }                                                                   \
    BENCHMARK(Prevector ## name ## Trivial);

PREVECTOR_TEST(Clear)
PREVECTOR_TEST(Destructor)
PREVECTOR_TEST(Resize)
