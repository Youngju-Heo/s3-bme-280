#include "unity.h"
#include "web-bridge.h"

void setUp(void) {}
void tearDown(void) {}

static char out[128];

void test_get_read_only_commands(void) {
    TEST_ASSERT_TRUE(web_bridge_build_request("GET", "cmd=ping", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"ping\"}", out);
    TEST_ASSERT_TRUE(web_bridge_build_request("GET", "cmd=get_log&offset=1000&limit=500", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"get_log\",\"offset\":1000,\"limit\":500}", out);
    TEST_ASSERT_TRUE(web_bridge_build_request("GET", "limit=5&cmd=get_log", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"get_log\",\"limit\":5}", out);
}

void test_post_set_time_only(void) {
    TEST_ASSERT_TRUE(web_bridge_build_request("POST", "cmd=set_time&epoch=1789000000", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"set_time\",\"epoch\":1789000000}", out);
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=set_time&epoch=1", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("POST", "cmd=get_status", out, sizeof out));
}

void test_rejects_forbidden_and_malformed(void) {
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=clear_log", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("POST", "cmd=set_interval&interval_s=10", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("POST", "cmd=set_wifi&ssid=x", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "offset=1", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=get_log&offset=12a", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=get%5Flog", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=ping&unknown=1", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("DELETE", "cmd=ping", out, sizeof out));
}

void test_output_buffer_too_small(void) {
    char tiny[8];
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=ping", tiny, sizeof tiny));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_read_only_commands);
    RUN_TEST(test_post_set_time_only);
    RUN_TEST(test_rejects_forbidden_and_malformed);
    RUN_TEST(test_output_buffer_too_small);
    return UNITY_END();
}
