#include "bm24_ui.h"

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

static void miner_page(const bm24_ui_state *state,
                       bm24_display_frame *frame)
{
    frame->style = BM24_DISPLAY_STYLE_BIG_VALUE;
    frame->background = bm24_img_miner;
    strlcpy(frame->line[0], "MINER 1/5", sizeof(frame->line[0]));
    format_rate(state->hw_khs + state->sw_khs, frame->line[1],
                sizeof(frame->line[1]));
    snprintf(frame->line[2], sizeof(frame->line[2]),
             "HW %.1f / SW %.1f", state->hw_khs, state->sw_khs);
    snprintf(frame->line[3], sizeof(frame->line[3]),
             "TEMP %.1f C / DUTY %u%%", state->temperature_c,
             (unsigned)state->miner.sw_duty_percent);
    char best[20];
    format_difficulty(state->pool.best_difficulty, best, sizeof(best));
    snprintf(frame->line[4], sizeof(frame->line[4]),
             "SHARES %" PRIu64 "/%" PRIu64 " / BEST %s",
             state->pool.accepted, state->pool.submitted, best);
    char uptime[24];
    format_uptime(state->uptime_seconds, uptime, sizeof(uptime));
    snprintf(frame->line[5], sizeof(frame->line[5]),
             "%s | JOB %" PRIu32 " | %s", uptime,
             state->pool.active_job_tag,
             state->pool.connected ? "POOL ONLINE" : "POOL WARTET");
}

static void clock_page(const bm24_ui_state *state,
                       const bm24_metrics_snapshot *metrics,
                       bm24_display_frame *frame)
{
    frame->style = BM24_DISPLAY_STYLE_BIG_VALUE;
    frame->background = bm24_img_clock;
    strlcpy(frame->line[0], "UHR / BLOCK 2/5", sizeof(frame->line[0]));
    format_clock(metrics->time_synced, frame->line[1],
                 sizeof(frame->line[1]));
    char block[24] = "-";
    char price[24] = "-";
    if (metrics->block_height)
        bm24_thousands(metrics->block_height, block, sizeof(block));
    if (metrics->price_valid)
        bm24_thousands(metrics->btc_usd, price, sizeof(price));
    snprintf(frame->line[2], sizeof(frame->line[2]), "BLOCK #%s", block);
    snprintf(frame->line[3], sizeof(frame->line[3]), "BITCOIN $%s", price);
    char rate[24];
    format_rate(state->hw_khs + state->sw_khs, rate, sizeof(rate));
    snprintf(frame->line[4], sizeof(frame->line[4]), "MINER %s", rate);
    snprintf(frame->line[5], sizeof(frame->line[5]),
             "WIFI %d DBM | SHARES %" PRIu64 "/%" PRIu64,
             (int)state->network.rssi,
             state->pool.accepted, state->pool.submitted);
}

static void network_page(const bm24_metrics_snapshot *metrics,
                         bm24_display_frame *frame)
{
    frame->style = BM24_DISPLAY_STYLE_DASHBOARD;
    frame->background = bm24_img_network;
    strlcpy(frame->line[0], "BITCOIN NETZ 3/5", sizeof(frame->line[0]));
    char block[24] = "-";
    char halving[24] = "-";
    if (metrics->block_height)
        bm24_thousands(metrics->block_height, block, sizeof(block));
    if (metrics->halving_blocks)
        bm24_thousands(metrics->halving_blocks, halving, sizeof(halving));
    snprintf(frame->line[1], sizeof(frame->line[1]), "BLOCK #%s", block);
    if (metrics->global_hash_eh > 0.0)
        snprintf(frame->line[2], sizeof(frame->line[2]),
                 "HASH %.1f EH/S", metrics->global_hash_eh);
    else
        strlcpy(frame->line[2], "HASH WIRD GELADEN",
                sizeof(frame->line[2]));
    if (metrics->network_difficulty_t > 0.0)
        snprintf(frame->line[3], sizeof(frame->line[3]),
                 "DIFF %.2f T", metrics->network_difficulty_t);
    else
        strlcpy(frame->line[3], "DIFF WIRD GELADEN",
                sizeof(frame->line[3]));
    if (metrics->half_hour_fee)
        snprintf(frame->line[4], sizeof(frame->line[4]),
                 "FEE %u SAT/VB", (unsigned)metrics->half_hour_fee);
    else
        strlcpy(frame->line[4], "FEE WIRD GELADEN",
                sizeof(frame->line[4]));
    char change[16];
    format_percent(metrics->retarget_change, change, sizeof(change));
    snprintf(frame->line[5], sizeof(frame->line[5]),
             "HALV %.10s | RET %u %.8s",
             halving, (unsigned)metrics->retarget_blocks, change);
}

static void price_page(const bm24_ui_state *state,
                       const bm24_metrics_snapshot *metrics,
                       bm24_display_frame *frame)
{
    frame->style = BM24_DISPLAY_STYLE_BIG_VALUE;
    frame->background = bm24_img_price;
    strlcpy(frame->line[0], "BITCOIN PREIS 4/5", sizeof(frame->line[0]));
    char price[24] = "-";
    char block[24] = "-";
    if (metrics->price_valid)
        bm24_thousands(metrics->btc_usd, price, sizeof(price));
    if (metrics->block_height)
        bm24_thousands(metrics->block_height, block, sizeof(block));
    snprintf(frame->line[1], sizeof(frame->line[1]), "$%s", price);
    snprintf(frame->line[2], sizeof(frame->line[2]), "BLOCK #%s", block);
    snprintf(frame->line[3], sizeof(frame->line[3]),
             "MEDIUM FEE %u SAT/VB", (unsigned)metrics->half_hour_fee);
    char rate[24];
    format_rate(state->hw_khs + state->sw_khs, rate, sizeof(rate));
    snprintf(frame->line[4], sizeof(frame->line[4]), "MINER %s", rate);
    format_clock(metrics->time_synced, frame->line[5],
                 sizeof(frame->line[5]));
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
        if (page.stable_pressed && !page.long_fired &&
            (uint32_t)(now - page.pressed_at) >= LONG_MS) {
            page.long_fired = true;
            if (bm24_network_open_portal())
                ESP_LOGW(TAG, "Setup-Portal per Langdruck geoeffnet");
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
