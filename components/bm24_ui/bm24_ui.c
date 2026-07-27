#include "bm24_ui.h"

#include "esp_system.h"

#include "bm24_config.h"
#include "bm24_media.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "bm24_display.h"
#include "bm24_format.h"
#include "bm24_metrics.h"

#define BUTTON_DISPLAY GPIO_NUM_0
#define BUTTON_PAGE    GPIO_NUM_14
#define PAGE_COUNT     5u
#define DEBOUNCE_TICKS 3u
#define DOUBLE_MS      400u
#define LONG_MS        4000u
/* Noch laenger gehalten setzt das Geraet auf Werkszustand zurueck. In 1.x
   lag das auf demselben Knopf; ohne diesen Weg kommt niemand mehr sauber
   von einem alten WLAN oder einer fremden Adresse los. Die Anzeige warnt
   ab vier Sekunden, damit es nicht versehentlich passiert. */
#define RESET_MS       10000u

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    uint8_t stable_ticks;
    uint32_t pressed_at;
    bool long_fired;
} button_state;

static const char *TAG = "bm24_ui";
static TaskHandle_t s_button_task;
static volatile uint8_t s_page;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void format_rate(double khs, char *out, size_t capacity)
{
    if (khs >= 1000.0)
        snprintf(out, capacity, "%.2f MH/S", khs / 1000.0);
    else
        snprintf(out, capacity, "%.1f KH/S", khs);
}

static void format_difficulty(double difficulty, char *out, size_t capacity)
{
    if (!(difficulty > 0.0))
        strlcpy(out, "-", capacity);
    else if (difficulty >= 1000000.0)
        snprintf(out, capacity, "%.2f M", difficulty / 1000000.0);
    else if (difficulty >= 1000.0)
        snprintf(out, capacity, "%.2f K", difficulty / 1000.0);
    else if (difficulty >= 1.0)
        snprintf(out, capacity, "%.3f", difficulty);
    else
        snprintf(out, capacity, "%.6f", difficulty);
}

static void format_uptime(uint64_t seconds, char *out, size_t capacity)
{
    uint64_t hours = seconds / 3600u;
    snprintf(out, capacity, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
             hours, (seconds / 60u) % 60u, seconds % 60u);
}

static void format_percent(double value, char *out, size_t capacity)
{
    if (value > 999.99)
        value = 999.99;
    if (value < -999.99)
        value = -999.99;
    int hundredths = (int)(value * 100.0 +
        (value >= 0.0 ? 0.5 : -0.5));
    unsigned magnitude = (unsigned)(hundredths < 0
        ? -hundredths : hundredths);
    snprintf(out, capacity, "%c%u.%02u%%",
             hundredths < 0 ? '-' : '+',
             magnitude / 100u, magnitude % 100u);
}

static void format_clock(bool synced, char *out, size_t capacity)
{
    time_t now;
    time(&now);
    struct tm local;
    if (!synced || now < 1704067200 || !localtime_r(&now, &local)) {
        strlcpy(out, "--:--", capacity);
        return;
    }
    strftime(out, capacity, "%H:%M", &local);
}

/* Koordinaten aus der vermessenen 1.x-Anzeige. Die Grafiken tragen ihre
   Beschriftungen im Bild, hier stehen nur noch die Werte an der Stelle,
   an der im Bild ihr Kaestchen ist. x ist bei right = true die rechte
   Kante des Kaestchens. */
