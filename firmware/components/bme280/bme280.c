#include "bme280.h"

#define RESET_CMD 0xB6
#define CTRL_HUM_OSRS_X1 0x01
#define CTRL_MEAS_SLEEP 0x24    // osrs_t=1, osrs_p=1, mode=sleep
#define CTRL_MEAS_FORCED 0x25   // osrs_t=1, osrs_p=1, mode=forced
#define STATUS_MEASURING 0x08
#define MEASURE_POLL_MS 10
#define MEASURE_POLL_MAX 10

int bme280_init(bme280_t *dev, const bme280_bus_t *bus)
{
    dev->bus = *bus;
    if (dev->bus.write_reg(dev->bus.ctx, BME280_REG_RESET, RESET_CMD)) return BME280_ERR_BUS;
    dev->bus.delay_ms(dev->bus.ctx, 5);

    uint8_t id = 0;
    if (dev->bus.read_regs(dev->bus.ctx, BME280_REG_CHIP_ID, &id, 1)) return BME280_ERR_BUS;
    if (id != BME280_CHIP_ID) return BME280_ERR_CHIP_ID;

    uint8_t calib1[26], calib2[7];
    if (dev->bus.read_regs(dev->bus.ctx, BME280_REG_CALIB1, calib1, sizeof calib1)) return BME280_ERR_BUS;
    if (dev->bus.read_regs(dev->bus.ctx, BME280_REG_CALIB2, calib2, sizeof calib2)) return BME280_ERR_BUS;
    bme280_parse_calib(calib1, calib2, &dev->calib);

    if (dev->bus.write_reg(dev->bus.ctx, BME280_REG_CTRL_HUM, CTRL_HUM_OSRS_X1)) return BME280_ERR_BUS;
    if (dev->bus.write_reg(dev->bus.ctx, BME280_REG_CONFIG, 0x00)) return BME280_ERR_BUS;
    if (dev->bus.write_reg(dev->bus.ctx, BME280_REG_CTRL_MEAS, CTRL_MEAS_SLEEP)) return BME280_ERR_BUS;
    return BME280_OK;
}

int bme280_read(bme280_t *dev, bme280_reading_t *out)
{
    if (dev->bus.write_reg(dev->bus.ctx, BME280_REG_CTRL_MEAS, CTRL_MEAS_FORCED)) return BME280_ERR_BUS;
    dev->bus.delay_ms(dev->bus.ctx, MEASURE_POLL_MS);   // wait for the measurement to complete before the first status poll

    int polls = 0;
    for (;;) {
        uint8_t status = 0;
        if (dev->bus.read_regs(dev->bus.ctx, BME280_REG_STATUS, &status, 1)) return BME280_ERR_BUS;
        if (!(status & STATUS_MEASURING)) break;
        if (++polls > MEASURE_POLL_MAX) return BME280_ERR_TIMEOUT;
        dev->bus.delay_ms(dev->bus.ctx, MEASURE_POLL_MS);
    }

    uint8_t d[8];
    if (dev->bus.read_regs(dev->bus.ctx, BME280_REG_DATA, d, sizeof d)) return BME280_ERR_BUS;
    int32_t adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t adc_h = ((int32_t)d[6] << 8) | d[7];
    bme280_compensate(&dev->calib, adc_t, adc_p, adc_h, out);
    return BME280_OK;
}
