#include <math.h>
#include "unity.h"
#include "bme280.h"

void setUp(void) {}
void tearDown(void) {}

static const bme280_calib_t calib = {
    .dig_t1 = 27504, .dig_t2 = 26435, .dig_t3 = -1000,
    .dig_p1 = 36477, .dig_p2 = -10685, .dig_p3 = 3024, .dig_p4 = 2855, .dig_p5 = 140,
    .dig_p6 = -7, .dig_p7 = 15500, .dig_p8 = -14600, .dig_p9 = 6000,
    .dig_h1 = 75, .dig_h2 = 367, .dig_h3 = 0, .dig_h4 = 301, .dig_h5 = 50, .dig_h6 = 30,
};

// Datasheet double-precision reference formulas (independent of the integer implementation).
static double ref_temp(const bme280_calib_t *c, int32_t adc_t, double *t_fine)
{
    double var1 = (adc_t / 16384.0 - c->dig_t1 / 1024.0) * c->dig_t2;
    double var2 = (adc_t / 131072.0 - c->dig_t1 / 8192.0) * (adc_t / 131072.0 - c->dig_t1 / 8192.0) * c->dig_t3;
    *t_fine = var1 + var2;
    return (var1 + var2) / 5120.0;
}

static double ref_press(const bme280_calib_t *c, int32_t adc_p, double t_fine)
{
    double var1 = t_fine / 2.0 - 64000.0;
    double var2 = var1 * var1 * c->dig_p6 / 32768.0;
    var2 = var2 + var1 * c->dig_p5 * 2.0;
    var2 = var2 / 4.0 + c->dig_p4 * 65536.0;
    var1 = (c->dig_p3 * var1 * var1 / 524288.0 + c->dig_p2 * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * c->dig_p1;
    double p = 1048576.0 - adc_p;
    p = (p - var2 / 4096.0) * 6250.0 / var1;
    var1 = c->dig_p9 * p * p / 2147483648.0;
    var2 = p * c->dig_p8 / 32768.0;
    return p + (var1 + var2 + c->dig_p7) / 16.0;
}

static double ref_hum(const bme280_calib_t *c, int32_t adc_h, double t_fine)
{
    double h = t_fine - 76800.0;
    h = (adc_h - (c->dig_h4 * 64.0 + c->dig_h5 / 16384.0 * h)) *
        (c->dig_h2 / 65536.0 * (1.0 + c->dig_h6 / 67108864.0 * h * (1.0 + c->dig_h3 / 67108864.0 * h)));
    h = h * (1.0 - c->dig_h1 * h / 524288.0);
    if (h > 100.0) h = 100.0;
    if (h < 0.0) h = 0.0;
    return h;
}

void test_datasheet_example_temperature_and_pressure(void) {
    bme280_reading_t r;
    bme280_compensate(&calib, 519888, 415148, 30000, &r);
    TEST_ASSERT_INT32_WITHIN(1, 2508, r.temp_centi);
    TEST_ASSERT_UINT32_WITHIN(10, 100653, r.pressure_pa);
}

void test_matches_double_reference_over_range(void) {
    static const int32_t adc_t[] = { 400000, 519888, 600000 };
    static const int32_t adc_p[] = { 300000, 415148, 500000 };
    static const int32_t adc_h[] = { 20000, 30000, 45000 };
    for (int i = 0; i < 3; i++) {
        double t_fine;
        double t = ref_temp(&calib, adc_t[i], &t_fine);
        double p = ref_press(&calib, adc_p[i], t_fine);
        double h = ref_hum(&calib, adc_h[i], t_fine);
        bme280_reading_t r;
        bme280_compensate(&calib, adc_t[i], adc_p[i], adc_h[i], &r);
        TEST_ASSERT_INT32_WITHIN(2, (int32_t)lround(t * 100.0), r.temp_centi);
        TEST_ASSERT_UINT32_WITHIN(20, (uint32_t)lround(p), r.pressure_pa);
        TEST_ASSERT_UINT32_WITHIN(20, (uint32_t)lround(h * 100.0), r.hum_centi);
    }
}

void test_parse_calib_decodes_layout(void) {
    uint8_t c1[26] = {0};
    uint8_t c2[7] = {0};
    c1[0] = 0x70; c1[1] = 0x6B;            // dig_t1 = 27504
    c1[2] = 0x43; c1[3] = 0x67;            // dig_t2 = 26435
    c1[4] = 0x18; c1[5] = 0xFC;            // dig_t3 = -1000
    c1[6] = 0x7D; c1[7] = 0x8E;            // dig_p1 = 36477
    c1[8] = 0x43; c1[9] = 0xD6;            // dig_p2 = -10685
    c1[10] = 0xD0; c1[11] = 0x0B;          // dig_p3 = 3024
    c1[12] = 0x27; c1[13] = 0x0B;          // dig_p4 = 2855
    c1[14] = 0x8C; c1[15] = 0x00;          // dig_p5 = 140
    c1[16] = 0xF9; c1[17] = 0xFF;          // dig_p6 = -7
    c1[18] = 0x8C; c1[19] = 0x3C;          // dig_p7 = 15500
    c1[20] = 0xF8; c1[21] = 0xC6;          // dig_p8 = -14600
    c1[22] = 0x70; c1[23] = 0x17;          // dig_p9 = 6000
    c1[25] = 75;                           // dig_h1
    c2[0] = 0x6F; c2[1] = 0x01;            // dig_h2 = 367
    c2[2] = 0x12;                          // dig_h3 = 18
    c2[3] = 0x12; c2[4] = 0x2D; c2[5] = 0x03;   // dig_h4 = 0x12D = 301, dig_h5 = 0x32 = 50
    c2[6] = 30;                            // dig_h6
    bme280_calib_t c;
    bme280_parse_calib(c1, c2, &c);
    TEST_ASSERT_EQUAL_UINT16(27504, c.dig_t1);
    TEST_ASSERT_EQUAL_INT16(26435, c.dig_t2);
    TEST_ASSERT_EQUAL_INT16(-1000, c.dig_t3);
    TEST_ASSERT_EQUAL_UINT16(36477, c.dig_p1);
    TEST_ASSERT_EQUAL_INT16(-10685, c.dig_p2);
    TEST_ASSERT_EQUAL_INT16(3024, c.dig_p3);
    TEST_ASSERT_EQUAL_INT16(2855, c.dig_p4);
    TEST_ASSERT_EQUAL_INT16(140, c.dig_p5);
    TEST_ASSERT_EQUAL_INT16(-7, c.dig_p6);
    TEST_ASSERT_EQUAL_INT16(15500, c.dig_p7);
    TEST_ASSERT_EQUAL_INT16(-14600, c.dig_p8);
    TEST_ASSERT_EQUAL_INT16(6000, c.dig_p9);
    TEST_ASSERT_EQUAL_UINT8(75, c.dig_h1);
    TEST_ASSERT_EQUAL_INT16(367, c.dig_h2);
    TEST_ASSERT_EQUAL_UINT8(0x12, c.dig_h3);
    TEST_ASSERT_EQUAL_INT16(301, c.dig_h4);
    TEST_ASSERT_EQUAL_INT16(50, c.dig_h5);
    TEST_ASSERT_EQUAL_INT8(30, c.dig_h6);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_datasheet_example_temperature_and_pressure);
    RUN_TEST(test_matches_double_reference_over_range);
    RUN_TEST(test_parse_calib_decodes_layout);
    return UNITY_END();
}