static void miner_page(const bm24_ui_state *state,
                       bm24_display_frame *frame)
{
    frame->background = bm24_img_miner;
    frame->slot_count = 6;

    /* grosse Hashrate links, darunter die Gesamtzahl */
    frame->slot[0] = (bm24_display_slot){126, 84, 3, true};
    format_rate(state->hw_khs + state->sw_khs, frame->line[0],
                sizeof(frame->line[0]));

    frame->slot[1] = (bm24_display_slot){150, 141, 2, true};
    snprintf(frame->line[1], sizeof(frame->line[1]), "%.1f C",
             state->temperature_c);

    /* rechte Spalte: gleiche Reihenfolge wie in 1.x */
    frame->slot[2] = (bm24_display_slot){313, 33, 2, true};
    snprintf(frame->line[2], sizeof(frame->line[2]), "%" PRIu32,
             state->pool.active_job_tag);

    bm24_metrics_snapshot metrics;
    bm24_metrics_get(&metrics);
    double best = state->pool.best_difficulty;
    if (metrics.pool_stats_valid && metrics.pool_best_difficulty > best)
        best = metrics.pool_best_difficulty;

    frame->slot[3] = (bm24_display_slot){313, 78, 2, true};
    format_difficulty(best, frame->line[3], sizeof(frame->line[3]));

    frame->slot[4] = (bm24_display_slot){313, 93, 2, true};
    snprintf(frame->line[4], sizeof(frame->line[4]), "%" PRIu64,
             state->pool.accepted);

    frame->slot[5] = (bm24_display_slot){313, 123, 2, true};
    format_uptime(state->uptime_seconds, frame->line[5],
                  sizeof(frame->line[5]));
}

static void clock_page(const bm24_ui_state *state,
                       const bm24_metrics_snapshot *metrics,
                       bm24_display_frame *frame)
{
    frame->background = bm24_img_clock;
    frame->slot_count = 4;

    frame->slot[0] = (bm24_display_slot){140, 80, 3, true};
    format_clock(metrics->time_synced, frame->line[0],
                 sizeof(frame->line[0]));

    frame->slot[1] = (bm24_display_slot){306, 68, 2, true};
    if (metrics->chain_valid)
        snprintf(frame->line[1], sizeof(frame->line[1]), "%" PRIu32,
                 metrics->block_height);
    else
        strlcpy(frame->line[1], "-", sizeof(frame->line[1]));

    frame->slot[2] = (bm24_display_slot){306, 142, 2, true};
    if (metrics->price_valid)
        snprintf(frame->line[2], sizeof(frame->line[2]), "%.0f",
                 metrics->btc_usd);
    else
        strlcpy(frame->line[2], "-", sizeof(frame->line[2]));

    frame->slot[3] = (bm24_display_slot){140, 142, 2, true};
    format_rate(state->hw_khs + state->sw_khs, frame->line[3],
                sizeof(frame->line[3]));
}

static void network_page(const bm24_metrics_snapshot *metrics,
                         bm24_display_frame *frame)
{
    frame->background = bm24_img_network;
    frame->slot_count = 5;

    frame->slot[0] = (bm24_display_slot){306, 56, 2, true};
    if (metrics->chain_valid)
        snprintf(frame->line[0], sizeof(frame->line[0]), "%" PRIu32,
                 metrics->block_height);
    else
        strlcpy(frame->line[0], "-", sizeof(frame->line[0]));

    frame->slot[1] = (bm24_display_slot){142, 90, 2, true};
    snprintf(frame->line[1], sizeof(frame->line[1]), "%" PRIu32,
             metrics->retarget_blocks);

    frame->slot[2] = (bm24_display_slot){128, 134, 2, true};
    snprintf(frame->line[2], sizeof(frame->line[2]), "%.0f",
             metrics->global_hash_eh);

    frame->slot[3] = (bm24_display_slot){282, 116, 2, true};
    snprintf(frame->line[3], sizeof(frame->line[3]), "%" PRIu32,
             metrics->half_hour_fee);

    frame->slot[4] = (bm24_display_slot){306, 142, 2, true};
    snprintf(frame->line[4], sizeof(frame->line[4]), "%.0fT",
             metrics->network_difficulty_t);
}

