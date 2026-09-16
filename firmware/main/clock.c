#include "clock.h"
#include <sys/time.h>
#include <time.h>
#include "esp_timer.h"

static bool g_time_valid;

void clock_set_time(uint32_t epoch)
{
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    g_time_valid = true;
}

bool clock_time_valid(void) { return g_time_valid; }

uint32_t clock_uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

uint32_t clock_timestamp(void) { return g_time_valid ? (uint32_t)time(NULL) : clock_uptime_s(); }
