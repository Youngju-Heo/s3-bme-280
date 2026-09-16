#include "unity.h"
#include "fake-bus.h"

static bme280_t dev;

void setUp(void) { fake_bus_reset(); }
void tearDown(void) {}

static void load_example_calib(void)
{
    uint8_t *c1 = fake_bus_regs + BME280_REG_CALIB1;
    uint8_t *c2 = fake_bus_regs + BME280_REG_CALIB2;
    c1[0] = 0x70; c1[1] = 0x6B; c1[2] = 0x43; c1[3] = 0x67; c1[4] = 0x18; c1[5] = 0xFC;
    c1[6] = 0x7D; c1[7] = 0x8E; c1[8] = 0x43; c1[9] = 0xD6; c1[10] = 0xD0; c1[11] = 0x0B;
    c1[12] = 0x27; c1[13] = 0x0B; c1[14] = 0x8C; c1[15] = 0x00; c1[16] = 0xF9; c1[17] = 0xFF;
    c1[18] = 0x8C; c1[19] = 0x3C; c1[20] = 0xF8; c1[21] = 0xC6; c1[22] = 0x70; c1[23] = 0x17;
    c1[25] = 75;
    c2[0] = 0x6F; c2[1] = 0x01; c2[2] = 0; c2[3] = 0x12; c2[4] = 0x2D; c2[5] = 0x03; c2[6] = 30;
}

void test_init_rejects_wrong_chip_id(void) {
    fake_bus_regs[BME280_REG_CHIP_ID] = 0x58;
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, bme280_init(&dev, &fake_bus));
}

void test_init_reports_bus_error(void) {
    fake_bus_fail = true;
    TEST_ASSERT_EQUAL(BME280_ERR_BUS, bme280_init(&dev, &fake_bus));
}

void test_init_resets_reads_calib_and_configures(void) {
    load_example_calib();
    TEST_ASSERT_EQUAL(BME280_OK, bme280_init(&dev, &fake_bus));
    TEST_ASSERT_EQUAL_HEX8(BME280_REG_RESET, fake_bus_write_log[0][0]);
    TEST_ASSERT_EQUAL_HEX8(0xB6, fake_bus_write_log[0][1]);
    TEST_ASSERT_EQUAL_UINT16(27504, dev.calib.dig_t1);
    TEST_ASSERT_EQUAL_INT16(301, dev.calib.dig_h4);
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_bus_regs[BME280_REG_CTRL_HUM]);    // osrs_h x1
    TEST_ASSERT_EQUAL_HEX8(0x00, fake_bus_regs[BME280_REG_CONFIG]);      // filter off, 4-wire SPI
    TEST_ASSERT_EQUAL_HEX8(0x24, fake_bus_regs[BME280_REG_CTRL_MEAS]);   // osrs_t x1, osrs_p x1, sleep
}

void test_read_triggers_forced_mode_and_compensates(void) {
    load_example_calib();
    TEST_ASSERT_EQUAL(BME280_OK, bme280_init(&dev, &fake_bus));
    uint8_t *d = fake_bus_regs + BME280_REG_DATA;
    // adc_p = 415148 = 0x655AC -> 0x65 0x5A 0xC0 ; adc_t = 519888 = 0x7EED0 -> 0x7E 0xED 0x00 ; adc_h = 30000 = 0x7530
    d[0] = 0x65; d[1] = 0x5A; d[2] = 0xC0; d[3] = 0x7E; d[4] = 0xED; d[5] = 0x00; d[6] = 0x75; d[7] = 0x30;
    bme280_reading_t r;
    TEST_ASSERT_EQUAL(BME280_OK, bme280_read(&dev, &r));
    TEST_ASSERT_EQUAL_HEX8(0x25, fake_bus_last_write(BME280_REG_CTRL_MEAS));   // forced mode
    bme280_reading_t expect;
    bme280_compensate(&dev.calib, 519888, 415148, 30000, &expect);
    TEST_ASSERT_EQUAL_INT32(expect.temp_centi, r.temp_centi);
    TEST_ASSERT_EQUAL_UINT32(expect.pressure_pa, r.pressure_pa);
    TEST_ASSERT_EQUAL_UINT32(expect.hum_centi, r.hum_centi);
}

void test_read_times_out_when_measuring_never_clears(void) {
    load_example_calib();
    TEST_ASSERT_EQUAL(BME280_OK, bme280_init(&dev, &fake_bus));
    fake_bus_regs[BME280_REG_STATUS] = 0x08;   // measuring bit stuck
    bme280_reading_t r;
    TEST_ASSERT_EQUAL(BME280_ERR_TIMEOUT, bme280_read(&dev, &r));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_wrong_chip_id);
    RUN_TEST(test_init_reports_bus_error);
    RUN_TEST(test_init_resets_reads_calib_and_configures);
    RUN_TEST(test_read_triggers_forced_mode_and_compensates);
    RUN_TEST(test_read_times_out_when_measuring_never_clears);
    return UNITY_END();
}
