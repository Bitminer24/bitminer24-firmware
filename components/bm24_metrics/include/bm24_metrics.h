#ifndef BM24_METRICS_H
#define BM24_METRICS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool time_synced;
    bool price_valid;
    bool chain_valid;
    bool solo_valid;
    uint32_t block_height;
    uint32_t half_hour_fee;
    uint32_t halving_blocks;
    uint32_t retarget_blocks;
    uint32_t solo_last_height;
    uint32_t solo_total_blocks;
    uint32_t solo_blocks_this_year;
    uint32_t solo_avg_days;
    int64_t solo_last_timestamp;
    double btc_usd;
    double global_hash_eh;
    double network_difficulty_t;
    double retarget_change;
    double jackpot_eur;
    uint64_t successful_requests;
    uint64_t failed_requests;
} bm24_metrics_snapshot;

/* Hintergrundabrufe fuer die Informationsseiten. Jeder HTTPS-Abruf ist
   zeitlich getrennt und gibt dem SHA-Werk nur fuer den TLS-Aufruf frei. */
bool bm24_metrics_start(void);
void bm24_metrics_get(bm24_metrics_snapshot *out);

#ifdef __cplusplus
}
#endif

#endif /* BM24_METRICS_H */
