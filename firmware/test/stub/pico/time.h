/* host-test stand-in: a simulated clock the test advances */
#pragma once
#include <stdint.h>
typedef uint64_t absolute_time_t;
extern uint64_t sim_us;
static inline absolute_time_t get_absolute_time(void) { return sim_us; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }
