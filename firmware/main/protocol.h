#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bme280.h"
#include "log-store.h"

#define PROTOCOL_FIRMWARE_VERSION "0.1.0"
#define PROTOCOL_MAX_LINE 256
#define PROTOCOL_MAX_LIMIT 500
#define PROTOCOL_DEFAULT_LIMIT 100

typedef void (*protocol_write_fn)(void *ctx, const char *data, size_t len);

typedef struct {
    void *ctx;
    log_store_t *store;
    uint8_t (*boot_id)(void *ctx);
    uint32_t (*uptime_s)(void *ctx);
    bool (*time_valid)(void *ctx);
    void (*set_time)(void *ctx, uint32_t epoch);
    int (*read_now)(void *ctx, bme280_reading_t *out);
    bool (*sensor_ok)(void *ctx);
    bool (*store_ok)(void *ctx);
    uint32_t (*interval_s)(void *ctx);
    int (*set_interval_s)(void *ctx, uint32_t s);
} protocol_ops_t;

void protocol_init(const protocol_ops_t *ops);
void protocol_handle_line(const char *line, protocol_write_fn write, void *wctx);
