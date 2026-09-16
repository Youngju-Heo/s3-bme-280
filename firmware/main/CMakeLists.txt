idf_component_register(
    SRCS "app-main.c" "spi-bus.c" "clock.c" "settings.c" "sampler.c" "protocol.c" "json-mini.c" "status-led.c" "status-led-color.c" "wifi.c" "web-bridge.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES log_store bme280 esp_driver_spi esp_driver_usb_serial_jtag nvs_flash esp_partition esp_timer esp_driver_rmt esp_wifi esp_netif esp_event)
