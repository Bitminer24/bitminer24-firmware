#ifndef BM24_POOL_H
#define BM24_POOL_H

#include <stdbool.h>
#include <stdint.h>

#include "bm24_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;
    bool subscribed;
    bool authorized;
    double difficulty;
    uint32_t active_job_tag;
    uint64_t jobs;
    uint64_t submitted;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t stale;
    uint64_t reconnects;
    uint64_t protocol_errors;
    double best_difficulty;
    char last_error[96];
} bm24_pool_stats;

/* Startet genau einen Pool-Task. Wi-Fi und bm24_miner muessen vorher
   initialisiert sein. Der Task reconnectet mit begrenztem Backoff. */
bool bm24_pool_start(const bm24_config *config);
void bm24_pool_get_stats(bm24_pool_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* BM24_POOL_H */
