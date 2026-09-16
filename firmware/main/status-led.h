#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "status-led-color.h"

// On-board WS2812 of the ESP32-S3-Zero (GPIO21), driven through the RMT peripheral.
esp_err_t status_led_init(void);
void status_led_update(status_led_state_t state, uint32_t now_ms);   // re-sends only when the color changes
