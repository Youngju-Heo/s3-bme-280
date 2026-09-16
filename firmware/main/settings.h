#pragma once
#include <stdint.h>
#include "esp_err.h"

#define SETTINGS_INTERVAL_MIN 10
#define SETTINGS_INTERVAL_MAX 3600
#define SETTINGS_INTERVAL_DEFAULT 60

esp_err_t settings_init(void);            // loads interval, increments and persists boot_id
uint32_t settings_interval_s(void);
esp_err_t settings_set_interval_s(uint32_t s);   // ESP_ERR_INVALID_ARG if out of range
uint8_t settings_boot_id(void);
