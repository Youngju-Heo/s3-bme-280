#include "unity.h"
#include "json-mini.h"

void setUp(void) {}
void tearDown(void) {}

void test_get_string(void) {
    char out[32];
    TEST_ASSERT_TRUE(json_mini_get_string("{\"cmd\":\"get_log\",\"offset\":0}", "cmd", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("get_log", out);
}

void test_get_string_with_spaces(void) {
    char out[32];
    TEST_ASSERT_TRUE(json_mini_get_string("{ \"cmd\" : \"ping\" }", "cmd", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("ping", out);
}

void test_get_string_missing_or_wrong_type(void) {
    char out[32];
    TEST_ASSERT_FALSE(json_mini_get_string("{\"offset\":5}", "cmd", out, sizeof out));
    TEST_ASSERT_FALSE(json_mini_get_string("{\"cmd\":5}", "cmd", out, sizeof out));
    TEST_ASSERT_FALSE(json_mini_get_string("{\"cmd\":\"unterminated", "cmd", out, sizeof out));
}

void test_get_string_too_long(void) {
    char out[4];
    TEST_ASSERT_FALSE(json_mini_get_string("{\"cmd\":\"ping\"}", "cmd", out, sizeof out));
}

void test_get_uint(void) {
    uint32_t v = 0;
    TEST_ASSERT_TRUE(json_mini_get_uint("{\"cmd\":\"get_log\",\"offset\":1234,\"limit\":500}", "offset", &v));
    TEST_ASSERT_EQUAL_UINT32(1234, v);
    TEST_ASSERT_TRUE(json_mini_get_uint("{\"epoch\": 1789000000}", "epoch", &v));
    TEST_ASSERT_EQUAL_UINT32(1789000000u, v);
}

void test_get_uint_missing_or_not_number(void) {
    uint32_t v = 0;
    TEST_ASSERT_FALSE(json_mini_get_uint("{\"cmd\":\"ping\"}", "epoch", &v));
    TEST_ASSERT_FALSE(json_mini_get_uint("{\"epoch\":\"abc\"}", "epoch", &v));
    TEST_ASSERT_FALSE(json_mini_get_uint("{\"epoch\":-5}", "epoch", &v));
}

void test_key_must_be_quoted_exactly(void) {
    uint32_t v = 0;
    TEST_ASSERT_FALSE(json_mini_get_uint("{\"my_offset\":7}", "offset", &v));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_string);
    RUN_TEST(test_get_string_with_spaces);
    RUN_TEST(test_get_string_missing_or_wrong_type);
    RUN_TEST(test_get_string_too_long);
    RUN_TEST(test_get_uint);
    RUN_TEST(test_get_uint_missing_or_not_number);
    RUN_TEST(test_key_must_be_quoted_exactly);
    return UNITY_END();
}
