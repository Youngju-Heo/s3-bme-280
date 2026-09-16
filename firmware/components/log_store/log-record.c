#include "log-record.h"

uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void log_record_encode(const log_record_t *rec, uint8_t out[LOG_RECORD_SIZE])
{
    put_u32(out, rec->timestamp);
    put_u16(out + 4, (uint16_t)rec->temp_centi);
    put_u16(out + 6, rec->hum_centi);
    put_u32(out + 8, rec->pressure_pa);
    out[12] = rec->flags;
    out[13] = rec->boot_id;
    put_u16(out + 14, crc16_ccitt(out, 14));
}

bool log_record_is_empty(const uint8_t in[LOG_RECORD_SIZE])
{
    for (int i = 0; i < LOG_RECORD_SIZE; i++) {
        if (in[i] != 0xFF) return false;
    }
    return true;
}

bool log_record_decode(const uint8_t in[LOG_RECORD_SIZE], log_record_t *out)
{
    if (log_record_is_empty(in)) return false;
    if (get_u16(in + 14) != crc16_ccitt(in, 14)) return false;
    out->timestamp = get_u32(in);
    out->temp_centi = (int16_t)get_u16(in + 4);
    out->hum_centi = get_u16(in + 6);
    out->pressure_pa = get_u32(in + 8);
    out->flags = in[12];
    out->boot_id = in[13];
    return true;
}
