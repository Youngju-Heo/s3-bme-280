#include "clock.h"
#include <sys/time.h>
#include <time.h>
#include "esp_timer.h"

static clock_source_t g_source = CLOCK_SOURCE_NONE;

void clock_set_time(uint32_t epoch)
{
    if (g_source == CLOCK_SOURCE_NTP) return;   // NTP is authoritative once it has synced
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    g_source = CLOCK_SOURCE_PC;
}

void clock_mark_valid(clock_source_t source) { g_source = source; }

bool clock_time_valid(void) { return g_source != CLOCK_SOURCE_NONE; }

const char *clock_time_source(void)
{
    switch (g_source) {
    case CLOCK_SOURCE_PC: return "pc";
    case CLOCK_SOURCE_NTP: return "ntp";
    default: return "none";
    }
}

uint32_t clock_uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

uint32_t clock_timestamp(void) { return clock_time_valid() ? (uint32_t)time(NULL) : clock_uptime_s(); }
