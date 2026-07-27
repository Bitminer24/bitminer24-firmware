/* Aequivalenz des portierten SW-Kernels gegen die bm24_sha-Referenz.
   Der Kernel stammt aus 1.x und ist auf Geschwindigkeit gebaut; hier wird
   festgenagelt, dass er byte-identisch dasselbe rechnet wie die kompakte,
   gegen NIST und Genesis bewiesene Referenz. */

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "../../components/bm24_sha/bm24_sha.c"
#include "../../components/bm24_sha_sw/bm24_sha_sw.c"

void setUp(void) {}
void tearDown(void) {}

static void build_header(uint8_t header[80], uint32_t seed)
{
    for (int i = 0; i < 80; ++i)
        header[i] = (uint8_t)(seed * 131 + i * 37 + 11);
}

static void set_nonce(uint8_t header[80], uint32_t n)
{
    header[76] = (uint8_t)n;
    header[77] = (uint8_t)(n >> 8);
    header[78] = (uint8_t)(n >> 16);
    header[79] = (uint8_t)(n >> 24);
}

static void test_midstate_equals_reference(void)
{
    uint8_t header[80];
    build_header(header, 1);

    uint32_t ref_state[8];
    bm24_sha_midstate(header, ref_state);

    uint32_t sw_state[8];
    nerd_mids(sw_state, header);

    TEST_ASSERT_EQUAL_MEMORY(ref_state, sw_state, sizeof(ref_state));
}

static void test_sha256d_filter_complete_and_correct(void)
{
    /* nerd_sha256d ist wie die Baked-Variante ein Mining-Filter: er bricht
       nach Runde 60 frueh ab, wenn das Ergebnis sicher keine 16 Null-Endbits
       hat, und laesst doubleHash dann unbefuellt. Deshalb werden hier BEIDE
       Richtungen festgenagelt:
       1. Vollstaendigkeit: hat der Referenz-Hash 16 Null-Endbits, MUSS der
          Filter true liefern — ein verworfener echter Kandidat waere ein
          verlorener Block.
       2. Korrektheit: liefert er true, muss der Hash byte-identisch zur
          Referenz sein. */
    uint8_t header[80], want[32], got[32];
    int candidates = 0;

    for (uint32_t seed = 0; seed < 4; ++seed) {
        build_header(header, seed);

        nerdSHA256_context ctx;
        memcpy(ctx.buffer, header, 64);
        nerd_mids(ctx.digest, header);

        for (uint32_t n = 0; n < 200000; ++n) {
            set_nonce(header, n);
            bm24_double_sha(header, 80, want);
            bool is_candidate = (want[30] == 0 && want[31] == 0);

            bool claimed = nerd_sha256d(&ctx, header + 64, got);
            if (is_candidate) {
                TEST_ASSERT_TRUE_MESSAGE(claimed, "Filter verwarf echten Kandidaten");
                TEST_ASSERT_EQUAL_MEMORY(want, got, 32);
                candidates++;
            } else if (claimed) {
                /* false positive ist erlaubt teuer, aber muss korrekt rechnen */
                TEST_ASSERT_EQUAL_MEMORY(want, got, 32);
            }
        }
    }
    /* 800k Nonces, Erwartungswert ~12 Kandidaten — 0 waere ein kaputter Test */
    TEST_ASSERT_GREATER_THAN(0, candidates);
}

static void test_baked_agrees_when_candidate(void)
{
    /* nerd_sha256d_baked ist ein Filter: liefert er true, muss der Hash
       stimmen. Bei false darf doubleHash undefiniert bleiben — genau die
       1.x-Falle, deshalb wird hier NUR der true-Fall verglichen. */
    uint8_t header[80], want[32], got[32];
    build_header(header, 3);

    uint32_t mid[8], bake[16];
    nerd_mids(mid, header);
    nerd_sha256_bake(mid, header + 64, bake);

    int candidates = 0;
    for (uint32_t n = 0; n < 300000 && candidates < 3; ++n) {
        set_nonce(header, n);
        if (nerd_sha256d_baked(mid, header + 64, bake, got)) {
            bm24_double_sha(header, 80, want);
            TEST_ASSERT_EQUAL_MEMORY(want, got, 32);
            candidates++;
        }
    }
    /* Bei 300k Nonces sind im Erwartungswert ~4-5 Kandidaten (2^-16). */
    TEST_ASSERT_GREATER_THAN(0, candidates);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_midstate_equals_reference);
    RUN_TEST(test_sha256d_filter_complete_and_correct);
    RUN_TEST(test_baked_agrees_when_candidate);
    return UNITY_END();
}
