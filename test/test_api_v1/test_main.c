#include <string.h>

#include "unity.h"
#include "../../components/bm24_api_v1/bm24_api_v1.c"

static const char *ID = "123e4567-e89b-42d3-a456-426614174000";

void setUp(void) {}
void tearDown(void) {}

static void test_info_contains_stable_contract_and_safe_capabilities(void)
{
    char json[768];
    bm24_api_v1_info info = {
        .device_id = ID,
        .firmware_version = "2.1.0-app-alpha.1",
    };
    TEST_ASSERT_TRUE(bm24_api_v1_format_info(&info, json, sizeof(json)));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"api_version\":\"1.0\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"device_type\":\"nerdminer\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_config\":false"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"power_reporting\":\"unavailable\""));
}

static void test_status_keeps_unavailable_measurements_null(void)
{
    char json[1280];
    bm24_api_v1_status status = {
        .device_id = ID,
        .observed_at_json = "null",
        .status = "degraded",
        .hashrate_available = false,
        .temperature_available = false,
        .shares_submitted_available = false,
        .shares_accepted_available = true,
        .shares_accepted = 7,
        .best_difficulty = 123.5,
        .blocks_found = 0,
        .pool_connected = false,
        .uptime_s = 42,
        .last_activity_at_json = "null",
    };
    TEST_ASSERT_TRUE(bm24_api_v1_format_status(&status, json, sizeof(json)));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_hps\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"power_w\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"submitted\":null,\"accepted\":7"));
}

static void test_small_output_buffer_fails_closed(void)
{
    char json[32];
    bm24_api_v1_info info = { .device_id = ID, .firmware_version = "2.1.0" };
    TEST_ASSERT_FALSE(bm24_api_v1_format_info(&info, json, sizeof(json)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_info_contains_stable_contract_and_safe_capabilities);
    RUN_TEST(test_status_keeps_unavailable_measurements_null);
    RUN_TEST(test_small_output_buffer_fails_closed);
    return UNITY_END();
}
