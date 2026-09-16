#include "unity.h"
#include "sampler.h"
#include "fake-bus.h"
#include "fake-flash.h"

static bme280_t sensor;
static log_store_t store;

static void set_raw(uint32_t adc_t, uint32_t adc_p, uint32_t adc_h)
{
    uint8_t *d = fake_bus_regs + BME280_REG_DATA;
    d[0] = adc_p >> 12; d[1] = adc_p >> 4; d[2] = (adc_p & 0xF) << 4;
    d[3] = adc_t >> 12; d[4] = adc_t >> 4; d[5] = (adc_t & 0xF) << 4;
    d[6] = adc_h >> 8; d[7] = adc_h & 0xFF;
}

void setUp(void)
{
    fake_bus_reset();
    fake_flash_reset();
    uint8_t *c1 = fake_bus_regs + BME280_REG_CALIB1;
    c1[0] = 0x70; c1[1] = 0x6B; c1[2] = 0x43; c1[3] = 0x67; c1[4] = 0x18; c1[5] = 0xFC;
    c1[6] = 0x7D; c1[7] = 0x8E; c1[8] = 0x43; c1[9] = 0xD6; c1[10] = 0xD0; c1[11] = 0x0B;
    c1[12] = 0x27; c1[13] = 0x0B; c1[14] = 0x8C; c1[15] = 0x00; c1[16] = 0xF9; c1[17] = 0xFF;
    c1[18] = 0x8C; c1[19] = 0x3C; c1[20] = 0xF8; c1[21] = 0xC6; c1[22] = 0x70; c1[23] = 0x17;
    set_raw(519888, 415148, 30000);
    TEST_ASSERT_EQUAL(BME280_OK, bme280_init(&sensor, &fake_bus));
    TEST_ASSERT_EQUAL(0, log_store_init(&store, &fake_flash));
    sampler_init(&sensor, &store, 60);
}
void tearDown(void) {}

void test_first_tick_samples_immediately(void) {
    sampler_tick(0, 5000, false, 2);
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
    log_record_t r; uint32_t n;
    log_store_read(&store, 0, &r, 1, &n);
    TEST_ASSERT_EQUAL_UINT32(5000, r.timestamp);
    TEST_ASSERT_EQUAL_UINT8(0, r.flags);
    TEST_ASSERT_EQUAL_UINT8(2, r.boot_id);
    TEST_ASSERT_INT16_WITHIN(1, 2508, r.temp_centi);
    TEST_ASSERT_UINT32_WITHIN(10, 100653, r.pressure_pa);
}

void test_respects_interval(void) {
    sampler_tick(0, 0, false, 1);
    sampler_tick(30, 30, false, 1);
    sampler_tick(59, 59, false, 1);
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
    sampler_tick(60, 60, false, 1);
    TEST_ASSERT_EQUAL_UINT32(2, log_store_count(&store));
    sampler_tick(125, 125, false, 1);   // late tick still samples; next due at 185
    TEST_ASSERT_EQUAL_UINT32(3, log_store_count(&store));
    sampler_tick(184, 184, false, 1);
    TEST_ASSERT_EQUAL_UINT32(3, log_store_count(&store));
}

void test_time_valid_flag_and_epoch(void) {
    sampler_tick(0, 1789000000u, true, 1);
    log_record_t r; uint32_t n;
    log_store_read(&store, 0, &r, 1, &n);
    TEST_ASSERT_EQUAL_UINT32(1789000000u, r.timestamp);
    TEST_ASSERT_EQUAL_UINT8(LOG_RECORD_FLAG_TIME_VALID, r.flags);
}

void test_set_interval_applies_from_last_sample(void) {
    sampler_tick(0, 0, false, 1);
    sampler_set_interval(10);
    TEST_ASSERT_EQUAL_UINT32(10, sampler_interval());
    sampler_tick(9, 9, false, 1);
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
    sampler_tick(10, 10, false, 1);
    TEST_ASSERT_EQUAL_UINT32(2, log_store_count(&store));
}

void test_sensor_error_skips_record_and_flags(void) {
    TEST_ASSERT_TRUE(sampler_sensor_ok());
    fake_bus_fail = true;
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_EQUAL_UINT32(0, log_store_count(&store));
    TEST_ASSERT_FALSE(sampler_sensor_ok());
    fake_bus_fail = false;
    sampler_tick(60, 60, false, 1);
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
    TEST_ASSERT_TRUE(sampler_sensor_ok());
}

void test_no_sensor(void) {
    sampler_init(NULL, &store, 60);
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_EQUAL_UINT32(0, log_store_count(&store));
    TEST_ASSERT_FALSE(sampler_sensor_ok());
}

void test_store_failure_flags_store_not_ok(void) {
    TEST_ASSERT_TRUE(sampler_store_ok());
    fake_flash_fail_write = true;
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_EQUAL_UINT32(0, log_store_count(&store));
    TEST_ASSERT_FALSE(sampler_store_ok());
    fake_flash_fail_write = false;
    sampler_tick(60, 60, false, 1);
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
    TEST_ASSERT_TRUE(sampler_store_ok());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_tick_samples_immediately);
    RUN_TEST(test_respects_interval);
    RUN_TEST(test_time_valid_flag_and_epoch);
    RUN_TEST(test_set_interval_applies_from_last_sample);
    RUN_TEST(test_sensor_error_skips_record_and_flags);
    RUN_TEST(test_no_sensor);
    RUN_TEST(test_store_failure_flags_store_not_ok);
    return UNITY_END();
}
