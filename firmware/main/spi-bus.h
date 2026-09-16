#pragma once
#include "esp_err.h"
#include "bme280.h"

// SPI2 on SCK=GPIO3, MOSI=GPIO4, CS=GPIO5, MISO=GPIO6 (matches the BME280 module pin order SCL, SDA, CSB, SDO).
esp_err_t spi_bus_bme280_init(bme280_bus_t *out);
