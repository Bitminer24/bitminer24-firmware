/* BitMiner24 Firmware 2.0 — Phase-2-Pruefstand.
   Misst den Software-SHA256d-Kernel (aus 1.x portiert, host-verifiziert)
   unter GCC 14 auf beiden Kernen: Hashes pro Sekunde je Kern plus
   Chip-Temperatur, jede Sekunde seriell. Kein WLAN, kein Pool — reine,
   reproduzierbare Kernleistung. Referenzwert 1.x (GCC 8.4, Kern 1,
   Prioritaet 1 neben allem anderen): ~40 kH/s. */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_idf_version.h"
#include "driver/temperature_sensor.h"
#include "esp_task_wdt.h"

#include "bm24_sha.h"
#include "bm24_sha_sw.h"
#include "bm24_sha_hw.h"

/* fester Testheader; Inhalt beliebig, nur reproduzierbar muss er sein */
static uint8_t s_header[80];

static volatile uint32_t s_hashes[2];
static volatile uint32_t s_candidates[2];

static void stats_task(void *arg);

/* Kern 0: Hardware-SHA-Werk in Chunks; jeder Kandidat referenz-verifiziert */
static void bench_hw_task(void *arg)
{
    (void)arg;
    uint32_t nonce = 0;
    for (;;) {
        bm24_hw_result r = bm24_sha_hw_scan(s_header, nonce, 8192);
        s_hashes[0] += r.hashes;
        s_candidates[0] += r.candidates;
        if (r.mismatches) {
            printf("*** HW-MISMATCH: %u Abweichungen — Abbruch ***\n",
                   (unsigned)r.mismatches);
            vTaskDelete(NULL);
        }
        nonce += 8192;
    }
}

static void bench_task(void *arg)
{
    const int core = (int)(intptr_t)arg;

    uint32_t mid[8], bake[16];
    uint8_t  tail[16], hash[32];
    memcpy(tail, s_header + 64, 16);
    nerd_mids(mid, s_header);
    nerd_sha256_bake(mid, tail, bake);

    uint32_t nonce = (core == 0) ? 0 : 0x80000000u;
    for (;;) {
        ((uint32_t *)(tail + 12))[0] = nonce;
        if (nerd_sha256d_baked(mid, tail, bake, hash))
            s_candidates[core]++;
        s_hashes[core]++;
        nonce++;
    }
}

void app_main(void)
{
    printf("\n=== BitMiner24 2.0 — HW+SW-Pruefstand (IDF 5.5, GCC 14) ===\n");
    printf("ESP-IDF %s\n", esp_get_idf_version());

    for (int i = 0; i < 80; ++i)
        s_header[i] = (uint8_t)(i * 37 + 11);

    /* Selbsttest vor jeder Messung: der schnelle Kernel muss der Referenz
       entsprechen, sonst ist jede Zahl wertlos. Gleiche Disziplin wie der
       Boot-Selbsttest in 1.x. */
    {
        uint32_t mid_sw[8], mid_ref[8];
        nerd_mids(mid_sw, s_header);
        bm24_sha_midstate(s_header, mid_ref);
        if (memcmp(mid_sw, mid_ref, sizeof(mid_sw)) != 0) {
            printf("SELBSTTEST FEHLGESCHLAGEN: Midstate weicht ab — Abbruch\n");
            return;
        }
        printf("Selbsttest: Midstate SW == Referenz\n");
    }
    const bool hw_ok = bm24_sha_hw_selftest(64);
    printf("Selbsttest HW-Werk: %s\n",
           hw_ok ? "64/64 == Referenz" : "FEHLGESCHLAGEN");
    if (!hw_ok) {
        printf("HW-Pruefstand bleibt aus: keine Messung mit falschen Hashes.\n");
        return;
    }

    temperature_sensor_handle_t tsens = NULL;
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    temperature_sensor_install(&tcfg, &tsens);
    temperature_sensor_enable(tsens);

    /* Pruefstand: Watchdog ab hier bewusst aus, die Bench-Tasks pausieren
       nie. Nur die Statistik-Task wird hier erstellt — sie startet die
       Bench-Tasks selbst. Wuerde app_main (Prioritaet 1) bench0
       (Prioritaet 2, gleicher Kern) direkt starten, waere es ab diesem
       Moment verdraengt und kaeme nie zum zweiten xTaskCreate. */
    esp_task_wdt_deinit();
    xTaskCreatePinnedToCore(stats_task, "stats", 4096, tsens, 5, NULL, 0);
}

static void stats_task(void *arg)
{
    temperature_sensor_handle_t tsens = (temperature_sensor_handle_t)arg;
    xTaskCreatePinnedToCore(bench_hw_task, "benchHw", 8192, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(bench_task,    "benchSw", 8192, (void *)1, 2, NULL, 1);
    uint32_t last0 = 0, last1 = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t h0 = s_hashes[0], h1 = s_hashes[1];
        float temp = 0;
        temperature_sensor_get_celsius(tsens, &temp);
        printf("[bench] Kern0 %.1f kH/s, Kern1 %.1f kH/s, gesamt %.1f kH/s, "
               "Kandidaten %" PRIu32 "/%" PRIu32 ", Temp %.0f C\n",
               (h0 - last0) / 1000.0f, (h1 - last1) / 1000.0f,
               ((h0 - last0) + (h1 - last1)) / 1000.0f,
               s_candidates[0], s_candidates[1], temp);
        last0 = h0; last1 = h1;
    }
}
