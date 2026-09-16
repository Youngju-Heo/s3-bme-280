#pragma once
#include <stdbool.h>
#include <string.h>
#include "log-store.h"

// NOR flash emulation: write can only clear bits, erase sets a sector to 0xFF.
static uint8_t fake_flash_mem[LOG_STORE_SECTORS * LOG_STORE_SECTOR_SIZE];
static int fake_flash_erase_count[LOG_STORE_SECTORS];
static bool fake_flash_fail_write;

static inline int fake_flash_read(void *ctx, uint32_t offset, void *buf, size_t len)
{
    (void)ctx;
    memcpy(buf, fake_flash_mem + offset, len);
    return 0;
}

static inline int fake_flash_write(void *ctx, uint32_t offset, const void *buf, size_t len)
{
    (void)ctx;
    if (fake_flash_fail_write) return -1;
    for (size_t i = 0; i < len; i++) fake_flash_mem[offset + i] &= ((const uint8_t *)buf)[i];
    return 0;
}

static inline int fake_flash_erase_sector(void *ctx, uint32_t sector)
{
    (void)ctx;
    memset(fake_flash_mem + sector * LOG_STORE_SECTOR_SIZE, 0xFF, LOG_STORE_SECTOR_SIZE);
    fake_flash_erase_count[sector]++;
    return 0;
}

static inline void fake_flash_reset(void)
{
    memset(fake_flash_mem, 0xFF, sizeof fake_flash_mem);
    memset(fake_flash_erase_count, 0, sizeof fake_flash_erase_count);
    fake_flash_fail_write = false;
}

static const log_store_flash_t fake_flash = {
    .ctx = NULL, .read = fake_flash_read, .write = fake_flash_write, .erase_sector = fake_flash_erase_sector,
};
