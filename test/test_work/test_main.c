/* Host-Tests fuer die native Stratum-Jobaufbereitung. Die Erwartungswerte
   wurden unabhaengig mit Python hashlib erzeugt, nicht mit bm24_sha selbst. */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "../../components/bm24_sha/bm24_sha.c"
#include "../../components/bm24_work/bm24_work.c"

void setUp(void) {}
void tearDown(void) {}

static void hex_to_bytes(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static bm24_work_input fixture(void)
{
    static const char *branches[] = {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    };
    static const char prev[] =
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f";

    bm24_work_input in = {
        .version_hex = "20000000",
        .prev_hash_hex = prev,
        .coinbase1_hex = "01000000",
        .coinbase2_hex = "ffffffff",
        .ntime_hex = "65000000",
        .nbits_hex = "1d00ffff",
        .merkle_count = 2
    };
    in.merkle_hex[0] = branches[0];
    in.merkle_hex[1] = branches[1];
    return in;
}

static void test_compact_diff1_target(void)
{
    uint8_t got[32] = {0}, want[32] = {0};
    want[26] = 0xff;
    want[27] = 0xff;
    TEST_ASSERT_EQUAL(BM24_WORK_OK,
                      bm24_compact_target("1d00ffff", got));
    TEST_ASSERT_EQUAL_MEMORY(want, got, 32);
}

static void test_compact_target_rejects_negative_and_zero(void)
{
    uint8_t target[32];
    TEST_ASSERT_EQUAL(BM24_WORK_BAD_COMPACT_TARGET,
                      bm24_compact_target("1d80ffff", target));
    TEST_ASSERT_EQUAL(BM24_WORK_BAD_COMPACT_TARGET,
                      bm24_compact_target("00000001", target));
}

static void test_build_known_work(void)
{
    static const char expected_header[] =
        "00000020"
        "03020100070605040b0a09080f0e0d0c"
        "13121110171615141b1a19181f1e1d1c"
        "93e3361350009fca625f9c22d89bb9e4"
        "d005ec33ecf9f12b3f6a73589b14e86a"
        "00000065ffff001d00000000";
    static const char expected_merkle[] =
        "93e3361350009fca625f9c22d89bb9e4"
        "d005ec33ecf9f12b3f6a73589b14e86a";

    bm24_work_input in = fixture();
    bm24_work work;
    TEST_ASSERT_EQUAL(BM24_WORK_OK,
                      bm24_work_build(&in, "a1b2c3d4", 1, 4, &work));

    uint8_t want_header[80], want_merkle[32];
    hex_to_bytes(expected_header, want_header, 80);
    hex_to_bytes(expected_merkle, want_merkle, 32);
    TEST_ASSERT_EQUAL_STRING("00000001", work.extranonce2_hex);
    TEST_ASSERT_EQUAL_MEMORY(want_header, work.header, 80);
    TEST_ASSERT_EQUAL_MEMORY(want_merkle, work.merkle_root, 32);

    uint32_t mid[8];
    bm24_sha_midstate(work.header, mid);
    TEST_ASSERT_EQUAL_MEMORY(mid, work.midstate, sizeof(mid));
}

static void test_build_rejects_invalid_hex(void)
{
    bm24_work_input in = fixture();
    bm24_work work;
    in.prev_hash_hex = "xyz";
    TEST_ASSERT_EQUAL(BM24_WORK_BAD_HEX,
                      bm24_work_build(&in, "a1b2c3d4", 1, 4, &work));
}

static void test_build_rejects_extranonce_overflow(void)
{
    bm24_work_input in = fixture();
    bm24_work work;
    TEST_ASSERT_EQUAL(BM24_WORK_BAD_EXTRANONCE2,
                      bm24_work_build(&in, "a1b2c3d4", 0x10000, 2, &work));
}

static void test_genesis_hash_meets_diff1_target(void)
{
    uint8_t hash[32], target[32], bad[32];
    hex_to_bytes(
        "6fe28c0ab6f1b372c1a6a246ae63f74f"
        "931e8365e15a089c68d6190000000000",
        hash, 32);
    memset(bad, 0xff, sizeof(bad));

    TEST_ASSERT_EQUAL(BM24_WORK_OK,
                      bm24_compact_target("1d00ffff", target));
    TEST_ASSERT_TRUE(bm24_hash_meets_target(hash, target));
    TEST_ASSERT_FALSE(bm24_hash_meets_target(bad, target));
}

static void test_genesis_hash_difficulty(void)
{
    uint8_t hash[32];
    hex_to_bytes(
        "6fe28c0ab6f1b372c1a6a246ae63f74f"
        "931e8365e15a089c68d6190000000000",
        hash, 32);
    double got = bm24_hash_difficulty(hash);
    TEST_ASSERT_TRUE(fabs(got - 2536.426) < 0.01);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_compact_diff1_target);
    RUN_TEST(test_compact_target_rejects_negative_and_zero);
    RUN_TEST(test_build_known_work);
    RUN_TEST(test_build_rejects_invalid_hex);
    RUN_TEST(test_build_rejects_extranonce_overflow);
    RUN_TEST(test_genesis_hash_meets_diff1_target);
    RUN_TEST(test_genesis_hash_difficulty);
    return UNITY_END();
}
