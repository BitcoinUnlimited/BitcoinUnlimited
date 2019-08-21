// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTILTIME_H
#define BITCOIN_UTILTIME_H

#include <stdint.h>
#include <string>

/** Returns the calendar time in seconds since the epoch, or @nMockTime if mock time is enabled during testing */
int64_t GetTime();
/** Returns the calendar time in milliseconds since the epoch, or @nMockTime*1000 if mock time is enabled during testing
 */
int64_t GetTimeMillis();
/** Returns the calendar time in microseconds since the epoch, or @nMockTime*10^6 if mock time is enabled during testing
 */
int64_t GetTimeMicros();

/** Returns the calendar time in microseconds since the epoch.  Not affected by mock time during testing. */
int64_t GetLogTimeMicros();

/** Sets a fake time value, which is very useful for testing */
void SetMockTime(int64_t nMockTimeIn);

/** Sleep for n milliseconds */
void MilliSleep(int64_t n);

/** Returns a monotonically increasing time for interval measurement (in nSec).  This number is unrelated to calendar
time and is not affected by mock time during test */
uint64_t GetStopwatch();
/** Returns a monotonically increasing time for interval measurement (in uSec).  This number is unrelated to calendar
time and is not affected by mock time during test */
inline uint64_t GetStopwatchMicros() { return GetStopwatch() / 1000; }
/** Convert seconds since the epoch to a string */
std::string DateTimeStrFormat(const char *pszFormat, int64_t nTime);

#endif // BITCOIN_UTILTIME_H
