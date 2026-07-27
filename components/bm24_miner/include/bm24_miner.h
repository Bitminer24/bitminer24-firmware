#ifndef BM24_MINER_H
#define BM24_MINER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BM24_MINER_SOURCE_HW = 0,
    BM24_MINER_SOURCE_SW = 1
} bm24_miner_source;

typedef struct {
    uint32_t tag;                  /* vom Stratum-Task vergebene Jobkennung */
    uint8_t header[80];
    uint8_t network_target_le[32];
    double pool_difficulty;
} bm24_miner_job;

typedef struct {
    uint32_t job_tag;
    uint32_t nonce;
    uint8_t hash[32];
    double difficulty;
    bool network_block;
    bm24_miner_source source;
} bm24_miner_share;

typedef struct {
    uint32_t generation;
    bool active;
    bool hw_trusted;
    uint8_t sw_duty_percent;
    uint64_t hw_hashes;
    uint64_t sw_hashes;
    uint64_t total_hw_hashes;
    uint64_t total_sw_hashes;
    uint64_t hw_candidates;
    uint64_t sw_candidates;
    uint64_t mismatches;
    uint64_t shares;
    uint64_t dropped_shares;
} bm24_miner_stats;

/* Startet je einen Worker auf Kern 0 (HW) und Kern 1 (SW). Vorher wird das
   SHA-Werk erneut mit 64 Vektoren geprueft. */
bool bm24_miner_start(void);

/* Atomarer Jobwechsel zwischen den Nonce-Chunks. */
bool bm24_miner_set_job(const bm24_miner_job *job);
void bm24_miner_clear_job(void);

/* Nichtblockierend bei timeout_ms=0. */
bool bm24_miner_get_share(bm24_miner_share *out, uint32_t timeout_ms);
void bm24_miner_get_stats(bm24_miner_stats *out);

/* 0 stoppt nur den SW-Kern, 100 ist volle Leistung. Das HW-Werk bleibt an. */
void bm24_miner_set_sw_duty(uint8_t percent);

/* Gibt TLS/anderen IDF-Nutzern des SHA-Werks Vorrang. Ein vergessenes
   true laeuft nach 20 Sekunden automatisch aus. */
void bm24_miner_set_network_window(bool active);

#ifdef __cplusplus
}
#endif

#endif /* BM24_MINER_H */
