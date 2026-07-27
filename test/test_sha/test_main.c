/* Host-Tests fuer bm24_sha: NIST-Vektoren, Bitcoin-Genesis-Block und die
   Kern-Eigenschaft des Miners — Midstate-Pfad == kompletter Header-Hash
   fuer beliebige Nonces. */

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "../../components/bm24_sha/bm24_sha.c"

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

static void assert_hash_hex(const char *expect_hex, const uint8_t got[32])
{
    uint8_t expect[32];
    hex_to_bytes(expect_hex, expect, 32);
    TEST_ASSERT_EQUAL_MEMORY(expect, got, 32);
}

/* --- NIST FIPS 180-4 Vektoren --- */

static void test_sha256_abc(void)
{
    uint8_t out[32];
    bm24_sha256((const uint8_t *)"abc", 3, out);
    assert_hash_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", out);
}

static void test_sha256_empty(void)
{
    uint8_t out[32];
    bm24_sha256((const uint8_t *)"", 0, out);
    assert_hash_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", out);
}

static void test_sha256_two_block_message(void)
{
    /* 56 Byte -> Padding erzwingt einen zweiten Block */
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t out[32];
    bm24_sha256((const uint8_t *)m, strlen(m), out);
    assert_hash_hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", out);
}

/* --- Bitcoin Genesis-Block: 80-Byte-Header, bekanntes Ergebnis --- */

static const char *GENESIS_HEX =
    "0100000000000000000000000000000000000000000000000000000000000000"
    "000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa"
    "4b1e5e4a29ab5f49ffff001d1dac2b7c";

static void test_double_sha_genesis(void)
{
    uint8_t header[80], out[32];
    hex_to_bytes(GENESIS_HEX, header, 80);
    bm24_double_sha(header, 80, out);
    /* interne Byte-Reihenfolge; rueckwaerts gelesen ergibt das die bekannte
       000000000019d668... Block-Hash-Darstellung */
    assert_hash_hex("6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000", out);
}

/* --- Kern-Eigenschaft: Midstate-Pfad identisch zum vollen Hash --- */

static void test_midstate_matches_full_genesis(void)
{
    uint8_t header[80], full[32], mid[32];
    hex_to_bytes(GENESIS_HEX, header, 80);

    uint32_t state[8];
    bm24_sha_midstate(header, state);

    uint32_t nonce = (uint32_t)header[76] | ((uint32_t)header[77] << 8) |
                     ((uint32_t)header[78] << 16) | ((uint32_t)header[79] << 24);

    bm24_double_sha(header, 80, full);
    bm24_double_sha_from_midstate(state, header + 64, nonce, mid);
    TEST_ASSERT_EQUAL_MEMORY(full, mid, 32);
}

static void test_midstate_matches_full_many_nonces(void)
{
    uint8_t header[80], full[32], mid[32];
    hex_to_bytes(GENESIS_HEX, header, 80);

    /* Header variieren, damit nicht nur der Genesis-Sonderfall abgedeckt ist */
    for (int i = 0; i < 80; ++i) header[i] ^= (uint8_t)(i * 37 + 11);

    uint32_t state[8];
    bm24_sha_midstate(header, state);

    const uint32_t nonces[] = { 0, 1, 0x7fffffff, 0x80000000, 0xdeadbeef, 0xffffffff };
    for (size_t i = 0; i < sizeof(nonces) / sizeof(nonces[0]); ++i) {
        uint32_t n = nonces[i];
        header[76] = (uint8_t)n;
        header[77] = (uint8_t)(n >> 8);
        header[78] = (uint8_t)(n >> 16);
        header[79] = (uint8_t)(n >> 24);

        bm24_double_sha(header, 80, full);
        bm24_double_sha_from_midstate(state, header + 64, n, mid);
        TEST_ASSERT_EQUAL_MEMORY(full, mid, 32);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_empty);
    RUN_TEST(test_sha256_two_block_message);
    RUN_TEST(test_double_sha_genesis);
    RUN_TEST(test_midstate_matches_full_genesis);
    RUN_TEST(test_midstate_matches_full_many_nonces);
    return UNITY_END();
}
