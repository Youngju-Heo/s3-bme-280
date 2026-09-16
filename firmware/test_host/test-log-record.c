#include <string.h>
#include "unity.h"
#include "log-record.h"

void setUp(void) {}
void tearDown(void) {}

static const log_record_t sample = {
    .timestamp = 1789000000u, .temp_centi = -520, .hum_centi = 4120,
    .pressure_pa = 101325u, .flags = LOG_RECORD_FLAG_TIME_VALID, .boot_id = 7,
};

void test_crc16_ccitt_known_vector(void) {
    // CRC-16/CCITT-FALSE("123456789") == 0x29B1
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt((const uint8_t *)"123456789", 9));
}

void test_encode_layout_is_little_endian(void) {
    uint8_t buf[LOG_RECORD_SIZE];
    log_record_encode(&sample, buf);
    TEST_ASSERT_EQUAL_HEX8(0x40, buf[0]);  // 1789000000 = 0x6AA1F940
    TEST_ASSERT_EQUAL_HEX8(0xF9, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xA1, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x6A, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, buf[4]);  // -520 = 0xFDF8
    TEST_ASSERT_EQUAL_HEX8(0xFD, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[12]);
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[13]);
}

void test_encode_decode_roundtrip(void) {
    uint8_t buf[LOG_RECORD_SIZE];
    log_record_t out;
    log_record_encode(&sample, buf);
    TEST_ASSERT_TRUE(log_record_decode(buf, &out));
    TEST_ASSERT_EQUAL_UINT32(sample.timestamp, out.timestamp);
    TEST_ASSERT_EQUAL_INT16(sample.temp_centi, out.temp_centi);
    TEST_ASSERT_EQUAL_UINT16(sample.hum_centi, out.hum_centi);
    TEST_ASSERT_EQUAL_UINT32(sample.pressure_pa, out.pressure_pa);
    TEST_ASSERT_EQUAL_UINT8(sample.flags, out.flags);
    TEST_ASSERT_EQUAL_UINT8(sample.boot_id, out.boot_id);
}

void test_decode_rejects_crc_mismatch(void) {
    uint8_t buf[LOG_RECORD_SIZE];
    log_record_t out;
    log_record_encode(&sample, buf);
    buf[8] ^= 0x01;
    TEST_ASSERT_FALSE(log_record_decode(buf, &out));
}

void test_empty_slot_detection(void) {
    uint8_t buf[LOG_RECORD_SIZE];
    log_record_t out;
    memset(buf, 0xFF, sizeof buf);
    TEST_ASSERT_TRUE(log_record_is_empty(buf));
    TEST_ASSERT_FALSE(log_record_decode(buf, &out));
    buf[15] = 0x00;
    TEST_ASSERT_FALSE(log_record_is_empty(buf));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_ccitt_known_vector);
    RUN_TEST(test_encode_layout_is_little_endian);
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_decode_rejects_crc_mismatch);
    RUN_TEST(test_empty_slot_detection);
    return UNITY_END();
}
