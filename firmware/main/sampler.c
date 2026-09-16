#include "sampler.h"

static bme280_t *g_sensor;
static log_store_t *g_store;
static uint32_t g_interval_s;
static bool g_sensor_ok;
static bool g_has_sampled;
static uint32_t g_last_sample_s;

void sampler_init(bme280_t *sensor, log_store_t *store, uint32_t interval_s)
{
    g_sensor = sensor;
    g_store = store;
    g_interval_s = interval_s;
    g_sensor_ok = sensor != NULL;
    g_has_sampled = false;
    g_last_sample_s = 0;
}

void sampler_set_interval(uint32_t interval_s) { g_interval_s = interval_s; }
uint32_t sampler_interval(void) { return g_interval_s; }
bool sampler_sensor_ok(void) { return g_sensor_ok; }

static int16_t clamp_i16(int32_t v) { return v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : (int16_t)v; }
static uint16_t clamp_u16(uint32_t v) { return v > UINT16_MAX ? UINT16_MAX : (uint16_t)v; }

void sampler_tick(uint32_t uptime_s, uint32_t timestamp, bool time_valid, uint8_t boot_id)
{
    if (g_has_sampled && uptime_s - g_last_sample_s < g_interval_s) return;
    g_has_sampled = true;
    g_last_sample_s = uptime_s;

    if (g_sensor == NULL) { g_sensor_ok = false; return; }

    bme280_reading_t r;
    if (bme280_read(g_sensor, &r) != BME280_OK) { g_sensor_ok = false; return; }
    g_sensor_ok = true;

    log_record_t rec = {
        .timestamp = timestamp,
        .temp_centi = clamp_i16(r.temp_centi),
        .hum_centi = clamp_u16(r.hum_centi),
        .pressure_pa = r.pressure_pa,
        .flags = time_valid ? LOG_RECORD_FLAG_TIME_VALID : 0,
        .boot_id = boot_id,
    };
    log_store_append(g_store, &rec);
}
