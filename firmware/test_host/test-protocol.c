#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "protocol.h"
#include "fake-flash.h"

static log_store_t store;
static char out[65536];
static size_t out_len;

static struct {
    uint8_t boot_id; uint32_t uptime_s; bool time_valid; uint32_t set_epoch;
    int read_rc; bme280_reading_t reading; bool sensor_ok; uint32_t interval_s;
} fake;

static uint8_t f_boot_id(void *c) { (void)c; return fake.boot_id; }
static uint32_t f_uptime_s(void *c) { (void)c; return fake.uptime_s; }
static bool f_time_valid(void *c) { (void)c; return fake.time_valid; }
static void f_set_time(void *c, uint32_t e) { (void)c; fake.set_epoch = e; fake.time_valid = true; }
static int f_read_now(void *c, bme280_reading_t *r) { (void)c; *r = fake.reading; return fake.read_rc; }
static bool f_sensor_ok(void *c) { (void)c; return fake.sensor_ok; }
static uint32_t f_interval_s(void *c) { (void)c; return fake.interval_s; }
static int f_set_interval_s(void *c, uint32_t s) { (void)c; if (s < 10 || s > 3600) return -1; fake.interval_s = s; return 0; }

static const protocol_ops_t ops = {
    .ctx = NULL, .store = &store, .boot_id = f_boot_id, .uptime_s = f_uptime_s, .time_valid = f_time_valid,
    .set_time = f_set_time, .read_now = f_read_now, .sensor_ok = f_sensor_ok,
    .interval_s = f_interval_s, .set_interval_s = f_set_interval_s,
};

static void capture(void *ctx, const char *data, size_t len)
{
    (void)ctx;
    if (out_len + len < sizeof out) { memcpy(out + out_len, data, len); out_len += len; out[out_len] = 0; }
}

static const char *handle(const char *line) { out_len = 0; out[0] = 0; protocol_handle_line(line, capture, NULL); return out; }

void setUp(void)
{
    fake_flash_reset();
    log_store_init(&store, &fake_flash);
    memset(&fake, 0, sizeof fake);
    fake.boot_id = 3; fake.uptime_s = 120; fake.sensor_ok = true; fake.interval_s = 60;
    fake.reading = (bme280_reading_t){ .temp_centi = 2345, .pressure_pa = 101325, .hum_centi = 4120 };
    protocol_init(&ops);
}
void tearDown(void) {}

void test_ping(void) {
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"firmware\":\"0.1.0\",\"boot_id\":3,\"uptime_s\":120,\"time_valid\":false}\n",
                             handle("{\"cmd\":\"ping\"}"));
}

void test_unknown_and_bad_request(void) {
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"unknown_cmd\"}\n", handle("{\"cmd\":\"nope\"}"));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"bad_request\"}\n", handle("hello"));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"bad_request\"}\n", handle("{\"cmd\":\"set_time\"}"));
}

void test_set_time(void) {
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"boot_id\":3,\"uptime_s\":120}\n", handle("{\"cmd\":\"set_time\",\"epoch\":1789000000}"));
    TEST_ASSERT_EQUAL_UINT32(1789000000u, fake.set_epoch);
}

void test_read_now_formats_decimals(void) {
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"temp_c\":23.45,\"hum_pct\":41.20,\"pressure_pa\":101325}\n", handle("{\"cmd\":\"read_now\"}"));
    fake.reading.temp_centi = -520; fake.reading.hum_centi = 5;
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"temp_c\":-5.20,\"hum_pct\":0.05,\"pressure_pa\":101325}\n", handle("{\"cmd\":\"read_now\"}"));
    fake.read_rc = -1;
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"sensor_error\"}\n", handle("{\"cmd\":\"read_now\"}"));
}

void test_get_status(void) {
    log_record_t r = { .timestamp = 1, .boot_id = 3 };
    log_store_append(&store, &r);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"count\":1,\"capacity\":32512,\"interval_s\":60,\"sensor_ok\":true,\"time_valid\":false,\"boot_id\":3,\"uptime_s\":120}\n",
        handle("{\"cmd\":\"get_status\"}"));
}

void test_get_log_pages(void) {
    for (uint32_t i = 0; i < 3; i++) {
        log_record_t r = { .timestamp = 1000 + i, .temp_centi = -100, .hum_centi = 4000, .pressure_pa = 100000 + i,
                           .flags = LOG_RECORD_FLAG_TIME_VALID, .boot_id = 3 };
        log_store_append(&store, &r);
    }
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"total\":3,\"offset\":1,\"records\":[[1001,-100,4000,100001,1,3],[1002,-100,4000,100002,1,3]]}\n",
        handle("{\"cmd\":\"get_log\",\"offset\":1,\"limit\":2}"));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"total\":3,\"offset\":5,\"records\":[]}\n",
                             handle("{\"cmd\":\"get_log\",\"offset\":5,\"limit\":2}"));
}

void test_get_log_defaults_and_clamps_limit(void) {
    for (uint32_t i = 0; i < 600; i++) {
        log_record_t r = { .timestamp = i, .boot_id = 3 };
        log_store_append(&store, &r);
    }
    handle("{\"cmd\":\"get_log\"}");                 // offset 0, limit default 100
    int n = 0;
    for (const char *p = out; (p = strstr(p, "],[")) != NULL; p++) n++;
    TEST_ASSERT_EQUAL(99, n);
    handle("{\"cmd\":\"get_log\",\"limit\":9999}");  // clamped to 500
    n = 0;
    for (const char *p = out; (p = strstr(p, "],[")) != NULL; p++) n++;
    TEST_ASSERT_EQUAL(499, n);
}

void test_clear_log(void) {
    log_record_t r = { .timestamp = 1, .boot_id = 3 };
    log_store_append(&store, &r);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}\n", handle("{\"cmd\":\"clear_log\"}"));
    TEST_ASSERT_EQUAL_UINT32(0, log_store_count(&store));
}

void test_set_interval(void) {
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}\n", handle("{\"cmd\":\"set_interval\",\"interval_s\":120}"));
    TEST_ASSERT_EQUAL_UINT32(120, fake.interval_s);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"out_of_range\"}\n", handle("{\"cmd\":\"set_interval\",\"interval_s\":5}"));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"bad_request\"}\n", handle("{\"cmd\":\"set_interval\"}"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ping);
    RUN_TEST(test_unknown_and_bad_request);
    RUN_TEST(test_set_time);
    RUN_TEST(test_read_now_formats_decimals);
    RUN_TEST(test_get_status);
    RUN_TEST(test_get_log_pages);
    RUN_TEST(test_get_log_defaults_and_clamps_limit);
    RUN_TEST(test_clear_log);
    RUN_TEST(test_set_interval);
    return UNITY_END();
}
