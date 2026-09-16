#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bme280.h"
#include "log-store.h"

void sampler_init(bme280_t *sensor, log_store_t *store, uint32_t interval_s);
void sampler_set_interval(uint32_t interval_s);
uint32_t sampler_interval(void);
bool sampler_sensor_ok(void);
void sampler_tick(uint32_t uptime_s, uint32_t timestamp, bool time_valid, uint8_t boot_id);
