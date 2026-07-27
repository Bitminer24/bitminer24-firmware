/* Host-Tests fuer bm24_format. Laufen auf dem PC (pio test -e native)
   und in der CI, ohne Hardware. Die Erwartungswerte entsprechen dem
   Verhalten der vermessenen 1.x-Firmware. */

#include <unity.h>
/* Implementierung direkt einziehen: der PlatformIO-Test-Build kompiliert nur
   dieses Verzeichnis, die Komponenten gehoeren dem IDF-CMake. So bleibt die
   Komponente ein reines IDF-Modul und der Test trotzdem selbststaendig. */
#include "../../components/bm24_format/bm24_format.c"

static char buf[32];

void setUp(void) {}
void tearDown(void) {}

static void test_thousands_plain(void)
{
    TEST_ASSERT_EQUAL_STRING("179.009", bm24_thousands(179009, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("959.115", bm24_thousands(959115, buf, sizeof(buf)));
}

static void test_thousands_small_numbers_unchanged(void)
{
    TEST_ASSERT_EQUAL_STRING("0",   bm24_thousands(0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("7",   bm24_thousands(7, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("999", bm24_thousands(999, buf, sizeof(buf)));
}

static void test_thousands_boundaries(void)
{
    TEST_ASSERT_EQUAL_STRING("1.000",     bm24_thousands(1000, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("999.999",   bm24_thousands(999999, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("1.000.000", bm24_thousands(1000000, buf, sizeof(buf)));
}

static void test_thousands_negative(void)
{
    TEST_ASSERT_EQUAL_STRING("-1.234", bm24_thousands(-1234, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("-999",   bm24_thousands(-999, buf, sizeof(buf)));
}

static void test_thousands_rounds_to_integer(void)
{
    TEST_ASSERT_EQUAL_STRING("1.235", bm24_thousands(1234.6, buf, sizeof(buf)));
}

static void test_thousands_tiny_buffer_terminates(void)
{
    char tiny[4];
    bm24_thousands(123456, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL(0, tiny[3] != '\0' ? 1 : 0);
}

static void test_age_just_now(void)
{
    TEST_ASSERT_EQUAL_STRING("gerade eben", bm24_age_text(-5, buf, sizeof(buf)));
}

static void test_age_minutes(void)
{
    TEST_ASSERT_EQUAL_STRING("vor 0 Min.",  bm24_age_text(30, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("vor 12 Min.", bm24_age_text(12 * 60, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("vor 59 Min.", bm24_age_text(3599, buf, sizeof(buf)));
}

static void test_age_hours(void)
{
    TEST_ASSERT_EQUAL_STRING("vor 1 Std.",  bm24_age_text(3600, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("vor 47 Std.", bm24_age_text(47 * 3600 + 59, buf, sizeof(buf)));
}

static void test_age_days(void)
{
    TEST_ASSERT_EQUAL_STRING("vor 2 Tagen",  bm24_age_text(48 * 3600, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("vor 17 Tagen", bm24_age_text(17 * 24 * 3600 + 7200, buf, sizeof(buf)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_thousands_plain);
    RUN_TEST(test_thousands_small_numbers_unchanged);
    RUN_TEST(test_thousands_boundaries);
    RUN_TEST(test_thousands_negative);
    RUN_TEST(test_thousands_rounds_to_integer);
    RUN_TEST(test_thousands_tiny_buffer_terminates);
    RUN_TEST(test_age_just_now);
    RUN_TEST(test_age_minutes);
    RUN_TEST(test_age_hours);
    RUN_TEST(test_age_days);
    return UNITY_END();
}
