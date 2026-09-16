#include "unity.h"
#include "fake-flash.h"

static log_store_t store;

void setUp(void) { fake_flash_reset(); TEST_ASSERT_EQUAL(0, log_store_init(&store, &fake_flash)); }
void tearDown(void) {}

static void append_n(uint32_t n, uint32_t first_ts)
{
    for (uint32_t i = 0; i < n; i++) {
        log_record_t r = { .timestamp = first_ts + i, .temp_centi = 2000, .hum_centi = 5000,
                           .pressure_pa = 100000, .flags = 0, .boot_id = 1 };
        TEST_ASSERT_EQUAL(0, log_store_append(&store, &r));
    }
}

static uint32_t oldest_ts(void)
{
    log_record_t r; uint32_t n;
    TEST_ASSERT_EQUAL(0, log_store_read(&store, 0, &r, 1, &n));
    TEST_ASSERT_EQUAL_UINT32(1, n);
    return r.timestamp;
}

void test_fresh_flash_is_empty(void) {
    TEST_ASSERT_EQUAL_UINT32(0, log_store_count(&store));
}

void test_append_then_read(void) {
    append_n(1, 100);
    log_record_t r; uint32_t n;
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
    TEST_ASSERT_EQUAL(0, log_store_read(&store, 0, &r, 10, &n));
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_UINT32(100, r.timestamp);
    TEST_ASSERT_EQUAL_INT16(2000, r.temp_centi);
}

void test_read_with_offset_and_limit(void) {
    append_n(300, 0);
    log_record_t r[5]; uint32_t n;
    TEST_ASSERT_EQUAL(0, log_store_read(&store, 297, r, 5, &n));
    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_UINT32(297, r[0].timestamp);
    TEST_ASSERT_EQUAL_UINT32(299, r[2].timestamp);
    TEST_ASSERT_EQUAL(0, log_store_read(&store, 300, r, 5, &n));
    TEST_ASSERT_EQUAL_UINT32(0, n);
}

void test_erase_ahead_when_entering_sector(void) {
    append_n(256, 0);                       // fills sector 0; sector 1 erased on first write
    TEST_ASSERT_EQUAL(1, fake_flash_erase_count[1]);
    TEST_ASSERT_EQUAL(0, fake_flash_erase_count[2]);
    append_n(1, 256);                       // first write in sector 1 -> erase sector 2
    TEST_ASSERT_EQUAL(1, fake_flash_erase_count[2]);
}

void test_capacity_is_fixed_and_rolls_by_sector(void) {
    append_n(LOG_STORE_CAPACITY, 0);
    TEST_ASSERT_EQUAL_UINT32(LOG_STORE_CAPACITY, log_store_count(&store));
    TEST_ASSERT_EQUAL_UINT32(0, oldest_ts());
    append_n(1, LOG_STORE_CAPACITY);        // enters sector 127 -> erases sector 0 -> oldest 256 dropped
    TEST_ASSERT_EQUAL_UINT32(LOG_STORE_CAPACITY - 256 + 1, log_store_count(&store));
    TEST_ASSERT_EQUAL_UINT32(256, oldest_ts());
    append_n(255, LOG_STORE_CAPACITY + 1);
    TEST_ASSERT_EQUAL_UINT32(LOG_STORE_CAPACITY, log_store_count(&store));
    append_n(1, LOG_STORE_CAPACITY + 256);  // wraps to slot 0 -> erases sector 1
    TEST_ASSERT_EQUAL_UINT32(512, oldest_ts());
    TEST_ASSERT_EQUAL_UINT32(LOG_STORE_CAPACITY - 256 + 1, log_store_count(&store));
}

void test_recovery_before_wrap(void) {
    append_n(1000, 0);
    log_store_t again;
    TEST_ASSERT_EQUAL(0, log_store_init(&again, &fake_flash));
    TEST_ASSERT_EQUAL_UINT32(store.head, again.head);
    TEST_ASSERT_EQUAL_UINT32(store.tail, again.tail);
    TEST_ASSERT_EQUAL_UINT32(1000, log_store_count(&again));
}

void test_recovery_after_wrap(void) {
    append_n(40000, 0);
    log_store_t again;
    TEST_ASSERT_EQUAL(0, log_store_init(&again, &fake_flash));
    TEST_ASSERT_EQUAL_UINT32(store.head, again.head);
    TEST_ASSERT_EQUAL_UINT32(store.tail, again.tail);
    TEST_ASSERT_EQUAL_UINT32(log_store_count(&store), log_store_count(&again));
}

void test_recovery_when_head_sector_is_full(void) {
    append_n(512, 0);                       // sectors 0,1 full; head = 512 (sector 2 first slot)
    log_store_t again;
    TEST_ASSERT_EQUAL(0, log_store_init(&again, &fake_flash));
    TEST_ASSERT_EQUAL_UINT32(512, again.head);
    TEST_ASSERT_EQUAL_UINT32(0, again.tail);
}

void test_recovery_skips_partially_written_slot(void) {
    append_n(10, 0);
    // simulate power loss mid-write: slot 10 has garbage in bytes 4.. but first 4 bytes still 0xFF
    memset(fake_flash_mem + 10 * LOG_RECORD_SIZE + 4, 0x12, 12);
    log_store_t again;
    TEST_ASSERT_EQUAL(0, log_store_init(&again, &fake_flash));
    TEST_ASSERT_EQUAL_UINT32(11, again.head);
    log_record_t r[20]; uint32_t n;
    TEST_ASSERT_EQUAL(0, log_store_read(&again, 0, r, 20, &n));
    TEST_ASSERT_EQUAL_UINT32(10, n);        // corrupt slot skipped
}

void test_read_skips_crc_corrupted_record(void) {
    append_n(3, 0);
    fake_flash_mem[1 * LOG_RECORD_SIZE + 8] &= 0xF0;   // damage pressure byte of record 1
    log_record_t r[3]; uint32_t n;
    TEST_ASSERT_EQUAL(0, log_store_read(&store, 0, r, 3, &n));
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_UINT32(0, r[0].timestamp);
    TEST_ASSERT_EQUAL_UINT32(2, r[1].timestamp);
}

void test_clear_erases_everything(void) {
    append_n(1000, 0);
    TEST_ASSERT_EQUAL(0, log_store_clear(&store));
    TEST_ASSERT_EQUAL_UINT32(0, log_store_count(&store));
    TEST_ASSERT_EQUAL_UINT32(0, store.head);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_flash_mem[0]);
}

void test_init_reports_corrupt_when_no_erased_sector(void) {
    memset(fake_flash_mem, 0x00, sizeof fake_flash_mem);
    log_store_t again;
    TEST_ASSERT_EQUAL(LOG_STORE_ERR_CORRUPT, log_store_init(&again, &fake_flash));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_flash_is_empty);
    RUN_TEST(test_append_then_read);
    RUN_TEST(test_read_with_offset_and_limit);
    RUN_TEST(test_erase_ahead_when_entering_sector);
    RUN_TEST(test_capacity_is_fixed_and_rolls_by_sector);
    RUN_TEST(test_recovery_before_wrap);
    RUN_TEST(test_recovery_after_wrap);
    RUN_TEST(test_recovery_when_head_sector_is_full);
    RUN_TEST(test_recovery_skips_partially_written_slot);
    RUN_TEST(test_read_skips_crc_corrupted_record);
    RUN_TEST(test_clear_erases_everything);
    RUN_TEST(test_init_reports_corrupt_when_no_erased_sector);
    return UNITY_END();
}
