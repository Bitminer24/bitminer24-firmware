#ifndef BM24_SHA_H
#define BM24_SHA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Portabler SHA-256-Referenzpfad. Bewusst kompakt und lesbar: er ist der
   Massstab, gegen den jeder schnelle Pfad (Hardware-Werk, unrolled SW)
   verifiziert wird — auf dem Host in der CI und auf dem Geraet im
   Boot-Selbsttest. Geschwindigkeit ist hier ausdruecklich kein Ziel. */

void bm24_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void bm24_double_sha(const uint8_t *data, size_t len, uint8_t out[32]);

/* Mining-Zerlegung eines 80-Byte-Headers:
   Block 1 (Byte 0..63) ist je Job konstant -> Midstate einmal rechnen.
   Block 2 (Byte 64..79) = 12 Byte Rest + 4 Byte Nonce + Padding. */

/* Zustand nach dem ersten 64-Byte-Block. */
void bm24_sha_midstate(const uint8_t header64[64], uint32_t state[8]);

/* Doppel-SHA ab Midstate: tail12 sind Header-Bytes 64..75, die Nonce wird
   little-endian eingesetzt (Bitcoin-Serialisierung). Ergebnis identisch zu
   bm24_double_sha ueber den kompletten gepatchten Header — das ist genau
   die Eigenschaft, die die Host-Tests festnageln. */
void bm24_double_sha_from_midstate(const uint32_t state[8],
                                   const uint8_t tail12[12],
                                   uint32_t nonce,
                                   uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif /* BM24_SHA_H */
