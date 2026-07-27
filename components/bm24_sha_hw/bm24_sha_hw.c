#include "bm24_sha_hw.h"

#include <string.h>

#include "esp_attr.h"
#include "soc/hwcrypto_reg.h"   /* S3: SHA-Register liegen hier, nicht in sha_reg.h */
#include "soc/dport_access.h"
#include "hal/sha_ll.h"
#include "sha/sha_core.h"          /* esp_sha_acquire/release_hardware */

#include "bm24_sha.h"              /* host-bewiesene Referenz */

#ifndef BM24_SHA_LOCK_CHUNK
#define BM24_SHA_LOCK_CHUNK 8192
#endif

/* ---- Registerpfad, 1:1 die in 1.x vermessene Fassung ------------------- */






/* liest den Digest nur, wenn die letzten 16 Bit null sind (Kandidat) */

/* ---- ein Hash: Midstate laden, Block 2, dann Hash-vom-Hash ------------- */

/* Ein Hash ueber die dokumentierte SHA-API von IDF 5.5.
   Der rohe Registerpfad aus 1.x (SHA_CONTINUE_REG/SHA_START_REG direkt)
   blieb hier nach dem ersten Abschnitt stehen: seit 5.5 setzen die
   SHA-Funktionen den Modus nicht mehr implizit, und AES/SHA/MPI teilen
   sich Steuerregister, deren Zugriff jetzt gekapselt ist. Siehe
   Migration Guide 5.5 (Security) und espressif/esp-idf@7761b0f. */
static bool IRAM_ATTR hw_one_hash(const uint32_t mid[8],
                                  const uint8_t block2[64],
                                  uint32_t out[8])
{
    uint32_t state[8];
    memcpy(state, mid, sizeof(state));

    esp_sha_set_mode(SHA2_256);
    esp_sha_write_digest_state(SHA2_256, state);
    esp_sha_block(SHA2_256, block2, false);
    esp_sha_read_digest_state(SHA2_256, state);

    /* zweiter SHA ueber die 32 Ergebnisbytes: ein voller Block */
    uint8_t inner[64] = {0};
    for (int i = 0; i < 8; ++i) {
        inner[i * 4 + 0] = (uint8_t)(state[i] >> 24);
        inner[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        inner[i * 4 + 2] = (uint8_t)(state[i] >> 8);
        inner[i * 4 + 3] = (uint8_t)state[i];
    }
    inner[32] = 0x80;
    inner[62] = 0x01;   /* Laenge 256 Bit */

    esp_sha_set_mode(SHA2_256);
    esp_sha_block(SHA2_256, inner, true);
    esp_sha_read_digest_state(SHA2_256, out);

    return ((out[7] & 0xFFFF0000u) == 0);
}


/* Digest-Woerter (big-endian Werte) in Byte-Reihenfolge der Referenz */
static void digest_words_to_bytes(const uint32_t w[8], uint8_t out[32])
{
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = (uint8_t)(w[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(w[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(w[i] >> 8);
        out[i * 4 + 3] = (uint8_t)w[i];
    }
}

bm24_hw_result bm24_sha_hw_scan(const uint8_t header80[80],
                                uint32_t nonce_start, uint32_t count)
{
    bm24_hw_result r = {0};

    uint32_t mid[8], hw[8];
    uint8_t  patched[80], want[32], got[32];
    uint8_t  block2[64] = {0};

    /* Treffer werden WAEHREND des Chunks nur gemerkt, nie sofort geprueft.
       Die Sperre auf das SHA-Werk mittendrin freizugeben und neu zu nehmen
       liess den Miner nach dem ersten Durchlauf stehen; die Nachrechnung
       gehoert hinter das Freigeben. Bei 2^-16 Trefferquote passen 16
       Plaetze bequem auf einen 8192er-Chunk. */
    uint32_t hit_nonce[16];
    uint32_t hit_words[16][8];

    bm24_sha_midstate(header80, mid);
    memcpy(patched, header80, 80);
    memcpy(block2, header80 + 64, 12);   /* Rest des Headers */
    block2[16] = 0x80;                   /* Padding fuer 80 Byte */
    block2[62] = 0x02; block2[63] = 0x80;/* Laenge 640 Bit */

    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > BM24_SHA_LOCK_CHUNK) chunk = BM24_SHA_LOCK_CHUNK;

        int hits = 0;
        /* Diagnose: Sperre nur EINMAL nehmen. IDF 5.5 setzt den Baustein
           beim Freigeben zurueck (sha_ll_reset_register in
           esp_sha_acquire_hardware), was den Registerpfad aus 1.x nach dem
           ersten Abschnitt stehen liess. */
        esp_sha_acquire_hardware();

        for (uint32_t i = 0; i < chunk; ++i) {
            uint32_t nonce = nonce_start + done + i;
            block2[12] = (uint8_t)nonce;
            block2[13] = (uint8_t)(nonce >> 8);
            block2[14] = (uint8_t)(nonce >> 16);
            block2[15] = (uint8_t)(nonce >> 24);
            if (hw_one_hash(mid, block2, hw) && hits < 16) {
                hit_nonce[hits] = nonce;
                memcpy(hit_words[hits], hw, sizeof(hw));
                hits++;
            }
        }
        esp_sha_release_hardware();

        for (int k = 0; k < hits; ++k) {
            digest_words_to_bytes(hit_words[k], got);
            uint32_t n = hit_nonce[k];
            patched[76] = (uint8_t)n;
            patched[77] = (uint8_t)(n >> 8);
            patched[78] = (uint8_t)(n >> 16);
            patched[79] = (uint8_t)(n >> 24);
            bm24_double_sha(patched, 80, want);
            if (memcmp(want, got, 32) == 0) r.candidates++;
            else                            r.mismatches++;
        }

        done += chunk;
    }
    r.hashes = done;
    return r;
}

bool bm24_sha_hw_selftest(int n)
{
    uint8_t header[80], want[32], got[32], block2[64];
    uint32_t mid[8], hw[8];

    for (int v = 0; v < n; ++v) {
        for (int i = 0; i < 80; ++i)
            header[i] = (uint8_t)(v * 131 + i * 37 + 11);

        bm24_sha_midstate(header, mid);
        memset(block2, 0, sizeof(block2));
        memcpy(block2, header + 64, 16);   /* inkl. Nonce aus dem Header */
        block2[16] = 0x80;
        block2[62] = 0x02; block2[63] = 0x80;

        esp_sha_acquire_hardware();
        hw_one_hash(mid, block2, hw);      /* Rueckgabe egal, out ist immer voll */
        esp_sha_release_hardware();

        digest_words_to_bytes(hw, got);
        bm24_double_sha(header, 80, want);
        if (memcmp(want, got, 32) != 0)
            return false;
    }
    return true;
}
