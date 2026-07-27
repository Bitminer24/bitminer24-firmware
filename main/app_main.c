/* BitMiner24 Firmware 2.0
   Reiner ESP-IDF-5.5-Produktfad: NVS -> WLAN/Setup -> Stratum -> native
   Jobaufbereitung -> verifizierte HW/SW-Kernel -> Share-Submit. */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "bm24_config.h"
#include "bm24_display.h"
#include "bm24_miner.h"
#include "bm24_network.h"
#include "bm24_pool.h"

#define BUTTON_SETUP GPIO_NUM_14

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

static void supervisor_task(void *arg)
{
    (void)arg;
    esp_task_wdt_config_t watchdog = {
        .timeout_ms = 12000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    bool watchdog_added =
        esp_task_wdt_init(&watchdog) == ESP_OK &&
        esp_task_wdt_add(NULL) == ESP_OK;

    gpio_config_t button = {
        .pin_bit_mask = 1ULL << BUTTON_SETUP,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&button);

    uint32_t last_generation = 0;
    uint64_t last_hw = 0, last_sw = 0;
    uint32_t stalled_seconds = 0;
    uint32_t setup_hold = 0;
    bool setup_opened = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (watchdog_added)
            esp_task_wdt_reset();

        if (gpio_get_level(BUTTON_SETUP) == 0) {
            if (setup_hold < 5)
                ++setup_hold;
            if (setup_hold >= 4 && !setup_opened) {
                setup_opened = bm24_network_open_portal();
                if (setup_opened)
                    ESP_LOGW(TAG, "Setup-Portal per Taste geoeffnet");
            }
        } else {
            setup_hold = 0;
        }

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

        if (s_display_ready) {
            if (network.portal_active) {
                bm24_display_setup(network.setup_ssid,
                                   BM24_SETUP_PASSWORD);
            } else {
                bm24_display_frame frame = {0};
                strlcpy(frame.line[0], "BITMINER24",
                        sizeof(frame.line[0]));
                snprintf(frame.line[1], sizeof(frame.line[1]),
                         "%.1f KH/S  %.0f C",
                         (hw_rate + sw_rate) / 1000.0, temperature);
                snprintf(frame.line[2], sizeof(frame.line[2]),
                         "HW %.1f  SW %.1f",
                         hw_rate / 1000.0, sw_rate / 1000.0);
                snprintf(frame.line[3], sizeof(frame.line[3]),
                         "POOL %s  WIFI %d",
                         pool.connected ? "ONLINE" : "WARTET",
                         (int)network.rssi);
                snprintf(frame.line[4], sizeof(frame.line[4]),
                         "SHARES %" PRIu64 "/%" PRIu64,
                         pool.accepted, pool.submitted);
                snprintf(frame.line[5], sizeof(frame.line[5]),
                         "IDF %s | DUTY %u%% | JOB %" PRIu32,
                         esp_get_idf_version(),
                         (unsigned)miner.sw_duty_percent,
                         pool.active_job_tag);
                bm24_display_set(&frame);
            }
        }

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

    s_display_ready = bm24_display_start();
    if (!s_display_ready)
        ESP_LOGE(TAG, "Display-Initialisierung fehlgeschlagen");

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

    /* Der Image-Checkpoint umfasst NVS, LCD-Treiber, SHA-Selbsttest und
       einen funktionsfaehigen WLAN- oder Setup-Pfad. Erst jetzt ist ein
       OTA-Image gemaess IDF-Rollbackvertrag gueltig. */
    if (pending_verify) {
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
            fatal_boot("OTA VALIDIERUNG", true);
        ESP_LOGI(TAG, "OTA-Image nach Selbsttests als gueltig markiert");
    }

    if (provisioned && !bm24_pool_start(&config))
        fatal_boot("POOL TASK", false);

    if (xTaskCreatePinnedToCore(supervisor_task, "bm24Supervisor", 6144,
                                NULL, 5, NULL, 0) != pdPASS)
        fatal_boot("SUPERVISOR TASK", false);
}
