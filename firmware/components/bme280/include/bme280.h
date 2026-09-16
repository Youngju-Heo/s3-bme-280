#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BME280_OK 0
#define BME280_ERR_BUS (-1)
#define BME280_ERR_CHIP_ID (-2)
#define BME280_ERR_TIMEOUT (-3)

#define BME280_CHIP_ID 0x60
#define BME280_REG_CALIB1 0x88   // 26 bytes
#define BME280_REG_CHIP_ID 0xD0
#define BME280_REG_RESET 0xE0
#define BME280_REG_CALIB2 0xE1   // 7 bytes
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS 0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG 0xF5
#define BME280_REG_DATA 0xF7     // 8 bytes
#define BME280_MAX_TRANSFER 26

typedef struct {
    uint16_t dig_t1; int16_t dig_t2, dig_t3;
    uint16_t dig_p1; int16_t dig_p2, dig_p3, dig_p4, dig_p5, dig_p6, dig_p7, dig_p8, dig_p9;
    uint8_t dig_h1; int16_t dig_h2; uint8_t dig_h3; int16_t dig_h4, dig_h5; int8_t dig_h6;
} bme280_calib_t;

typedef struct {
    int32_t temp_centi;    // 0.01 degC
    uint32_t pressure_pa;
    uint32_t hum_centi;    // 0.01 %RH
} bme280_reading_t;

typedef struct {
    void *ctx;
    int (*read_regs)(void *ctx, uint8_t reg, uint8_t *buf, size_t len);   // 0 on success
    int (*write_reg)(void *ctx, uint8_t reg, uint8_t val);                // 0 on success
    void (*delay_ms)(void *ctx, uint32_t ms);
} bme280_bus_t;

typedef struct {
    bme280_bus_t bus;
    bme280_calib_t calib;
} bme280_t;

void bme280_parse_calib(const uint8_t calib1[26], const uint8_t calib2[7], bme280_calib_t *out);
void bme280_compensate(const bme280_calib_t *c, int32_t adc_t, int32_t adc_p, int32_t adc_h, bme280_reading_t *out);

int bme280_init(bme280_t *dev, const bme280_bus_t *bus);
int bme280_read(bme280_t *dev, bme280_reading_t *out);
