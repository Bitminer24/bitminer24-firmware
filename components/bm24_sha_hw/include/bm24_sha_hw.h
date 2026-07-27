/* BitMiner24 2.0 — Hardware-SHA-Miner (ESP32-S3 SHA-Werk, Registerpfad).
   Portiert aus 1.x mit allen dort vermessenen Erkenntnissen:
   - Midstate + zweiter Block per Register, ~980 Takte/Hash
   - ZERO_TEXT_ONCE: Nullwoerter [9..14] nur einmal je Sperr-Abschnitt
   - Sperre alle BM24_SHA_LOCK_CHUNK Nonces kurz freigeben (TLS teilt sich
     das Werk; in 1.x fuehrte Dauerhalten zu 20-37 s Handshakes)
   - schlichtes Busy-Poll; kalibriertes Warten war messbar langsamer
   Geraete-only: auf dem Host existiert das SHA-Werk nicht. */

#ifndef BM24_SHA_HW_H
#define BM24_SHA_HW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t hashes;        /* verarbeitete Nonces                       */
    uint32_t candidates;    /* 16 Null-Endbits, per Referenz nachgeprueft */
    uint32_t mismatches;    /* HW != Referenz — muss immer 0 sein         */
} bm24_hw_result;

/* Boot-Selbsttest: n Zufallsheader komplett gegen die bm24_sha-Referenz.
   false => HW-Pfad nicht benutzen. Gleiche Disziplin wie 1.x (64/64). */
bool bm24_sha_hw_selftest(int n);

/* count Nonces ab nonce_start auf header80 rechnen. Jeder Kandidat wird
   vor dem Zaehlen per Software-Referenz nachgerechnet (Stufe-3-Disziplin
   aus 1.x: nichts verlaesst den Miner unverifiziert). */
bm24_hw_result bm24_sha_hw_scan(const uint8_t header80[80],
                                uint32_t nonce_start, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* BM24_SHA_HW_H */
