#include "bm24_sha_hw.h"

#include <string.h>

#include "esp_attr.h"
#include "soc/hwcrypto_reg.h"      /* S3: SHA-Register liegen hier */
#include "hal/sha_ll.h"
#include "sha/sha_core.h"          /* esp_sha_acquire/release_hardware */

#include "bm24_sha.h"              /* host-bewiesene Referenz */

#ifndef BM24_SHA_LOCK_CHUNK
#define BM24_SHA_LOCK_CHUNK 8192
#endif

/* ---------------------------------------------------------------------------
   Registerpfad, uebernommen aus der vermessenen 1.8.3-bm1 (~980 Takte/Hash).

   BYTE-REIHENFOLGE, die Falle: Die Textregister nehmen die Nachrichtenwoerter
   BYTEVERDREHT entgegen, die H-Register dagegen normal. Man sieht es an den
   Konstanten: das Padding-Byte 0x80 steht als 0x00000080 statt 0x80000000,
   die Laenge 640 Bit (0x280) als 0x80020000. Ein erster Portierungsversuch
   hat die Header-Woerter big-endian konvertiert — das Ergebnis wich von der
   Referenz ab, und der Selbstschutz hat den Pfad korrekt abgeschaltet.
   Deshalb hier: Woerter roh uebernehmen, nicht konvertieren.

   IDF 5.5: Der Modus wird nach jedem Nehmen der Sperre neu gesetzt. Die
   SHA-Funktionen tun das seit 5.5 nicht mehr implizit, und Takt- und
   Reset-Register werden von AES, SHA und MPI geteilt.
   --------------------------------------------------------------------------- */

static inline void ll_zero_const_text(void)
{
    uint32_t *t = (uint32_t *)SHA_TEXT_BASE;
    for (int i = 9; i <= 14; ++i)
        REG_WRITE(&t[i], 0);
}

/* Block 2 des Headers: 12 Byte Rest + Nonce + Padding + Laenge */
static inline void ll_fill_block1(const uint32_t tail3[3], uint32_t nonce)
{
    uint32_t *t = (uint32_t *)SHA_TEXT_BASE;
    REG_WRITE(&t[0], tail3[0]);
    REG_WRITE(&t[1], tail3[1]);
    REG_WRITE(&t[2], tail3[2]);
    REG_WRITE(&t[3], nonce);
    REG_WRITE(&t[4], 0x00000080);   /* 0x80-Padding, byteverdreht */
    REG_WRITE(&t[5], 0);
    REG_WRITE(&t[6], 0);
    REG_WRITE(&t[7], 0);
    REG_WRITE(&t[8], 0);
    /* [9..14] bleiben null, einmal je Sperr-Abschnitt geschrieben */
    REG_WRITE(&t[15], 0x80020000);  /* 640 Bit, byteverdreht */
}

/* zweiter SHA: das Ergebnis des ersten wird zur Nachricht */
static inline void ll_fill_block2_from_digest(void)
{
    uint32_t *t = (uint32_t *)SHA_TEXT_BASE;
    uint32_t *h = (uint32_t *)SHA_H_BASE;
    for (int i = 0; i < 8; ++i)
        REG_WRITE(&t[i], REG_READ(&h[i]));
    REG_WRITE(&t[8], 0x00000080);
    REG_WRITE(&t[15], 0x00010000);  /* 256 Bit, byteverdreht */
}

static inline void ll_write_digest(const uint32_t d[8])
{
    uint32_t *h = (uint32_t *)SHA_H_BASE;
    for (int i = 0; i < 8; ++i)
        REG_WRITE(&h[i], d[i]);
}

static inline void ll_wait_idle(void)
{
    while (REG_READ(SHA_BUSY_REG)) {}
}

static inline void ll_read_digest(uint32_t out[8])
{
    uint32_t *h = (uint32_t *)SHA_H_BASE;
    for (int i = 0; i < 8; ++i)
        out[i] = REG_READ(&h[i]);
}

/* true, wenn die letzten 16 Bit null sind; liest dann den vollen Digest */
static inline bool ll_read_digest_if(uint32_t out[8])
{
    uint32_t *h = (uint32_t *)SHA_H_BASE;
    uint32_t last = REG_READ(&h[7]);
    if ((last & 0xFFFF0000u) != 0)
        return false;
    out[7] = last;
    for (int i = 0; i < 7; ++i)
        out[i] = REG_READ(&h[i]);
    return true;
}

static inline void IRAM_ATTR hw_hash_core(const uint32_t mid[8],
                                          const uint32_t tail3[3],
                                          uint32_t nonce)
{
    ll_write_digest(mid);
    ll_fill_block1(tail3, nonce);
    REG_WRITE(SHA_CONTINUE_REG, 1);
    sha_ll_load(SHA2_256);
    ll_wait_idle();

    ll_fill_block2_from_digest();
    REG_WRITE(SHA_START_REG, 1);
    sha_ll_load(SHA2_256);
    ll_wait_idle();
}

