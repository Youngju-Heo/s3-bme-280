#pragma once
#include <stdint.h>

typedef enum {
    STATUS_LED_OFF,
    STATUS_LED_OK,          // sensor and store healthy, time synced: steady green
    STATUS_LED_OK_NO_TIME,  // healthy but time not yet synced: blinking green
    STATUS_LED_ERROR,       // sensor or store failure: steady red
} status_led_state_t;

typedef struct { uint8_t r, g, b; } status_led_rgb_t;

#define STATUS_LED_BRIGHTNESS 4      // out of 255; dim on purpose
#define STATUS_LED_BLINK_HALF_MS 500

status_led_rgb_t status_led_color(status_led_state_t state, uint32_t now_ms);
