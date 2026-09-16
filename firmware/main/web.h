#pragma once
#include "esp_err.h"

// Serializes device access with the main loop; app_main supplies mutex lock/unlock.
typedef struct {
    void *ctx;
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} web_lock_t;

esp_err_t web_start(const web_lock_t *lock);
