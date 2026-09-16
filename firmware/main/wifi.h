#pragma once
#include <stddef.h>
#include "esp_err.h"

typedef enum { WIFI_OFF, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED } wifi_state_t;

esp_err_t wifi_init(void);
esp_err_t wifi_set_credentials(const char *ssid, const char *password);
wifi_state_t wifi_state(void);
const char *wifi_state_name(void);
void wifi_ip(char *buf, size_t len);
