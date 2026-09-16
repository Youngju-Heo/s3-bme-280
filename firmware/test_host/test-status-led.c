#include "unity.h"
#include "status-led-color.h"

void setUp(void) {}
void tearDown(void) {}

static void assert_rgb(status_led_rgb_t c, uint8_t r, uint8_t g, uint8_t b)
{
    TEST_ASSERT_EQUAL_UINT8(r, c.r);
    TEST_ASSERT_EQUAL_UINT8(g, c.g);
    TEST_ASSERT_EQUAL_UINT8(b, c.b);
}

void test_ok_is_dim_steady_green(void) {
    assert_rgb(status_led_color(STATUS_LED_OK, 0), 0, STATUS_LED_BRIGHTNESS, 0);
    assert_rgb(status_led_color(STATUS_LED_OK, 700), 0, STATUS_LED_BRIGHTNESS, 0);
}

void test_error_is_dim_steady_red(void) {
    assert_rgb(status_led_color(STATUS_LED_ERROR, 0), STATUS_LED_BRIGHTNESS, 0, 0);
    assert_rgb(status_led_color(STATUS_LED_ERROR, 700), STATUS_LED_BRIGHTNESS, 0, 0);
}

void test_no_time_blinks_green_at_half_second(void) {
    assert_rgb(status_led_color(STATUS_LED_OK_NO_TIME, 0), 0, STATUS_LED_BRIGHTNESS, 0);
    assert_rgb(status_led_color(STATUS_LED_OK_NO_TIME, 499), 0, STATUS_LED_BRIGHTNESS, 0);
    assert_rgb(status_led_color(STATUS_LED_OK_NO_TIME, 500), 0, 0, 0);
    assert_rgb(status_led_color(STATUS_LED_OK_NO_TIME, 999), 0, 0, 0);
    assert_rgb(status_led_color(STATUS_LED_OK_NO_TIME, 1000), 0, STATUS_LED_BRIGHTNESS, 0);
}

void test_off(void) {
    assert_rgb(status_led_color(STATUS_LED_OFF, 123), 0, 0, 0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ok_is_dim_steady_green);
    RUN_TEST(test_error_is_dim_steady_red);
    RUN_TEST(test_no_time_blinks_green_at_half_second);
    RUN_TEST(test_off);
    return UNITY_END();
}
