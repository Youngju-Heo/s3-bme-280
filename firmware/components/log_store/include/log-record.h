#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_RECORD_SIZE 16
#define LOG_RECORD_FLAG_TIME_VALID 0x01

typedef struct {
    uint32_t timestamp;   // epoch seconds if time_valid, else seconds since boot
    int16_t temp_centi;   // 0.01 degC
    uint16_t hum_centi;   // 0.01 %RH
    uint32_t pressure_pa;
    uint8_t flags;
    uint8_t boot_id;
} log_record_t;

uint16_t crc16_ccitt(const uint8_t *data, size_t len);
void log_record_encode(const log_record_t *rec, uint8_t out[LOG_RECORD_SIZE]);
bool log_record_decode(const uint8_t in[LOG_RECORD_SIZE], log_record_t *out);
bool log_record_is_empty(const uint8_t in[LOG_RECORD_SIZE]);
