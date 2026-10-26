#pragma once
#include <stdbool.h>
#include <stdint.h>

// Deadlines must be scheduled less than 2^31 milliseconds into the future.
static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}
