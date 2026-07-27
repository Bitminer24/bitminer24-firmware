#include <string.h>
#include <unity.h>

#include "../../components/bm24_config/bm24_config.c"

void setUp(void) {}
void tearDown(void) {}

static bm24_config valid_config(void)
{
    bm24_config config;
    bm24_config_defaults(&config);
    strcpy(config.wifi_ssid, "Testnetz");
    strcpy(config.wifi_password, "achtzeichen");
    strcpy(config.worker, "bc1qexampleworker000000000000000000000000000");
    return config;
}

static void test_defaults_are_deliberately_not_provisioned(void)
{
    bm24_config config;
    bm24_config_defaults(&config);
    TEST_ASSERT_EQUAL(BM24_CONFIG_BAD_WIFI, bm24_config_validate(&config));
    TEST_ASSERT_EQUAL_STRING("public-pool.io", config.pool_host);
    TEST_ASSERT_EQUAL_UINT16(3333, config.pool_port);
}

static void test_valid_config(void)
{
    bm24_config config = valid_config();
    TEST_ASSERT_EQUAL(BM24_CONFIG_OK, bm24_config_validate(&config));
    TEST_ASSERT_TRUE(bm24_config_is_provisioned(&config));
}

static void test_open_wifi_and_long_wpa_password_are_supported(void)
{
    bm24_config config = valid_config();
    config.wifi_password[0] = '\0';
    TEST_ASSERT_EQUAL(BM24_CONFIG_OK, bm24_config_validate(&config));

    memset(config.wifi_password, 'p', BM24_WIFI_PASSWORD_MAX);
    config.wifi_password[BM24_WIFI_PASSWORD_MAX] = '\0';
    TEST_ASSERT_EQUAL(BM24_CONFIG_OK, bm24_config_validate(&config));
}

static void test_short_wifi_password_is_rejected(void)
{
    bm24_config config = valid_config();
    strcpy(config.wifi_password, "short");
    TEST_ASSERT_EQUAL(BM24_CONFIG_BAD_WIFI, bm24_config_validate(&config));
}

static void test_placeholder_and_burn_addresses_are_rejected(void)
{
    bm24_config config = valid_config();
    strcpy(config.worker, "yourBtcAddress");
    TEST_ASSERT_EQUAL(BM24_CONFIG_BAD_WORKER, bm24_config_validate(&config));
    strcpy(config.worker, "1BitcoinEaterAddressDontSendf59kuE");
    TEST_ASSERT_EQUAL(BM24_CONFIG_BAD_WORKER, bm24_config_validate(&config));
}

static void test_pool_host_is_bounded_and_not_a_url(void)
{
    bm24_config config = valid_config();
    strcpy(config.pool_host, "stratum+tcp://public-pool.io");
    TEST_ASSERT_EQUAL(BM24_CONFIG_BAD_POOL, bm24_config_validate(&config));
    strcpy(config.pool_host, "192.168.1.20");
    TEST_ASSERT_EQUAL(BM24_CONFIG_OK, bm24_config_validate(&config));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_deliberately_not_provisioned);
    RUN_TEST(test_valid_config);
    RUN_TEST(test_open_wifi_and_long_wpa_password_are_supported);
    RUN_TEST(test_short_wifi_password_is_rejected);
    RUN_TEST(test_placeholder_and_burn_addresses_are_rejected);
    RUN_TEST(test_pool_host_is_bounded_and_not_a_url);
    return UNITY_END();
}