static void price_page(const bm24_ui_state *state,
                       const bm24_metrics_snapshot *metrics,
                       bm24_display_frame *frame)
{
    frame->background = bm24_img_price;
    frame->slot_count = 4;

    frame->slot[0] = (bm24_display_slot){300, 62, 4, true};
    if (metrics->price_valid)
        bm24_thousands(metrics->btc_usd, frame->line[0],
                       sizeof(frame->line[0]));
    else
        strlcpy(frame->line[0], "-", sizeof(frame->line[0]));

    frame->slot[1] = (bm24_display_slot){90, 144, 2, true};
    format_rate(state->hw_khs + state->sw_khs, frame->line[1],
                sizeof(frame->line[1]));

    frame->slot[2] = (bm24_display_slot){198, 144, 2, true};
    if (metrics->chain_valid)
        snprintf(frame->line[2], sizeof(frame->line[2]), "%" PRIu32,
                 metrics->block_height);
    else
        strlcpy(frame->line[2], "-", sizeof(frame->line[2]));

    frame->slot[3] = (bm24_display_slot){306, 144, 2, true};
    format_clock(metrics->time_synced, frame->line[3],
                 sizeof(frame->line[3]));
}

static void solo_page(const bm24_metrics_snapshot *metrics,
                      bm24_display_frame *frame)
{
    /* Fuer den Solo-Tracker gibt es noch keine eigene Grafik; bis sie
       vorliegt bleibt die Seite bewusst ohne Hintergrund statt ein
       thematisch falsches Bild zu zeigen. */
    frame->style = BM24_DISPLAY_STYLE_DASHBOARD;
    strlcpy(frame->line[0], "SOLO TRACKER 5/5", sizeof(frame->line[0]));
    if (!metrics->solo_valid) {
        strlcpy(frame->line[1], "DATEN WERDEN GELADEN",
                sizeof(frame->line[1]));
        strlcpy(frame->line[2], "MINING LAEUFT WEITER",
                sizeof(frame->line[2]));
        return;
    }

    char height[24], jackpot[24], age[32];
    bm24_thousands(metrics->solo_last_height, height, sizeof(height));
    bm24_thousands(metrics->jackpot_eur, jackpot, sizeof(jackpot));
    time_t now;
    time(&now);
    long seconds_ago = now > metrics->solo_last_timestamp
        ? (long)(now - metrics->solo_last_timestamp) : 0;
    bm24_age_text(seconds_ago, age, sizeof(age));

    snprintf(frame->line[1], sizeof(frame->line[1]),
             "LETZTER BLOCK #%s", height);
    strlcpy(frame->line[2], age, sizeof(frame->line[2]));
    snprintf(frame->line[3], sizeof(frame->line[3]),
             "JACKPOT %s EUR", jackpot);
    snprintf(frame->line[4], sizeof(frame->line[4]),
             "GESAMT %u / DIESES JAHR %u",
             (unsigned)metrics->solo_total_blocks,
             (unsigned)metrics->solo_blocks_this_year);
    char change[16];
    format_percent(metrics->retarget_change, change, sizeof(change));
    snprintf(frame->line[5], sizeof(frame->line[5]),
             "SCHNITT %u T | RET %u %.8s",
             (unsigned)metrics->solo_avg_days,
             (unsigned)metrics->retarget_blocks,
             change);
}

static bool debounce(button_state *button, bool pressed)
{
    if (pressed != button->raw_pressed) {
        button->raw_pressed = pressed;
        button->stable_ticks = 0;
        return false;
    }
    if (button->stable_ticks < DEBOUNCE_TICKS)
        ++button->stable_ticks;
    if (button->stable_ticks == DEBOUNCE_TICKS &&
        button->stable_pressed != pressed) {
        button->stable_pressed = pressed;
        return true;
    }
    return false;
}

