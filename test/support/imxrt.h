#pragma once
#include <stdint.h>
extern volatile uint32_t test_aircr;
#define SCB_AIRCR test_aircr
