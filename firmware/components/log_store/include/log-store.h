#pragma once
#include "log-record.h"

#define LOG_STORE_SECTOR_SIZE 4096
#define LOG_STORE_SECTORS 128
#define LOG_STORE_SLOTS_PER_SECTOR (LOG_STORE_SECTOR_SIZE / LOG_RECORD_SIZE)
#define LOG_STORE_TOTAL_SLOTS (LOG_STORE_SECTORS * LOG_STORE_SLOTS_PER_SECTOR)
#define LOG_STORE_CAPACITY ((LOG_STORE_SECTORS - 1) * LOG_STORE_SLOTS_PER_SECTOR)
#define LOG_STORE_ERR_CORRUPT (-2)

typedef struct {
    void *ctx;
    int (*read)(void *ctx, uint32_t offset, void *buf, size_t len);
    int (*write)(void *ctx, uint32_t offset, const void *buf, size_t len);
    int (*erase_sector)(void *ctx, uint32_t sector);
} log_store_flash_t;

typedef struct {
    log_store_flash_t flash;
    uint32_t head;   // next slot to write
    uint32_t tail;   // oldest valid slot
} log_store_t;

int log_store_init(log_store_t *s, const log_store_flash_t *flash);
int log_store_append(log_store_t *s, const log_record_t *rec);
uint32_t log_store_count(const log_store_t *s);
int log_store_read(log_store_t *s, uint32_t offset, log_record_t *out, uint32_t max, uint32_t *n_read);
int log_store_clear(log_store_t *s);
