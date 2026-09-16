#include "status-led.h"
#include <string.h>
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#define LED_GPIO 21
#define RMT_RESOLUTION_HZ 10000000   // 10 MHz -> 0.1 us per tick
// WS2812 timing: bit0 = 0.3 us high / 0.9 us low, bit1 = 0.9 us high / 0.3 us low
#define T0H_TICKS 3
#define T0L_TICKS 9
#define T1H_TICKS 9
#define T1L_TICKS 3

static rmt_channel_handle_t g_chan;
static rmt_encoder_handle_t g_encoder;
static status_led_rgb_t g_last = { 0xFF, 0xFF, 0xFF };   // impossible value forces the first send

esp_err_t status_led_init(void)
{
    rmt_tx_channel_config_t chan = {
        .gpio_num = LED_GPIO, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64, .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&chan, &g_chan);
    if (err != ESP_OK) return err;

    rmt_bytes_encoder_config_t enc = {
        .bit0 = { .level0 = 1, .duration0 = T0H_TICKS, .level1 = 0, .duration1 = T0L_TICKS },
        .bit1 = { .level0 = 1, .duration0 = T1H_TICKS, .level1 = 0, .duration1 = T1L_TICKS },
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&enc, &g_encoder);
    if (err != ESP_OK) return err;
    return rmt_enable(g_chan);
}

void status_led_update(status_led_state_t state, uint32_t now_ms)
{
    if (g_chan == NULL) return;
    status_led_rgb_t c = status_led_color(state, now_ms);
    if (memcmp(&c, &g_last, sizeof c) == 0) return;
    g_last = c;

    uint8_t grb[3] = { c.g, c.r, c.b };   // WS2812 expects GRB order
    rmt_transmit_config_t tx = { .loop_count = 0 };
    if (rmt_transmit(g_chan, g_encoder, grb, sizeof grb, &tx) == ESP_OK) {
        rmt_tx_wait_all_done(g_chan, 100);   // line idles low afterwards, which doubles as the WS2812 reset gap
    }
}
