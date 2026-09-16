#include "status-led-color.h"

status_led_rgb_t status_led_color(status_led_state_t state, uint32_t now_ms)
{
    status_led_rgb_t off = { 0, 0, 0 };
    status_led_rgb_t green = { 0, STATUS_LED_BRIGHTNESS, 0 };
    status_led_rgb_t red = { STATUS_LED_BRIGHTNESS, 0, 0 };
    switch (state) {
    case STATUS_LED_OK: return green;
    case STATUS_LED_OK_NO_TIME: return (now_ms / STATUS_LED_BLINK_HALF_MS) % 2 == 0 ? green : off;
    case STATUS_LED_ERROR: return red;
    case STATUS_LED_WIFI_CONNECTING: {
        status_led_rgb_t blue = { 0, 0, STATUS_LED_BRIGHTNESS };
        return (now_ms / STATUS_LED_BLINK_HALF_MS) % 2 == 0 ? blue : off;
    }
    default: return off;
    }
}
