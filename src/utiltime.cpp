// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "utiltime.h"

#include <atomic>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <chrono>
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

    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                      boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1)))
                      .total_milliseconds();
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

    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                      boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1)))
                      .total_microseconds();
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
    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                      boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1)))
                      .total_microseconds();
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

std::string DateTimeStrFormat(const char *pszFormat, int64_t nTime)
{
    // std::locale takes ownership of the pointer
    std::locale loc(std::locale::classic(), new boost::posix_time::time_facet(pszFormat));
    std::stringstream ss;
    ss.imbue(loc);
    ss << boost::posix_time::from_time_t(nTime);
    return ss.str();
}
