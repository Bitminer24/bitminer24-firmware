#include <string.h>
#include <unity.h>

#include "../../components/bm24_stratum/bm24_stratum_wire.c"

void setUp(void) {}
void tearDown(void) {}

static void test_subscribe(void)
{
    char out[160];
    size_t n = bm24_stratum_write_subscribe(
        out, sizeof(out), 1, "BitMiner24/2.0");
    TEST_ASSERT_EQUAL(strlen(out), n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":1,\"method\":\"mining.subscribe\","
        "\"params\":[\"BitMiner24/2.0\"]}\n", out);
}

static void test_authorize_escapes_json(void)
{
    char out[192];
    TEST_ASSERT_NOT_EQUAL(
        0, bm24_stratum_write_authorize(
               out, sizeof(out), 2, "bc1q\"worker", "p\\ass\n"));
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":2,\"method\":\"mining.authorize\","
        "\"params\":[\"bc1q\\\"worker\",\"p\\\\ass\\n\"]}\n", out);
}

static void test_suggest_difficulty(void)
{
    char out[160];
    TEST_ASSERT_NOT_EQUAL(
        0, bm24_stratum_write_suggest_difficulty(
               out, sizeof(out), 3, 0.00015));
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":3,\"method\":\"mining.suggest_difficulty\","
        "\"params\":[0.00015]}\n", out);
    TEST_ASSERT_EQUAL(
        0, bm24_stratum_write_suggest_difficulty(
               out, sizeof(out), 3, 0.0));
}

static void test_submit_nonce_is_zero_padded(void)
{
    char out[256];
    TEST_ASSERT_NOT_EQUAL(
        0, bm24_stratum_write_submit(
               out, sizeof(out), 4, "bc1q.worker", "job-7",
               "00000001", "65000000", 0x1234));
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":4,\"method\":\"mining.submit\","
        "\"params\":[\"bc1q.worker\",\"job-7\",\"00000001\","
        "\"65000000\",\"00001234\"]}\n", out);
}

static void test_small_buffer_fails_closed(void)
{
    char out[12] = "unchanged";
    TEST_ASSERT_EQUAL(
        0, bm24_stratum_write_subscribe(
               out, sizeof(out), 1, "BitMiner24/2.0"));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_job_view_has_no_copy_or_allocation(void)
{
    bm24_stratum_job job = {0};
    strcpy(job.version_hex, "20000000");
    strcpy(job.prev_hash_hex,
           "000102030405060708090a0b0c0d0e0f"
           "101112131415161718191a1b1c1d1e1f");
    strcpy(job.coinbase1_hex, "01");
    strcpy(job.coinbase2_hex, "02");
    strcpy(job.ntime_hex, "65000000");
    strcpy(job.nbits_hex, "1d00ffff");
    strcpy(job.merkle_hex[0],
           "00000000000000000000000000000000"
           "00000000000000000000000000000000");
    job.merkle_count = 1;

    bm24_work_input view;
    bm24_stratum_job_view(&job, &view);
    TEST_ASSERT_EQUAL_PTR(job.version_hex, view.version_hex);
    TEST_ASSERT_EQUAL_PTR(job.merkle_hex[0], view.merkle_hex[0]);
    TEST_ASSERT_EQUAL(1, view.merkle_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_subscribe);
    RUN_TEST(test_authorize_escapes_json);
    RUN_TEST(test_suggest_difficulty);
    RUN_TEST(test_submit_nonce_is_zero_padded);
    RUN_TEST(test_small_buffer_fails_closed);
    RUN_TEST(test_job_view_has_no_copy_or_allocation);
    return UNITY_END();
}
