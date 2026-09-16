#include "wifi.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "clock.h"
#include "settings.h"

#define FAILED_AFTER_RETRIES 5
#define BACKOFF_MAX_S 60

static const char *TAG = "wifi";

static volatile wifi_state_t g_state = WIFI_OFF;
static char g_ip[16] = "";
static bool g_started;          // esp_wifi_start() has been called
static bool g_want_connect;     // credentials present; reconnect on disconnect
static int g_retries;
static esp_timer_handle_t g_retry_timer;
static bool g_sntp_started;
static bool g_reconnect_now;    // the next DISCONNECTED event is self-caused; reconnect immediately instead of backing off

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    clock_mark_valid(CLOCK_SOURCE_NTP);
    ESP_LOGI(TAG, "time synced via SNTP");
}

static void start_sntp_once(void)
{
    if (g_sntp_started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sntp_sync;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) g_sntp_started = true;
    else ESP_LOGE(TAG, "SNTP init failed");
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (g_want_connect) esp_wifi_connect();
}

static void schedule_retry(void)
{
    int delay_s = 1 << (g_retries < 6 ? g_retries : 6);   // 1,2,4,8,16,32,64
    if (delay_s > BACKOFF_MAX_S) delay_s = BACKOFF_MAX_S;
    g_retries++;
    g_state = g_retries >= FAILED_AFTER_RETRIES ? WIFI_FAILED : WIFI_CONNECTING;
    ESP_LOGW(TAG, "disconnected, retry %d in %d s", g_retries, delay_s);
    esp_timer_start_once(g_retry_timer, (uint64_t)delay_s * 1000000);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_START) {
        if (g_want_connect) { g_state = WIFI_CONNECTING; esp_wifi_connect(); }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        g_ip[0] = '\0';
        if (!g_want_connect) {
            g_state = WIFI_OFF;
        } else if (g_reconnect_now) {
            g_reconnect_now = false;
            g_retries = 0;
            g_state = WIFI_CONNECTING;
            esp_wifi_connect();
        } else {
            schedule_retry();
        }
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    snprintf(g_ip, sizeof g_ip, IPSTR, IP2STR(&ev->ip_info.ip));
    g_retries = 0;
    g_state = WIFI_CONNECTED;
    ESP_LOGI(TAG, "connected, ip=%s", g_ip);
    start_sntp_once();
}

static esp_err_t apply_config(const char *ssid, const char *password)
{
    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, ssid, sizeof cfg.sta.ssid);
    strncpy((char *)cfg.sta.password, password, sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

esp_err_t wifi_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL));
    esp_timer_create_args_t targs = { .callback = retry_timer_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &g_retry_timer));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    char ssid[33], password[64];
    if (!settings_wifi_credentials(ssid, sizeof ssid, password, sizeof password)) {
        ESP_LOGI(TAG, "no credentials stored");
        return ESP_OK;
    }
    err = apply_config(ssid, password);
    if (err != ESP_OK) return err;
    g_want_connect = true;
    g_state = WIFI_CONNECTING;
    err = esp_wifi_start();
    g_started = err == ESP_OK;
    if (err != ESP_OK) {
        g_state = WIFI_OFF;
        g_want_connect = false;
    }
    return err;
}

esp_err_t wifi_set_credentials(const char *ssid, const char *password)
{
    bool was_connected = g_state == WIFI_CONNECTED;
    esp_err_t err = settings_set_wifi_credentials(ssid, password);
    if (err != ESP_OK) return err;

    if (ssid[0] == '\0') {
        g_want_connect = false;
        g_reconnect_now = false;
        g_retries = 0;
        esp_timer_stop(g_retry_timer);
        if (g_started) { esp_wifi_disconnect(); esp_wifi_stop(); g_started = false; }
        g_ip[0] = '\0';
        g_state = WIFI_OFF;
        return ESP_OK;
    }

    esp_timer_stop(g_retry_timer);
    err = apply_config(ssid, password);
    if (err != ESP_OK) return err;
    g_retries = 0;
    g_want_connect = true;
    g_state = WIFI_CONNECTING;
    if (!g_started) {
        err = esp_wifi_start();             // STA_START event triggers esp_wifi_connect()
        g_started = err == ESP_OK;
        return err;
    }
    if (was_connected) {
        g_reconnect_now = true;
        return esp_wifi_disconnect();
    }
    return esp_wifi_connect();
}

wifi_state_t wifi_state(void) { return g_state; }

const char *wifi_state_name(void)
{
    switch (g_state) {
    case WIFI_CONNECTING: return "connecting";
    case WIFI_CONNECTED: return "connected";
    case WIFI_FAILED: return "failed";
    default: return "off";
    }
}

void wifi_ip(char *buf, size_t len)
{
    if (g_state == WIFI_CONNECTED) snprintf(buf, len, "%s", g_ip);
    else if (len) buf[0] = '\0';
}
