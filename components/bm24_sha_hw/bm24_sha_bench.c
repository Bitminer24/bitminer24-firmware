/* Zeitlich begrenzter Versuch: lohnt der DMA-Modus des SHA-Werks?

   Unsere Kette je Hash ist: Midstate laden, Block 2 des Headers rechnen,
   Digest lesen, daraus den zweiten SHA rechnen. Der Digest dazwischen
   verhindert, dass DMA seine eigentliche Staerke ausspielt, naemlich viele
   Bloecke am Stueck. Bleibt die Frage, ob eine DMA-Uebertragung von
   64 Byte billiger ist als sechzehn Registerschreibvorgaenge.

   Ergebnis wird auf Serial gemeldet und in MESSUNGEN.md festgehalten.
   Aktivieren mit -D BM24_BENCH_DMA=1. */

#include "bm24_sha_hw.h"

#if BM24_BENCH_DMA

#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "soc/hwcrypto_reg.h"
#include "hal/sha_ll.h"
#include "sha/sha_core.h"
#include "xtensa/hal.h"

#include "bm24_sha.h"

#define ROUNDS 2000

static const char *TAG = "bm24_bench";

static inline uint32_t cycles(void) { return (uint32_t)xthal_get_ccount(); }

void bm24_sha_bench_dma(void)
{
    uint8_t header[80];
    for (int i = 0; i < 80; ++i)
        header[i] = (uint8_t)(i * 37 + 11);

    uint32_t mid[8];
    bm24_sha_midstate(header, mid);
    uint32_t mid_regs[8];
    for (int i = 0; i < 8; ++i)
        mid_regs[i] = __builtin_bswap32(mid[i]);

    uint8_t block2[64] = {0};
    memcpy(block2, header + 64, 16);
    block2[16] = 0x80;
    block2[62] = 0x02; block2[63] = 0x80;

    uint8_t inner[64] = {0};
    inner[32] = 0x80;
    inner[62] = 0x01;

    esp_sha_acquire_hardware();

    /* --- Weg 1: Register, so wie der Miner heute arbeitet --- */
    esp_sha_set_mode(SHA2_256);
    uint32_t t0 = cycles();
    for (int r = 0; r < ROUNDS; ++r) {
        uint32_t *h = (uint32_t *)SHA_H_BASE;
        uint32_t *t = (uint32_t *)SHA_TEXT_BASE;
        for (int i = 0; i < 8; ++i) REG_WRITE(&h[i], mid_regs[i]);
        for (int i = 0; i < 16; ++i) REG_WRITE(&t[i], ((uint32_t *)block2)[i]);
        REG_WRITE(SHA_CONTINUE_REG, 1);
        sha_ll_load(SHA2_256);
        while (REG_READ(SHA_BUSY_REG)) {}
        for (int i = 0; i < 8; ++i) ((uint32_t *)inner)[i] = REG_READ(&h[i]);
        for (int i = 0; i < 16; ++i) REG_WRITE(&t[i], ((uint32_t *)inner)[i]);
        REG_WRITE(SHA_START_REG, 1);
        sha_ll_load(SHA2_256);
        while (REG_READ(SHA_BUSY_REG)) {}
    }
    uint32_t reg_cycles = cycles() - t0;

    /* --- Weg 2: dieselbe Kette ueber die DMA-Schnittstelle --- */
    esp_sha_set_mode(SHA2_256);
    uint32_t t1 = cycles();
    for (int r = 0; r < ROUNDS; ++r) {
        uint32_t state[8];
        memcpy(state, mid, sizeof(state));
        esp_sha_write_digest_state(SHA2_256, state);
        esp_sha_dma(SHA2_256, block2, 64, NULL, 0, false);
        esp_sha_read_digest_state(SHA2_256, state);
        for (int i = 0; i < 8; ++i)
            ((uint32_t *)inner)[i] = __builtin_bswap32(state[i]);
        esp_sha_dma(SHA2_256, inner, 64, NULL, 0, true);
        esp_sha_read_digest_state(SHA2_256, state);
    }
    uint32_t dma_cycles = cycles() - t1;

    esp_sha_release_hardware();

    ESP_LOGW(TAG, "Register: %" PRIu32 " Takte/Hash -> %.1f kH/s",
             reg_cycles / ROUNDS, 240000.0f / (reg_cycles / (float)ROUNDS));
    ESP_LOGW(TAG, "DMA:      %" PRIu32 " Takte/Hash -> %.1f kH/s",
             dma_cycles / ROUNDS, 240000.0f / (dma_cycles / (float)ROUNDS));
}

#endif /* BM24_BENCH_DMA */
