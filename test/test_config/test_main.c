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


/* ---------------------------------------------------------------------
   Auswahl der SSID: Liste aus dem Funkscan oder selbst getippt.

   Anlass war ein Kunde, bei dem der Scan mit "Scan fehlgeschlagen"
   abbrach. Weil das Formular damals nur die Auswahlliste kannte, konnte
   er sein WLAN nicht eintragen und die Firmware nicht in Betrieb nehmen.
   Diese Tests halten fest, dass ein fehlgeschlagener Scan niemanden mehr
   aussperrt.
   --------------------------------------------------------------------- */

static void test_selection_from_the_list_is_used(void)
{
    char out[BM24_WIFI_SSID_MAX + 1];
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("Fritzbox 7590", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Fritzbox 7590", out);
}

static void test_typed_ssid_works_when_the_scan_failed(void)
{
    /* Genau der Kundenfall: die Liste liefert nichts Brauchbares. */
    char out[BM24_WIFI_SSID_MAX + 1];
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("", "MeinNetz", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("MeinNetz", out);
}

static void test_typed_ssid_wins_over_the_list(void)
{
    char out[BM24_WIFI_SSID_MAX + 1];
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("Nachbar-WLAN", "MeinNetz", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("MeinNetz", out);
}

static void test_surrounding_spaces_are_removed(void)
{
    /* Beim Abtippen rutscht schnell ein Leerzeichen mit hinein. Auf dem
       Bildschirm sieht man es nicht, die Verbindung scheitert trotzdem. */
    char out[BM24_WIFI_SSID_MAX + 1];
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("", "  MeinNetz  ", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("MeinNetz", out);
}

static void test_spaces_inside_the_name_are_kept(void)
{
    char out[BM24_WIFI_SSID_MAX + 1];
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("", "Zuhause 2 4G", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Zuhause 2 4G", out);
}

static void test_a_field_with_only_spaces_counts_as_empty(void)
{
    char out[BM24_WIFI_SSID_MAX + 1];
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("Fritzbox", "   ", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Fritzbox", out);
}

static void test_both_empty_is_rejected(void)
{
    char out[BM24_WIFI_SSID_MAX + 1];
    TEST_ASSERT_FALSE(bm24_config_pick_ssid("", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(bm24_config_pick_ssid(NULL, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_too_long_is_rejected_instead_of_truncated(void)
{
    /* Eine stillschweigend gekuerzte SSID verbindet nie und der Anwender
       sieht nicht, warum. Lieber ablehnen. */
    char out[BM24_WIFI_SSID_MAX + 1];
    char long_name[BM24_WIFI_SSID_MAX + 10];
    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    TEST_ASSERT_FALSE(bm24_config_pick_ssid("", long_name, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_the_longest_allowed_ssid_still_fits(void)
{
    char out[BM24_WIFI_SSID_MAX + 1];
    char max_name[BM24_WIFI_SSID_MAX + 1];
    memset(max_name, 'B', BM24_WIFI_SSID_MAX);
    max_name[BM24_WIFI_SSID_MAX] = '\0';
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("", max_name, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(max_name, out);
}

static void test_picked_ssid_passes_validation(void)
{
    /* Der ganze Weg: getippt, uebernommen, und die Konfiguration gilt. */
    bm24_config config = valid_config();
    TEST_ASSERT_TRUE(bm24_config_pick_ssid("", " Gaeste-WLAN ",
                                           config.wifi_ssid,
                                           sizeof(config.wifi_ssid)));
    TEST_ASSERT_EQUAL_STRING("Gaeste-WLAN", config.wifi_ssid);
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
    RUN_TEST(test_selection_from_the_list_is_used);
    RUN_TEST(test_typed_ssid_works_when_the_scan_failed);
    RUN_TEST(test_typed_ssid_wins_over_the_list);
    RUN_TEST(test_surrounding_spaces_are_removed);
    RUN_TEST(test_spaces_inside_the_name_are_kept);
    RUN_TEST(test_a_field_with_only_spaces_counts_as_empty);
    RUN_TEST(test_both_empty_is_rejected);
    RUN_TEST(test_too_long_is_rejected_instead_of_truncated);
    RUN_TEST(test_the_longest_allowed_ssid_still_fits);
    RUN_TEST(test_picked_ssid_passes_validation);
    return UNITY_END();
}
