/* BitMiner24 2.0 — optimierter Software-SHA256d-Kernel.
   Portiert aus 1.x (src/ShaTests/nerdSHA256plus.cpp, Basis Blockstream
   Jade / @BitMaker). Voll ausgerollte Runden; auf dem Geraet in IRAM,
   auf dem Host identisch kompilierbar fuer die Aequivalenz-Tests gegen
   die bm24_sha-Referenz. */

#ifndef BM24_SHA_SW_H
#define BM24_SHA_SW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nerdSHA256_context {
    uint8_t  buffer[64];
    uint32_t digest[8];
} nerdSHA256_context;

/* Midstate nach dem ersten 64-Byte-Block des Headers. */
void nerd_mids(uint32_t *digest, const uint8_t *dataIn);

/* Doppel-SHA ab Midstate; dataIn zeigt auf Header-Byte 64..79.
   ACHTUNG: Mining-Filter wie die Baked-Variante — bricht nach Runde 60
   frueh ab und laesst doubleHash unbefuellt, wenn das Ergebnis sicher
   keine 16 Null-Endbits hat. true == Kandidat, Hash vollstaendig. Der
   Host-Test test_sha_sw beweist, dass der Filter nie einen echten
   Kandidaten verwirft. */
bool nerd_sha256d(nerdSHA256_context *midstate, const uint8_t *dataIn, uint8_t *doubleHash);

/* Vorberechnung konstanter W-Erweiterungen des zweiten Blocks (15 Woerter). */
void nerd_sha256_bake(const uint32_t *digest, const uint8_t *dataIn, uint32_t *bake);

/* Wie nerd_sha256d, nutzt die gebackenen Woerter. ACHTUNG: Mining-Filter —
   bricht frueh ab und laesst doubleHash unangetastet, wenn das Ergebnis
   sicher kein Kandidat ist. Nie als Referenz fuer Vergleiche benutzen
   (genau dieser Fehler hat in 1.x den Selbsttest sabotiert). */
bool nerd_sha256d_baked(const uint32_t *digest, const uint8_t *dataIn, const uint32_t *bake, uint8_t *doubleHash);

void ByteReverseWords(uint32_t *out, const uint32_t *in, uint32_t byteCount);

#ifdef __cplusplus
}
#endif

#endif /* BM24_SHA_SW_H */
