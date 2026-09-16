#include <string.h>
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bme280.h"
#include "clock.h"
#include "log-store.h"
#include "protocol.h"
#include "sampler.h"
#include "settings.h"
#include "spi-bus.h"

static const char *TAG = "app";

static bme280_t g_sensor;
static bme280_t *g_sensor_ptr;
static log_store_t g_store;

// --- log_store flash backend on the "bmelog" partition ---
static int flash_read(void *ctx, uint32_t offset, void *buf, size_t len)
{
    return esp_partition_read((const esp_partition_t *)ctx, offset, buf, len) == ESP_OK ? 0 : -1;
}
static int flash_write(void *ctx, uint32_t offset, const void *buf, size_t len)
{
    return esp_partition_write((const esp_partition_t *)ctx, offset, buf, len) == ESP_OK ? 0 : -1;
}
static int flash_erase_sector(void *ctx, uint32_t sector)
{
    return esp_partition_erase_range((const esp_partition_t *)ctx, sector * LOG_STORE_SECTOR_SIZE, LOG_STORE_SECTOR_SIZE) == ESP_OK ? 0 : -1;
}

// --- protocol ops ---
static uint8_t op_boot_id(void *c) { (void)c; return settings_boot_id(); }
static uint32_t op_uptime_s(void *c) { (void)c; return clock_uptime_s(); }
static bool op_time_valid(void *c) { (void)c; return clock_time_valid(); }
static void op_set_time(void *c, uint32_t epoch) { (void)c; clock_set_time(epoch); }
static int op_read_now(void *c, bme280_reading_t *out)
{
    (void)c;
    if (g_sensor_ptr == NULL) return -1;
    return bme280_read(g_sensor_ptr, out) == BME280_OK ? 0 : -1;
}
static bool op_sensor_ok(void *c) { (void)c; return sampler_sensor_ok(); }
static uint32_t op_interval_s(void *c) { (void)c; return settings_interval_s(); }
static int op_set_interval_s(void *c, uint32_t s)
{
    (void)c;
    if (settings_set_interval_s(s) != ESP_OK) return -1;
    sampler_set_interval(s);
    return 0;
}

static const protocol_ops_t g_ops = {
    .ctx = NULL, .store = &g_store, .boot_id = op_boot_id, .uptime_s = op_uptime_s, .time_valid = op_time_valid,
    .set_time = op_set_time, .read_now = op_read_now, .sensor_ok = op_sensor_ok,
    .interval_s = op_interval_s, .set_interval_s = op_set_interval_s,
};

static void usb_write(void *ctx, const char *data, size_t len)
{
    (void)ctx;
    while (len > 0) {
        int n = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(1000));
        if (n <= 0) return;
        data += n;
        len -= (size_t)n;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(settings_init());
    ESP_LOGI(TAG, "boot_id=%u interval_s=%lu", settings_boot_id(), (unsigned long)settings_interval_s());

    bme280_bus_t bus;
    esp_err_t err = spi_bus_bme280_init(&bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
    } else {
        int rc = bme280_init(&g_sensor, &bus);
        if (rc == BME280_OK) g_sensor_ptr = &g_sensor;
        else ESP_LOGE(TAG, "BME280 init failed: %d", rc);
    }

    const esp_partition_t *part = esp_partition_find_first((esp_partition_type_t)0x40, (esp_partition_subtype_t)0x00, "bmelog");
    if (part == NULL) {
        ESP_LOGE(TAG, "bmelog partition not found");
        return;
    }
    log_store_flash_t flash = { .ctx = (void *)part, .read = flash_read, .write = flash_write, .erase_sector = flash_erase_sector };
    int rc = log_store_init(&g_store, &flash);
    if (rc == LOG_STORE_ERR_CORRUPT) {
        ESP_LOGW(TAG, "log store has no erased sector, clearing");
        rc = log_store_clear(&g_store);
    }
    if (rc != 0) ESP_LOGE(TAG, "log store init failed: %d", rc);
    ESP_LOGI(TAG, "log store count=%lu", (unsigned long)log_store_count(&g_store));

    sampler_init(g_sensor_ptr, &g_store, settings_interval_s());
    protocol_init(&g_ops);

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 4096;
    cfg.rx_buffer_size = 512;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));

    static char line[PROTOCOL_MAX_LINE];
    size_t len = 0;
    bool overflow = false;
    for (;;) {
        uint8_t buf[64];
        int n = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (len > 0 && !overflow) {
                    line[len] = '\0';
                    protocol_handle_line(line, usb_write, NULL);
                }
                len = 0;
                overflow = false;
            } else if (len < sizeof line - 1) {
                line[len++] = c;
            } else {
                overflow = true;
            }
        }
        sampler_tick(clock_uptime_s(), clock_timestamp(), clock_time_valid(), settings_boot_id());
    }
}
