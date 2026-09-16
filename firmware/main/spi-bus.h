#pragma once
#include "esp_err.h"
#include "bme280.h"

// SPI2 on its IO_MUX pins: CS=GPIO10, MOSI=GPIO11, SCK=GPIO12, MISO=GPIO13 (BME280: CSB, SDA, SCL, SDO).
esp_err_t spi_bus_bme280_init(bme280_bus_t *out);
