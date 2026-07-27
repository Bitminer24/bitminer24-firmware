#ifndef BM24_UI_H
#define BM24_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "bm24_miner.h"
#include "bm24_network.h"
#include "bm24_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double hw_khs;
    double sw_khs;
    float temperature_c;
    uint64_t uptime_seconds;
    bm24_miner_stats miner;
    bm24_pool_stats pool;
    bm24_network_status network;
} bm24_ui_state;

/* Startet den nativen 20-ms-Tastentask und die zeitlich entzerrten
   Informationsabrufe. */
bool bm24_ui_start(void);

/* Baut die aktuell gewaehlte Seite und uebergibt sie dem LCD-Task. */
void bm24_ui_update(const bm24_ui_state *state);

#ifdef __cplusplus
}
#endif

#endif /* BM24_UI_H */
