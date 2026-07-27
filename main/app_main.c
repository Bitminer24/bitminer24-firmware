/* BitMiner24 Firmware 2.0 — Phase-2-Gesamtpruefstand.
   Ein festes mining.notify durchlaeuft IDF-cJSON, Coinbase/Merkle/Target,
   atomaren Jobwechsel, HW+SW-Kernel, Referenzpruefung und Share-Queue.
   Noch ohne WLAN/Pool, damit Leistung und Temperatur reproduzierbar bleiben. */

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
#include "bm24_stratum.h"
#include "bm24_work.h"
#include "bm24_miner.h"

/* Fester mining.notify-Vektor. Er durchlaeuft beim Boot denselben
   cJSON->Stratum->Coinbase->Merkle->Header-Pfad wie spaeter ein Pooljob. */
static const char s_notify_fixture[] =
    "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
    "\"job-7\","
    "\"000102030405060708090a0b0c0d0e0f"
      "101112131415161718191a1b1c1d1e1f\","
    "\"01000000\",\"ffffffff\",["
      "\"00000000000000000000000000000000"
        "00000000000000000000000000000000\","
      "\"ffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffff\"],"
    "\"20000000\",\"1d00ffff\",\"65000000\",true]}";

static uint8_t s_header[80];
static bm24_stratum_message s_fixture_message;

static void stats_task(void *arg);

void app_main(void)
{
    printf("\n=== BitMiner24 2.0 — HW+SW-Pruefstand (IDF 5.5, GCC 14) ===\n");
    printf("ESP-IDF %s\n", esp_get_idf_version());

    if (!bm24_stratum_parse_line(s_notify_fixture, &s_fixture_message) ||
        s_fixture_message.type != BM24_STRATUM_MSG_NOTIFY) {
        printf("SELBSTTEST FEHLGESCHLAGEN: mining.notify unlesbar\n");
        return;
    }
    bm24_work_input work_input;
    bm24_work work;
    bm24_stratum_job_view(&s_fixture_message.job, &work_input);
    bm24_work_status work_status =
        bm24_work_build(&work_input, "a1b2c3d4", 1, 4, &work);
    if (work_status != BM24_WORK_OK) {
        printf("SELBSTTEST FEHLGESCHLAGEN: Jobaufbereitung: %s\n",
               bm24_work_status_string(work_status));
        return;
    }
    memcpy(s_header, work.header, sizeof(s_header));
    printf("Selbsttest: Stratum -> Merkle -> 80-Byte-Header\n");

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
    temperature_sensor_handle_t tsens = NULL;
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    temperature_sensor_install(&tcfg, &tsens);
    temperature_sensor_enable(tsens);

    /* Pruefstand: Watchdog ab hier bewusst aus. Der echte Miner-Kern arbeitet
       in begrenzten Chunks; Produktfirmware aktiviert den Watchdog mit den
       finalen Task-Heartbeats wieder. */
    esp_task_wdt_deinit();

    if (!bm24_miner_start()) {
        printf("SELBSTTEST FEHLGESCHLAGEN: HW-Werk oder Minerstart\n");
        return;
    }
    printf("Selbsttest HW-Werk: 64/64 == Referenz\n");

    bm24_miner_job miner_job = {
        .tag = 1,
        .pool_difficulty = 0.00015
    };
    memcpy(miner_job.header, work.header, sizeof(miner_job.header));
    memcpy(miner_job.network_target_le, work.network_target_le,
           sizeof(miner_job.network_target_le));
    if (!bm24_miner_set_job(&miner_job)) {
        printf("SELBSTTEST FEHLGESCHLAGEN: Jobuebergabe an Miner\n");
        return;
    }
    printf("Selbsttest: atomarer Jobwechsel -> HW/SW-Worker\n");

    xTaskCreatePinnedToCore(stats_task, "stats", 4096, tsens, 5, NULL, 0);
}

static uint8_t smart_sw_duty(float temp, uint8_t current)
{
    /* 300 kH/s moeglichst halten, aber nicht blind durchheizen:
       - ab 63 C SW aus, bis der Chip wieder unter 59 C ist
       - im warmen Bereich nur den langsamen/waermeren SW-Anteil dosieren
       - das effiziente SHA-Werk bleibt unangetastet */
    if (temp >= 63.0f)
        return 0;
    if (current == 0)
        return temp < 59.0f ? 85 : 0;
    if (temp >= 60.0f)
        return 85;
    if (temp >= 58.0f)
        return 90;
    if (temp <= 56.0f)
        return 100;
    return current;
}

static void stats_task(void *arg)
{
    temperature_sensor_handle_t tsens = (temperature_sensor_handle_t)arg;
    uint64_t last_hw = 0, last_sw = 0;
    double best_diff = 0.0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        bm24_miner_stats stats;
        bm24_miner_get_stats(&stats);

        bm24_miner_share share;
        while (bm24_miner_get_share(&share, 0)) {
            if (share.difficulty > best_diff)
                best_diff = share.difficulty;
            if (share.network_block)
                printf("*** BLOCKKANDIDAT job=%" PRIu32 " nonce=%08" PRIx32
                       " ***\n", share.job_tag, share.nonce);
        }

        float temp = 0;
        temperature_sensor_get_celsius(tsens, &temp);

        uint8_t next_duty = smart_sw_duty(temp, stats.sw_duty_percent);
        if (next_duty != stats.sw_duty_percent)
            bm24_miner_set_sw_duty(next_duty);

        uint64_t dhw = stats.hw_hashes - last_hw;
        uint64_t dsw = stats.sw_hashes - last_sw;
        printf("[miner] HW %.1f kH/s, SW %.1f kH/s, gesamt %.1f kH/s, "
               "Duty %u%%, Kandidaten %" PRIu64 "/%" PRIu64
               ", Shares %" PRIu64 ", Best %.6g, Temp %.0f C%s\n",
               dhw / 1000.0, dsw / 1000.0, (dhw + dsw) / 1000.0,
               (unsigned)next_duty, stats.hw_candidates,
               stats.sw_candidates, stats.shares, best_diff, temp,
               stats.mismatches ? " *** MISMATCH ***" : "");
        last_hw = stats.hw_hashes;
        last_sw = stats.sw_hashes;
    }
}
