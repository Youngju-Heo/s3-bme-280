#pragma once
#include <string.h>
#include "bme280.h"

// Register-map emulation of a BME280 on the bus abstraction.
static uint8_t fake_bus_regs[256];
static uint8_t fake_bus_write_log[64][2];   // {reg, val}
static int fake_bus_write_count;
static bool fake_bus_fail;

static inline int fake_bus_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t len)
{
    (void)ctx;
    if (fake_bus_fail) return -1;
    memcpy(buf, fake_bus_regs + reg, len);
    return 0;
}

static inline int fake_bus_write_reg(void *ctx, uint8_t reg, uint8_t val)
{
    (void)ctx;
    if (fake_bus_fail) return -1;
    if (fake_bus_write_count < 64) {
        fake_bus_write_log[fake_bus_write_count][0] = reg;
        fake_bus_write_log[fake_bus_write_count][1] = val;
    }
    fake_bus_write_count++;
    if (reg != BME280_REG_RESET) fake_bus_regs[reg] = val;
    return 0;
}

static inline void fake_bus_delay_ms(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }

static inline void fake_bus_reset(void)
{
    memset(fake_bus_regs, 0, sizeof fake_bus_regs);
    fake_bus_write_count = 0;
    fake_bus_fail = false;
    fake_bus_regs[BME280_REG_CHIP_ID] = BME280_CHIP_ID;
    fake_bus_regs[BME280_REG_STATUS] = 0x00;   // not measuring
}

static inline uint8_t fake_bus_last_write(uint8_t reg)
{
    uint8_t v = 0;
    for (int i = 0; i < fake_bus_write_count && i < 64; i++) {
        if (fake_bus_write_log[i][0] == reg) v = fake_bus_write_log[i][1];
    }
    return v;
}

static const bme280_bus_t fake_bus = {
    .ctx = NULL, .read_regs = fake_bus_read_regs, .write_reg = fake_bus_write_reg, .delay_ms = fake_bus_delay_ms,
};
