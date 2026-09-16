#include "bme280.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void bme280_parse_calib(const uint8_t calib1[26], const uint8_t calib2[7], bme280_calib_t *c)
{
    c->dig_t1 = le16(calib1 + 0);
    c->dig_t2 = (int16_t)le16(calib1 + 2);
    c->dig_t3 = (int16_t)le16(calib1 + 4);
    c->dig_p1 = le16(calib1 + 6);
    c->dig_p2 = (int16_t)le16(calib1 + 8);
    c->dig_p3 = (int16_t)le16(calib1 + 10);
    c->dig_p4 = (int16_t)le16(calib1 + 12);
    c->dig_p5 = (int16_t)le16(calib1 + 14);
    c->dig_p6 = (int16_t)le16(calib1 + 16);
    c->dig_p7 = (int16_t)le16(calib1 + 18);
    c->dig_p8 = (int16_t)le16(calib1 + 20);
    c->dig_p9 = (int16_t)le16(calib1 + 22);
    c->dig_h1 = calib1[25];
    c->dig_h2 = (int16_t)le16(calib2 + 0);
    c->dig_h3 = calib2[2];
    c->dig_h4 = (int16_t)((int16_t)(int8_t)calib2[3] * 16 | (calib2[4] & 0x0F));
    c->dig_h5 = (int16_t)((int16_t)(int8_t)calib2[5] * 16 | (calib2[4] >> 4));
    c->dig_h6 = (int8_t)calib2[6];
}

void bme280_compensate(const bme280_calib_t *c, int32_t adc_t, int32_t adc_p, int32_t adc_h, bme280_reading_t *out)
{
    int32_t var1, var2, t_fine;

    var1 = ((((adc_t >> 3) - ((int32_t)c->dig_t1 << 1))) * ((int32_t)c->dig_t2)) >> 11;
    var2 = (((((adc_t >> 4) - ((int32_t)c->dig_t1)) * ((adc_t >> 4) - ((int32_t)c->dig_t1))) >> 12) *
            ((int32_t)c->dig_t3)) >> 14;
    t_fine = var1 + var2;
    out->temp_centi = (t_fine * 5 + 128) >> 8;

    var1 = (t_fine >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)c->dig_p6);
    var2 = var2 + ((var1 * ((int32_t)c->dig_p5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)c->dig_p4) << 16);
    var1 = (((c->dig_p3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((int32_t)c->dig_p2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t)c->dig_p1)) >> 15);
    if (var1 == 0) {
        out->pressure_pa = 0;
    } else {
        uint32_t p = (((uint32_t)(((int32_t)1048576) - adc_p) - (uint32_t)(var2 >> 12))) * 3125;
        if (p < 0x80000000) {
            p = (p << 1) / ((uint32_t)var1);
        } else {
            p = (p / (uint32_t)var1) * 2;
        }
        var1 = (((int32_t)c->dig_p9) * ((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
        var2 = (((int32_t)(p >> 2)) * ((int32_t)c->dig_p8)) >> 13;
        out->pressure_pa = (uint32_t)((int32_t)p + ((var1 + var2 + c->dig_p7) >> 4));
    }

    int32_t v = t_fine - ((int32_t)76800);
    v = (((((adc_h << 14) - (((int32_t)c->dig_h4) << 20) - (((int32_t)c->dig_h5) * v)) + ((int32_t)16384)) >> 15) *
         (((((((v * ((int32_t)c->dig_h6)) >> 10) * (((v * ((int32_t)c->dig_h3)) >> 11) + ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) * ((int32_t)c->dig_h2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)c->dig_h1)) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    uint32_t h_q22_10 = (uint32_t)(v >> 12);      // %RH * 1024
    out->hum_centi = (h_q22_10 * 100u + 512u) / 1024u;
}
