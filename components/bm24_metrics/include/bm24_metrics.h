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
    /* Pool-seitige Sicht auf die eigene Adresse. In 1.x war das die Zahl,
       an der Besitzer ihr Geraet gemessen haben; sie umfasst auch weitere
       Miner auf derselben Adresse und die beste je erreichte Difficulty,
       die einen Neustart ueberlebt. */
    bool pool_stats_valid;
    uint32_t pool_workers;
    double pool_worker_hash;      /* Summe aller Worker, H/s */
    double pool_best_difficulty;  /* pool-seitige Bestmarke  */
    uint64_t successful_requests;
    uint64_t failed_requests;
} bm24_metrics_snapshot;

/* Hintergrundabrufe fuer die Informationsseiten. Jeder HTTPS-Abruf ist
   zeitlich getrennt und gibt dem SHA-Werk nur fuer den TLS-Aufruf frei. */
bool bm24_metrics_start(void);

/* Pool und Arbeiteradresse fuer die Statistikabfrage setzen. Ohne Aufruf
   bleibt die Pool-Statistik einfach aus. */
void bm24_metrics_set_pool(const char *host, const char *worker);
void bm24_metrics_get(bm24_metrics_snapshot *out);

#ifdef __cplusplus
}
#endif

#endif /* BM24_METRICS_H */
