#include "settings.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS "bme"
#define KEY_INTERVAL "interval_s"
#define KEY_BOOT_ID "boot_id"

static nvs_handle_t g_nvs;
static uint32_t g_interval_s = SETTINGS_INTERVAL_DEFAULT;
static uint8_t g_boot_id;

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    err = nvs_open(NS, NVS_READWRITE, &g_nvs);
    if (err != ESP_OK) return err;

    if (nvs_get_u32(g_nvs, KEY_INTERVAL, &g_interval_s) != ESP_OK) g_interval_s = SETTINGS_INTERVAL_DEFAULT;
    if (nvs_get_u8(g_nvs, KEY_BOOT_ID, &g_boot_id) != ESP_OK) g_boot_id = 0;
    g_boot_id++;
    nvs_set_u8(g_nvs, KEY_BOOT_ID, g_boot_id);
    return nvs_commit(g_nvs);
}

uint32_t settings_interval_s(void) { return g_interval_s; }

esp_err_t settings_set_interval_s(uint32_t s)
{
    if (s < SETTINGS_INTERVAL_MIN || s > SETTINGS_INTERVAL_MAX) return ESP_ERR_INVALID_ARG;
    g_interval_s = s;
    esp_err_t err = nvs_set_u32(g_nvs, KEY_INTERVAL, s);
    if (err != ESP_OK) return err;
    return nvs_commit(g_nvs);
}

uint8_t settings_boot_id(void) { return g_boot_id; }