/* Sperre nehmen und den Baustein in den Zustand bringen, den der Hot-Loop
   erwartet. Seit IDF 5.5 gehoert das Setzen des Modus zwingend hierher. */
static inline void hw_begin(void)
{
    esp_sha_acquire_hardware();
    REG_WRITE(SHA_MODE_REG, SHA2_256);
    ll_zero_const_text();
}

/* Die H-Register liefern jedes SHA-Wort byteverdreht als CPU-u32. Das ist
   kein zusaetzliches Ausgabeformat: im little-endian Speicher stehen damit
   bereits exakt die 32 Digest-Bytes in Netzwerkreihenfolge. */
static void digest_regs_to_bytes(const uint32_t w[8], uint8_t out[32])
{
    memcpy(out, w, 32);
}

/* bm24_sha_midstate() liefert die FIPS-Werte als normale CPU-u32. Das
   SHA-Werk erwartet in H dagegen dasselbe Registerlayout, das es beim Lesen
   liefert: jedes Wort byteverdreht. Einmal pro Job vorbereiten; niemals im
   Nonce-Hot-Loop drehen. */
static void midstate_to_digest_regs(const uint32_t state[8], uint32_t regs[8])
{
    for (int i = 0; i < 8; ++i)
        regs[i] = __builtin_bswap32(state[i]);
}

/* Header-Bytes 64..75 als rohe Woerter, ohne Konvertierung (siehe oben) */
static void header_tail3(const uint8_t header80[80], uint32_t tail3[3])
{
    memcpy(tail3, header80 + 64, 12);
}

bm24_hw_result bm24_sha_hw_scan_candidates(
    const uint8_t header80[80], uint32_t nonce_start, uint32_t count,
    bm24_hw_candidate_cb callback, void *context)
{
    bm24_hw_result r = {0};

    uint32_t mid[8], mid_regs[8], tail3[3], hw[8];
    uint8_t  patched[80], want[32], got[32];

    /* Treffer werden waehrend des Abschnitts nur gemerkt und erst nach dem
       Freigeben der Sperre nachgerechnet. Die Sperre mittendrin abzugeben
       ist unnoetig und war fehleranfaellig. */
    uint32_t hit_nonce[16];
    uint32_t hit_words[16][8];

    bm24_sha_midstate(header80, mid);
    midstate_to_digest_regs(mid, mid_regs);
    header_tail3(header80, tail3);
    memcpy(patched, header80, 80);

    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > BM24_SHA_LOCK_CHUNK) chunk = BM24_SHA_LOCK_CHUNK;

        int hits = 0;
        hw_begin();
        for (uint32_t i = 0; i < chunk; ++i) {
            uint32_t nonce = nonce_start + done + i;
            hw_hash_core(mid_regs, tail3, nonce);
            if (ll_read_digest_if(hw) && hits < 16) {
                hit_nonce[hits] = nonce;
                memcpy(hit_words[hits], hw, sizeof(hw));
                hits++;
            }
        }
        esp_sha_release_hardware();

        for (int k = 0; k < hits; ++k) {
            digest_regs_to_bytes(hit_words[k], got);
            uint32_t n = hit_nonce[k];
            patched[76] = (uint8_t)n;
            patched[77] = (uint8_t)(n >> 8);
            patched[78] = (uint8_t)(n >> 16);
            patched[79] = (uint8_t)(n >> 24);
            bm24_double_sha(patched, 80, want);
            if (memcmp(want, got, 32) == 0) {
                r.candidates++;
                if (callback)
                    callback(n, got, context);
            } else {
                r.mismatches++;
            }
        }

        done += chunk;
    }
    r.hashes = done;
    return r;
}

bm24_hw_result bm24_sha_hw_scan(const uint8_t header80[80],
                                uint32_t nonce_start, uint32_t count)
{
    return bm24_sha_hw_scan_candidates(header80, nonce_start, count,
                                       NULL, NULL);
}

bool bm24_sha_hw_selftest(int n)
{
    uint8_t header[80], want[32], got[32];
    uint32_t mid[8], mid_regs[8], tail3[3], hw[8];

    for (int v = 0; v < n; ++v) {
        for (int i = 0; i < 80; ++i)
            header[i] = (uint8_t)(v * 131 + i * 37 + 11);

        bm24_sha_midstate(header, mid);
        midstate_to_digest_regs(mid, mid_regs);
        header_tail3(header, tail3);
        uint32_t nonce;
        memcpy(&nonce, header + 76, 4);

        hw_begin();
        hw_hash_core(mid_regs, tail3, nonce);
        /* IMMER den vollen Digest lesen — nie die Filterfunktion als
           Referenz nehmen (die 1.x-Falle, die dort den Selbsttest
           sabotiert hat). */
        ll_read_digest(hw);
        esp_sha_release_hardware();

        digest_regs_to_bytes(hw, got);
        bm24_double_sha(header, 80, want);
        if (memcmp(want, got, 32) != 0)
            return false;
    }
    return true;
}
