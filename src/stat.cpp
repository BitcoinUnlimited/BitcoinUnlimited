#include <cmath>
#include "utiltime.h"
#include "stat.h"


ExponentialMovingAverage::ExponentialMovingAverage(const double _interval,
    const size_t _time_units) :
    interval(_interval), time_units(_time_units) {}

int64_t ExponentialMovingAverage::time() {
    return GetTimeMicros();
}

void ExponentialMovingAverage::update(const size_t counts) {
    std::lock_guard<std::mutex> lock(cs);

    int64_t now = time();
    if (last_update == 0) last_update = now;

    int64_t delta = now - last_update;
    if (delta > 0) // guard against clock resets
        rate *= exp(- (delta / (time_units * interval)));
    last_update = now;
    rate += counts / interval;
}

double ExponentialMovingAverage::value() {
    update(0); // decay rate to current value

    std::lock_guard<std::mutex> lock(cs);
    return rate;
}
