#ifndef MONOTONIC_CLOCK_H
#define MONOTONIC_CLOCK_H

#include <stdint.h>

int monotonic_clock_now_ns(uint64_t *time_ns);

#endif