#include "utilprocessmsg.h"
#include "utiltime.h"
#include <cmath>

// Exponentially limit the rate of nSize flow to nLimit.  nLimit unit is thousands-per-minute.
bool RateLimitExceeded(double& dCount, int64_t& nLastTime, int64_t nLimit, unsigned int nSize)
{
    int64_t nNow = GetTime();
    dCount *= std::pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
    nLastTime = nNow;
    if (dCount >= nLimit*10*1000)
        return true;
    dCount += nSize;
    return false;
}
