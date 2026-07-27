/* BitMiner24 Firmware 2.0
   Reiner ESP-IDF-5.5-Produktfad: NVS -> WLAN/Setup -> Stratum -> native
   Jobaufbereitung -> verifizierte HW/SW-Kernel -> Share-Submit. */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "bm24_config.h"
#include "bm24_display.h"
#include "bm24_metrics.h"
#include "bm24_miner.h"
#include "bm24_network.h"
#include "bm24_pool.h"
#include "bm24_ui.h"

static const char *TAG = "bm24";
static temperature_sensor_handle_t s_temperature;
static bool s_display_ready;

static bool running_image_pending_verify(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return running &&
           esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

static void fatal_boot(const char *reason, bool rollback)
{
    ESP_LOGE(TAG, "FATAL: %s", reason);
    if (s_display_ready) {
        bm24_display_frame frame = {0};
        strlcpy(frame.line[0], "FEHLER", sizeof(frame.line[0]));
        strlcpy(frame.line[1], reason, sizeof(frame.line[1]));
        strlcpy(frame.line[3], rollback ? "OTA ROLLBACK" : "USB FLASH",
                sizeof(frame.line[3]));
        bm24_display_set(&frame);
    }
    if (rollback && running_image_pending_verify()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

static uint8_t smart_sw_duty(float temperature, uint8_t current)
{
    /* Das effiziente SHA-Werk bleibt bei 240 MHz. Geregelt wird nur der
       langsamere und thermisch teurere Softwareanteil, mit Hysterese. */
    if (temperature >= 63.0f)
        return 0;
    if (current == 0)
        return temperature < 59.0f ? 85 : 0;
    if (temperature >= 60.0f)
        return 85;
    if (temperature >= 58.0f)
        return 90;
    if (temperature <= 56.0f)
        return 100;
    return current;
}

/* Letzte Sekundenwerte fuer das Web-Dashboard. Sie werden im Supervisor
   ohnehin berechnet; hier landen sie in einer kleinen Ablage, damit der
   Webserver sie ohne Zugriff auf Miner und Pool ausliefern kann. */
static struct {
    double khs;
    float temperature;
    uint64_t uptime;
} s_live;

/* Zaehler ueber Neustarts hinweg. Wird beim Start geladen, alle fuenf
   Minuten gesichert — oft genug, um bei einem Stromausfall wenig zu
   verlieren, selten genug, um den Flash nicht zu verschleissen. */
static bm24_runtime_stats s_persisted;
static uint64_t s_accepted_base;   /* Shares aus frueheren Sitzungen */

static void dashboard_status(char *json, size_t capacity)
{
    bm24_miner_stats miner;
    bm24_pool_stats pool;
    bm24_metrics_snapshot metrics;
    bm24_miner_get_stats(&miner);
    bm24_pool_get_stats(&pool);
    bm24_metrics_get(&metrics);

    double best = pool.best_difficulty;
    if (metrics.pool_stats_valid && metrics.pool_best_difficulty > best)
        best = metrics.pool_best_difficulty;

    uint64_t up = s_persisted.total_seconds;   /* ueber alle Starts */
    char uptime[24];
    snprintf(uptime, sizeof(uptime), "%ud %02uh %02um",
             (unsigned)(up / 86400), (unsigned)((up / 3600) % 24),
             (unsigned)((up / 60) % 60));

    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(json, capacity,
             "{\"khs\":%.1f,\"temp\":%.1f,\"accepted\":%" PRIu64
             ",\"submitted\":%" PRIu64 ",\"best\":%.8f,\"pool\":%s"
             ",\"uptime\":\"%s\",\"workers\":%u,\"version\":\"%s\"}",
             s_live.khs, s_live.temperature, pool.accepted, pool.submitted,
             best, pool.connected ? "true" : "false", uptime,
             (unsigned)(metrics.pool_stats_valid ? metrics.pool_workers : 1),
             app->version);
}

static void supervisor_task(void *arg)
{
    (void)arg;
    bool watchdog_added = esp_task_wdt_add(NULL) == ESP_OK;

    uint32_t last_generation = 0;
    uint64_t last_hw = 0, last_sw = 0;
    uint32_t stalled_seconds = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (watchdog_added)
            esp_task_wdt_reset();

        bm24_miner_stats miner;
        bm24_pool_stats pool;
        bm24_network_status network;
        bm24_miner_get_stats(&miner);
        bm24_pool_get_stats(&pool);
        bm24_network_get_status(&network);

        uint64_t hw_rate = 0, sw_rate = 0;
        if (miner.generation == last_generation) {
            hw_rate = miner.hw_hashes - last_hw;
            sw_rate = miner.sw_hashes - last_sw;
        }
        last_generation = miner.generation;
        last_hw = miner.hw_hashes;
        last_sw = miner.sw_hashes;

        float temperature = 0.0f;
        if (s_temperature &&
            temperature_sensor_get_celsius(s_temperature, &temperature) ==
                ESP_OK) {
            uint8_t duty = smart_sw_duty(
                temperature, miner.sw_duty_percent);
            if (duty != miner.sw_duty_percent)
                bm24_miner_set_sw_duty(duty);
        } else {
            /* Ohne Temperaturmessung ist nur das kuehlere HW-Werk erlaubt. */
            bm24_miner_set_sw_duty(0);
        }

        /* Freien Speicher im Auge behalten. Ein langsames Leck faellt sonst
           erst auf, wenn ein TLS-Aufbau scheitert oder das Geraet abstuerzt.
           Gemeldet wird nur bei neuem Tiefstand unterhalb der Schwelle,
           damit das Protokoll nicht zulaeuft. */
        static uint32_t heap_low = UINT32_MAX;
        uint32_t heap_free = (uint32_t)esp_get_free_heap_size();
        if (heap_free < 60000 && heap_free < heap_low) {
            heap_low = heap_free;
            ESP_LOGW(TAG, "Wenig freier Speicher: %" PRIu32 " Byte", heap_free);
        }

        /* Job-Stillstand erkennen: der Pool-Task darf lange blockierend
           warten, aber wenn eine bestehende Verbindung ueber zehn Minuten
           keinen neuen Job mehr liefert, ist sie tot und nur der Socket
           weiss es noch nicht. Neu verbinden statt still weiterrechnen. */
        static uint64_t last_jobs = 0;
        static uint32_t job_idle_seconds = 0;
        if (pool.connected && pool.jobs == last_jobs) {
            if (++job_idle_seconds >= 600) {
                job_idle_seconds = 0;
                ESP_LOGW(TAG, "Zehn Minuten ohne neuen Job, Pool neu verbinden");
                bm24_pool_reconnect();
            }
        } else {
            job_idle_seconds = 0;
            last_jobs = pool.jobs;
        }

        if (miner.active && miner.hw_trusted && hw_rate == 0) {
            if (++stalled_seconds >= 20) {
                ESP_LOGE(TAG, "HW-Worker 20 s ohne Fortschritt, Neustart");
                esp_restart();
            }
        } else {
            stalled_seconds = 0;
        }
        if (miner.mismatches) {
            ESP_LOGE(TAG, "SHA-Mismatch erkannt, fail closed");
            bm24_miner_clear_job();
        }

        /* Gesamtlaufzeit und Bestmarke fortschreiben und gelegentlich
           sichern; sie sollen einen Stromausfall ueberstehen. */
        s_persisted.total_seconds++;
        /* Die Pool-Zaehler starten bei jedem Boot bei null; addiert wird
           deshalb der Stand dieser Sitzung auf den gemerkten Sockel. */
        s_persisted.accepted = s_accepted_base + pool.accepted;
        if (pool.best_difficulty > s_persisted.best_difficulty)
            s_persisted.best_difficulty = pool.best_difficulty;
        static uint32_t save_countdown = 300;
        if (--save_countdown == 0) {
            save_countdown = 300;
            bm24_stats_save(&s_persisted);
        }

        s_live.khs = (hw_rate + sw_rate) / 1000.0;
        s_live.temperature = temperature;
        s_live.uptime = (uint64_t)(esp_timer_get_time() / 1000000);

        bm24_ui_state ui = {
            .hw_khs = hw_rate / 1000.0,
            .sw_khs = sw_rate / 1000.0,
            .temperature_c = temperature,
            .uptime_seconds = s_persisted.total_seconds,
            .miner = miner,
            .pool = pool,
            .network = network
        };
        bm24_ui_update(&ui);

        ESP_LOGI(TAG,
                 "%.1f kH/s (HW %.1f, SW %.1f), %.1f C, Duty %u%%, "
                 "WLAN %s, Pool %s, Shares %" PRIu64 "/%" PRIu64
                 ", Fehler %" PRIu64,
                 (hw_rate + sw_rate) / 1000.0, hw_rate / 1000.0,
                 sw_rate / 1000.0, temperature,
                 (unsigned)miner.sw_duty_percent,
                 network.connected ? network.ip :
                     (network.portal_active ? "SETUP" : "offline"),
                 pool.connected ? "online" : pool.last_error,
                 pool.accepted, pool.submitted, miner.mismatches);
    }
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    bool pending_verify = running_image_pending_verify();
    printf("\n=== BitMiner24 %s | ESP-IDF %s ===\n",
           app->version, esp_get_idf_version());

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK)
        fatal_boot("NVS INIT", pending_verify);

    /* Der Aufbau steht jetzt in app_main, bevor irgendein Task startet.
       Vorher richtete ihn der Supervisor selbst ein, und der Metrik-Task
       war schneller — sein esp_task_wdt_add lief ins Leere. */
    esp_task_wdt_config_t watchdog = {
        /* 12 s waren zu knapp, seit auch Pool- und Metrik-Task ueberwacht
           werden: ein einzelner HTTPS-Abruf darf allein 7 s dauern, dazu
           kommt der TLS-Aufbau. 40 s faengt echte Haenger immer noch,
           loest aber bei langsamem Netz keinen Fehlalarm aus. */
        .timeout_ms = 40000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&watchdog);

    s_display_ready = bm24_display_start();
    if (!s_display_ready)
        fatal_boot("DISPLAY INIT", pending_verify);

    temperature_sensor_config_t temp_config =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    if (temperature_sensor_install(&temp_config, &s_temperature) != ESP_OK ||
        temperature_sensor_enable(s_temperature) != ESP_OK) {
        s_temperature = NULL;
        ESP_LOGE(TAG, "Temperatursensor fehlt; SW-Kern bleibt aus");
    }

    if (!bm24_miner_start())
        fatal_boot("SHA SELBSTTEST", pending_verify);
    ESP_LOGI(TAG, "SHA-Hardware-Selbsttest 64/64 bestanden");

    bm24_config config;
    bm24_config_status config_status = bm24_config_load(&config);
    bool provisioned = config_status == BM24_CONFIG_OK;
    if (!provisioned) {
        ESP_LOGW(TAG, "Konfiguration: %s; Setup wird gestartet",
                 bm24_config_status_string(config_status));
        bm24_config_defaults(&config);
    }

    bool connected = bm24_network_start(
        provisioned ? &config : NULL, provisioned ? 20000 : 0);
    bm24_network_status network;
    bm24_network_get_status(&network);
    if (!connected && !network.portal_active)
        fatal_boot("WLAN/SETUP INIT", pending_verify);
    bm24_network_set_status_provider(dashboard_status);
    if (!bm24_ui_start())
        fatal_boot("UI TASK", pending_verify);

    /* Der Image-Checkpoint umfasst NVS, LCD-Treiber, SHA-Selbsttest und
       einen funktionsfaehigen WLAN- oder Setup-Pfad. Erst jetzt ist ein
       OTA-Image gemaess IDF-Rollbackvertrag gueltig. */
    if (pending_verify) {
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
            fatal_boot("OTA VALIDIERUNG", true);
        ESP_LOGI(TAG, "OTA-Image nach Selbsttests als gueltig markiert");
    }

    /* Pool-seitige Statistik braucht Host und Adresse; sie zeigt auch
       weitere Miner auf derselben Adresse und die Bestmarke des Pools. */
    bm24_stats_load(&s_persisted);
    s_persisted.restarts++;
    s_accepted_base = s_persisted.accepted;
    ESP_LOGI(TAG, "Start %u, Gesamtlaufzeit bisher %" PRIu64 " s",
             (unsigned)s_persisted.restarts, s_persisted.total_seconds);

    bm24_metrics_set_pool(config.pool_host, config.worker);
    bm24_metrics_set_timezone(config.timezone_offset);
    bm24_display_apply_settings(config.brightness, config.invert_colors);

    if (provisioned && !bm24_pool_start(&config))
        fatal_boot("POOL TASK", false);

    if (xTaskCreatePinnedToCore(supervisor_task, "bm24Supervisor", 6144,
                                NULL, 5, NULL, 0) != pdPASS)
        fatal_boot("SUPERVISOR TASK", false);
}
