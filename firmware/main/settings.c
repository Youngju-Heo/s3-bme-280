#include "settings.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS "bme"
#define KEY_INTERVAL "interval_s"
#define KEY_BOOT_ID "boot_id"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"

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

bool settings_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    size_t n = ssid_len;
    if (nvs_get_str(g_nvs, KEY_WIFI_SSID, ssid, &n) != ESP_OK || ssid[0] == '\0') return false;
    n = pass_len;
    if (nvs_get_str(g_nvs, KEY_WIFI_PASS, password, &n) != ESP_OK) password[0] = '\0';
    return true;
}

esp_err_t settings_set_wifi_credentials(const char *ssid, const char *password)
{
    esp_err_t err;
    if (ssid[0] == '\0') {
        err = nvs_erase_key(g_nvs, KEY_WIFI_SSID);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
        err = nvs_erase_key(g_nvs, KEY_WIFI_PASS);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    } else {
        err = nvs_set_str(g_nvs, KEY_WIFI_SSID, ssid);
        if (err != ESP_OK) return err;
        err = nvs_set_str(g_nvs, KEY_WIFI_PASS, password);
        if (err != ESP_OK) return err;
    }
    return nvs_commit(g_nvs);
}
