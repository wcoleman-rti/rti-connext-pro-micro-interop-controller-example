#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L

#include <time.h>

#include "monotonic_clock.h"

int
monotonic_clock_now_ns(uint64_t *time_ns)
{
    struct timespec current_time;

    if (time_ns == NULL || clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
        return -1;
    }

    *time_ns = (uint64_t)current_time.tv_sec * UINT64_C(1000000000) +
               (uint64_t)current_time.tv_nsec;
    return 0;
}
#elif defined(__tricore__) || defined(__TRICORE__)
#error "Add the TC299 monotonic counter implementation for this target"
#else
#error "No monotonic clock implementation for this platform"
#endif