#pragma once
#include <stdbool.h>
#include <stdint.h>

void clock_set_time(uint32_t epoch);
bool clock_time_valid(void);
uint32_t clock_uptime_s(void);
uint32_t clock_timestamp(void);   // epoch if valid, else uptime
