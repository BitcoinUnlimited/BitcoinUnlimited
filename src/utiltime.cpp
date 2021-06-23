// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "tinyformat.h"
#include "utiltime.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <ctime>
#include <thread>

#ifdef WIN32
#include <windows.h> // for Sleep()
#endif

static std::atomic<int64_t> nMockTime(0);

int64_t GetTime()
{
    int64_t mocktime = nMockTime.load(std::memory_order_relaxed);
    if (mocktime)
    {
        return mocktime;
    }

    time_t now = time(nullptr);
    assert(now > 0);
    return now;
}

void SetMockTime(int64_t nMockTimeIn) { nMockTime.store(nMockTimeIn, std::memory_order_relaxed); }
int64_t GetTimeMillis()
{
    int64_t mocktime = nMockTime.load(std::memory_order_relaxed);
    if (mocktime)
        return mocktime * 1000;

    std::chrono::time_point<std::chrono::system_clock> clock_now = std::chrono::system_clock::now();
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(clock_now.time_since_epoch()).count();
    assert(now > 0);
    return now;
}

int64_t GetTimeMicros()
{
    int64_t mocktime = nMockTime.load(std::memory_order_relaxed);
    if (mocktime)
    {
        return mocktime * 1000000;
    }
    std::chrono::time_point<std::chrono::system_clock> clock_now = std::chrono::system_clock::now();
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(clock_now.time_since_epoch()).count();
    assert(now > 0);
    return now;
}


#ifdef WIN32
uint64_t GetStopwatch() { return 1000 * GetLogTimeMicros(); }
#elif MAC_OSX
uint64_t GetStopwatch() { return 1000 * GetLogTimeMicros(); }
#else
uint64_t GetStopwatch()
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) == 0)
    {
        uint64_t ret = t.tv_sec;
        ret *= 1000ULL * 1000ULL * 1000ULL; // convert sec to nsec
        ret += t.tv_nsec;
        return ret;
    }
    return 0;
}
#endif

/** Return a time useful for the debug log */
int64_t GetLogTimeMicros()
{
    std::chrono::time_point<std::chrono::system_clock> clock_now = std::chrono::system_clock::now();
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(clock_now.time_since_epoch()).count();
    assert(now > 0);
    return now;
}

void MilliSleep(int64_t n)
{
#ifdef WIN32
    Sleep(n);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
#endif
}

std::string FormatISO8601DateTime(int64_t nTime)
{
    struct tm ts;
    time_t time_val = nTime;
#ifdef HAVE_GMTIME_R
    if (gmtime_r(&time_val, &ts) == nullptr)
    {
#else
    if (gmtime_s(&ts, &time_val) != 0)
    {
#endif
        return {};
    }
    return strprintf("%04i-%02i-%02i %02i:%02i:%02i", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday, ts.tm_hour,
        ts.tm_min, ts.tm_sec);
}

std::string FormatISO8601Date(int64_t nTime)
{
    struct tm ts;
    time_t time_val = nTime;
#ifdef HAVE_GMTIME_R
    if (gmtime_r(&time_val, &ts) == nullptr)
    {
#else
    if (gmtime_s(&ts, &time_val) != 0)
    {
#endif
        return {};
    }
    return strprintf("%04i-%02i-%02i", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday);
}
