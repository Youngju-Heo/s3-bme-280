#include "spi-bus.h"
#include <string.h>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPI_PIN_SCK 3
#define SPI_PIN_MOSI 4
#define SPI_PIN_CS 5
#define SPI_PIN_MISO 6
#define SPI_CLOCK_HZ 1000000

static spi_device_handle_t g_dev;

static int spi_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t len)
{
    (void)ctx;
    if (len > BME280_MAX_TRANSFER) return -1;
    uint8_t tx[BME280_MAX_TRANSFER + 1] = { (uint8_t)(reg | 0x80) };
    uint8_t rx[BME280_MAX_TRANSFER + 1] = { 0 };
    spi_transaction_t t = { .length = 8 * (len + 1), .tx_buffer = tx, .rx_buffer = rx };
    if (spi_device_polling_transmit(g_dev, &t) != ESP_OK) return -1;
    memcpy(buf, rx + 1, len);
    return 0;
}

static int spi_write_reg(void *ctx, uint8_t reg, uint8_t val)
{
    (void)ctx;
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), val };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    return spi_device_polling_transmit(g_dev, &t) == ESP_OK ? 0 : -1;
}

static void spi_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

esp_err_t spi_bus_bme280_init(bme280_bus_t *out)
{
    spi_bus_config_t bus = {
        .mosi_io_num = SPI_PIN_MOSI, .miso_io_num = SPI_PIN_MISO, .sclk_io_num = SPI_PIN_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 64,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = SPI_CLOCK_HZ, .mode = 0, .spics_io_num = SPI_PIN_CS, .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &g_dev);
    if (err != ESP_OK) return err;

    *out = (bme280_bus_t){ .ctx = NULL, .read_regs = spi_read_regs, .write_reg = spi_write_reg, .delay_ms = spi_delay_ms };
    return ESP_OK;
}
