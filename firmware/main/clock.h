#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { CLOCK_SOURCE_NONE, CLOCK_SOURCE_PC, CLOCK_SOURCE_NTP } clock_source_t;

void clock_set_time(uint32_t epoch);           // from the PC; ignored once NTP has synced
void clock_mark_valid(clock_source_t source);  // time already set (e.g. by SNTP); record validity and source
bool clock_time_valid(void);
const char *clock_time_source(void);           // "none" | "pc" | "ntp"
uint32_t clock_uptime_s(void);
uint32_t clock_timestamp(void);                // epoch if valid, else uptime