static void button_task(void *arg)
{
    (void)arg;
    button_state display = {0};
    button_state page = {0};
    bool display_click_pending = false;
    uint32_t display_click_at = 0;

    for (;;) {
        uint32_t now = now_ms();
        bool display_pressed = gpio_get_level(BUTTON_DISPLAY) == 0;
        bool page_pressed = gpio_get_level(BUTTON_PAGE) == 0;

        if (debounce(&display, display_pressed)) {
            if (display.stable_pressed) {
                display.pressed_at = now;
            } else if ((uint32_t)(now - display.pressed_at) >= 40u &&
                       (uint32_t)(now - display.pressed_at) < LONG_MS) {
                if (display_click_pending &&
                    (uint32_t)(now - display_click_at) <= DOUBLE_MS) {
                    display_click_pending = false;
                    bm24_display_toggle_rotation();
                    ESP_LOGI(TAG, "Display gedreht");
                } else {
                    display_click_pending = true;
                    display_click_at = now;
                }
            }
        }
        if (display_click_pending &&
            (uint32_t)(now - display_click_at) > DOUBLE_MS) {
            display_click_pending = false;
            bm24_display_toggle_enabled();
            ESP_LOGI(TAG, "Display an/aus");
        }

        if (debounce(&page, page_pressed)) {
            if (page.stable_pressed) {
                page.pressed_at = now;
                page.long_fired = false;
            } else if (!page.long_fired &&
                       (uint32_t)(now - page.pressed_at) >= 40u) {
                s_page = (uint8_t)((s_page + 1u) % PAGE_COUNT);
                ESP_LOGI(TAG, "Seite %u/%u", (unsigned)s_page + 1u,
                         (unsigned)PAGE_COUNT);
            }
        }
        if (page.stable_pressed) {
            uint32_t held = (uint32_t)(now - page.pressed_at);
            if (!page.long_fired && held >= LONG_MS) {
                page.long_fired = true;
                if (bm24_network_open_portal())
                    ESP_LOGW(TAG, "Setup-Portal per Langdruck geoeffnet");
            }
            if (page.long_fired && held >= LONG_MS && held < RESET_MS) {
                /* Ruecklauf sichtbar machen, solange der Knopf gehalten wird */
                bm24_display_frame warn = {0};
                snprintf(warn.line[0], sizeof(warn.line[0]), "WERKSRESET");
                snprintf(warn.line[1], sizeof(warn.line[1]), "IN %u S",
                         (unsigned)((RESET_MS - held + 999u) / 1000u));
                snprintf(warn.line[3], sizeof(warn.line[3]),
                         "LOSLASSEN BRICHT AB");
                bm24_display_set(&warn);
            }
            if (held >= RESET_MS) {
                ESP_LOGW(TAG, "Werksreset ausgeloest");
                bm24_display_frame done = {0};
                snprintf(done.line[0], sizeof(done.line[0]), "WERKSRESET");
                snprintf(done.line[2], sizeof(done.line[2]), "NEUSTART");
                bm24_display_set(&done);
                bm24_config_erase();
                vTaskDelay(pdMS_TO_TICKS(1500));
                esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool bm24_ui_start(void)
{
    if (s_button_task)
        return true;
    gpio_config_t buttons = {
        .pin_bit_mask = (1ULL << BUTTON_DISPLAY) |
                        (1ULL << BUTTON_PAGE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    if (gpio_config(&buttons) != ESP_OK || !bm24_metrics_start())
        return false;
    return xTaskCreatePinnedToCore(button_task, "bm24Buttons", 3072, NULL,
                                   6, &s_button_task, 1) == pdPASS;
}

void bm24_ui_update(const bm24_ui_state *state)
{
    if (!state)
        return;
    if (state->network.portal_active) {
        bm24_display_setup(state->network.setup_ssid,
                           BM24_SETUP_PASSWORD);
        return;
    }

    bm24_metrics_snapshot metrics;
    bm24_metrics_get(&metrics);
    bm24_display_frame frame = {0};
    switch (s_page) {
    case 1: clock_page(state, &metrics, &frame); break;
    case 2: network_page(&metrics, &frame); break;
    case 3: price_page(state, &metrics, &frame); break;
    case 4: solo_page(&metrics, &frame); break;
    case 0:
    default:
        miner_page(state, &frame);
        break;
    }
    bm24_display_set(&frame);
}
