#include "bm24_miner.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "bm24_sha.h"
#include "bm24_sha_hw.h"
#include "bm24_sha_sw.h"
#include "bm24_work.h"

#define BM24_HW_CHUNK 8192u
#define BM24_SW_CHUNK 4096u
#define BM24_SHARE_QUEUE_LENGTH 16u
#define BM24_NETWORK_WINDOW_MAX_MS 20000u

static SemaphoreHandle_t s_job_mutex;
static QueueHandle_t s_share_queue;
static TaskHandle_t s_hw_task;
static TaskHandle_t s_sw_task;

static bm24_miner_job s_job;
static volatile uint32_t s_generation;
static volatile bool s_active;
static volatile bool s_started;
static volatile bool s_hw_trusted;
static volatile uint8_t s_sw_duty = 100;
static volatile bool s_network_window;
static volatile uint32_t s_network_deadline_ms;
static uint32_t s_network_users;
static uint32_t s_nonce_seed;

static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_network_lock = portMUX_INITIALIZER_UNLOCKED;
static bm24_miner_stats s_stats;
static uint64_t s_total_hw_hashes;
static uint64_t s_total_sw_hashes;

typedef struct {
    bm24_miner_job job;
    uint32_t generation;
} candidate_context;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool network_window_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_network_lock);
    active = s_network_window;
    if (active && (int32_t)(s_network_deadline_ms - now_ms()) <= 0) {
        s_network_window = false;
        s_network_users = 0;
        active = false;
    }
    portEXIT_CRITICAL(&s_network_lock);
    return active;
}

static void stats_reset_for_job(uint32_t generation)
{
    portENTER_CRITICAL(&s_stats_lock);
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.generation = generation;
    s_stats.active = true;
    s_stats.hw_trusted = s_hw_trusted;
    s_stats.sw_duty_percent = s_sw_duty;
    s_stats.total_hw_hashes = s_total_hw_hashes;
    s_stats.total_sw_hashes = s_total_sw_hashes;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void stats_add_hw(const bm24_hw_result *r)
{
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.hw_hashes += r->hashes;
    s_total_hw_hashes += r->hashes;
    s_stats.total_hw_hashes = s_total_hw_hashes;
    s_stats.hw_candidates += r->candidates;
    s_stats.mismatches += r->mismatches;
    s_stats.hw_trusted = s_hw_trusted;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void stats_add_sw(uint32_t hashes, uint32_t candidates,
                         uint32_t mismatches)
{
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.sw_hashes += hashes;
    s_total_sw_hashes += hashes;
    s_stats.total_sw_hashes = s_total_sw_hashes;
    s_stats.sw_candidates += candidates;
    s_stats.mismatches += mismatches;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void queue_share(const bm24_miner_job *job, uint32_t generation,
                        uint32_t nonce, const uint8_t hash[32],
                        bm24_miner_source source)
{
    if (!s_active || s_generation != generation)
        return;

    double difficulty = bm24_hash_difficulty(hash);
    if (difficulty < job->pool_difficulty)
        return;

    bm24_miner_share share = {
        .job_tag = job->tag,
        .nonce = nonce,
        .difficulty = difficulty,
        .network_block =
            bm24_hash_meets_target(hash, job->network_target_le),
        .source = source
    };
    memcpy(share.hash, hash, sizeof(share.hash));

    bool queued = xQueueSend(s_share_queue, &share, 0) == pdTRUE;
    portENTER_CRITICAL(&s_stats_lock);
    if (queued) s_stats.shares++;
    else        s_stats.dropped_shares++;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void hw_candidate(uint32_t nonce, const uint8_t hash[32], void *arg)
{
    candidate_context *ctx = (candidate_context *)arg;
    queue_share(&ctx->job, ctx->generation, nonce, hash,
                BM24_MINER_SOURCE_HW);
}

static bool load_job(uint32_t *local_generation, bm24_miner_job *job,
                     uint32_t *nonce, bool software, bool *loaded)
{
    if (loaded)
        *loaded = false;
    uint32_t generation = s_generation;
    if (!s_active || generation == *local_generation)
        return s_active;

    if (xSemaphoreTake(s_job_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;
    if (!s_active || s_generation != generation) {
        xSemaphoreGive(s_job_mutex);
        return false;
    }
    memcpy(job, &s_job, sizeof(*job));
    uint32_t seed = s_nonce_seed & 0x7fffffffu;
    *nonce = software ? (seed | 0x80000000u) : seed;
    *local_generation = generation;
    if (loaded)
        *loaded = true;
    xSemaphoreGive(s_job_mutex);
    return true;
}

static void hw_worker(void *arg)
{
    (void)arg;
    uint32_t local_generation = 0;
    uint32_t nonce = 0;
    bm24_miner_job job;

    for (;;) {
        if (!s_hw_trusted ||
            !load_job(&local_generation, &job, &nonce, false, NULL)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!s_active || s_generation != local_generation)
            continue;
        if (network_window_active()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        candidate_context ctx = {
            .job = job,
            .generation = local_generation
        };
        bm24_hw_result result = bm24_sha_hw_scan_candidates(
            job.header, nonce, BM24_HW_CHUNK, hw_candidate, &ctx);
        nonce += result.hashes;
        if (result.mismatches != 0)
            s_hw_trusted = false;
        stats_add_hw(&result);
    }
}

static void sw_worker(void *arg)
{
    (void)arg;
    uint32_t local_generation = 0;
    uint32_t nonce = 0;
    bm24_miner_job job;
    uint32_t mid[8], bake[16];
    uint8_t tail[16];

    for (;;) {
        bool loaded = false;
        if (!load_job(&local_generation, &job, &nonce, true, &loaded)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (loaded) {
            memcpy(tail, job.header + 64, sizeof(tail));
            nerd_mids(mid, job.header);
            nerd_sha256_bake(mid, tail, bake);
        }
        if (!s_active || s_generation != local_generation)
            continue;

        uint8_t duty = s_sw_duty;
        if (duty == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int64_t started = esp_timer_get_time();
        uint32_t candidates = 0;
        uint32_t mismatches = 0;
        for (uint32_t i = 0; i < BM24_SW_CHUNK; ++i) {
            uint32_t n = nonce + i;
            memcpy(tail + 12, &n, sizeof(n));

            uint8_t got[32];
            if (!nerd_sha256d_baked(mid, tail, bake, got))
                continue;

            uint8_t want[32];
            bm24_double_sha_from_midstate(mid, job.header + 64, n, want);
            if (memcmp(got, want, sizeof(got)) != 0) {
                mismatches++;
                continue;
            }
            candidates++;
            queue_share(&job, local_generation, n, got,
                        BM24_MINER_SOURCE_SW);
        }
        nonce += BM24_SW_CHUNK;
        stats_add_sw(BM24_SW_CHUNK, candidates, mismatches);

        if (duty < 100) {
            int64_t active_us = esp_timer_get_time() - started;
            uint64_t rest_us =
                (uint64_t)active_us * (100u - duty) / duty;
            uint32_t rest_ms = (uint32_t)((rest_us + 999u) / 1000u);
            TickType_t ticks = pdMS_TO_TICKS(rest_ms);
            if (ticks == 0)
                ticks = 1;
            vTaskDelay(ticks);
        }
    }
}

bool bm24_miner_start(void)
{
    if (s_started)
        return true;
    if (!bm24_sha_hw_selftest(64))
        return false;

    s_job_mutex = xSemaphoreCreateMutex();
    s_share_queue =
        xQueueCreate(BM24_SHARE_QUEUE_LENGTH, sizeof(bm24_miner_share));
    if (!s_job_mutex || !s_share_queue) {
        if (s_share_queue) vQueueDelete(s_share_queue);
        if (s_job_mutex) vSemaphoreDelete(s_job_mutex);
        s_share_queue = NULL;
        s_job_mutex = NULL;
        return false;
    }

    s_hw_trusted = true;
    BaseType_t hw_ok = xTaskCreatePinnedToCore(
        hw_worker, "bm24Hw", 8192, NULL, 2, &s_hw_task, 0);
    BaseType_t sw_ok = xTaskCreatePinnedToCore(
        sw_worker, "bm24Sw", 8192, NULL, 2, &s_sw_task, 1);
    if (hw_ok != pdPASS || sw_ok != pdPASS) {
        if (s_hw_task) vTaskDelete(s_hw_task);
        if (s_sw_task) vTaskDelete(s_sw_task);
        vQueueDelete(s_share_queue);
        vSemaphoreDelete(s_job_mutex);
        s_hw_task = NULL;
        s_sw_task = NULL;
        s_share_queue = NULL;
        s_job_mutex = NULL;
        return false;
    }

    s_started = true;
    return true;
}

bool bm24_miner_set_job(const bm24_miner_job *job)
{
    if (!s_started || !job || !(job->pool_difficulty > 0.0))
        return false;
    if (xSemaphoreTake(s_job_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    memcpy(&s_job, job, sizeof(s_job));
    s_nonce_seed = esp_random();
    uint32_t generation = s_generation + 1;
    stats_reset_for_job(generation);
    s_generation = generation;
    s_active = true;
    xQueueReset(s_share_queue);
    xSemaphoreGive(s_job_mutex);
    return true;
}

void bm24_miner_clear_job(void)
{
    if (!s_started)
        return;
    if (xSemaphoreTake(s_job_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_active = false;
        ++s_generation;
        xQueueReset(s_share_queue);
        xSemaphoreGive(s_job_mutex);
    }
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.active = false;
    portEXIT_CRITICAL(&s_stats_lock);
}

bool bm24_miner_get_share(bm24_miner_share *out, uint32_t timeout_ms)
{
    if (!s_started || !out)
        return false;
    return xQueueReceive(s_share_queue, out, pdMS_TO_TICKS(timeout_ms)) ==
           pdTRUE;
}

void bm24_miner_get_stats(bm24_miner_stats *out)
{
    if (!out)
        return;
    portENTER_CRITICAL(&s_stats_lock);
    memcpy(out, &s_stats, sizeof(*out));
    out->active = s_active;
    out->hw_trusted = s_hw_trusted;
    out->sw_duty_percent = s_sw_duty;
    portEXIT_CRITICAL(&s_stats_lock);
}

void bm24_miner_set_sw_duty(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    s_sw_duty = percent;
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.sw_duty_percent = percent;
    portEXIT_CRITICAL(&s_stats_lock);
}

void bm24_miner_set_network_window(bool active)
{
    portENTER_CRITICAL(&s_network_lock);
    if (active) {
        ++s_network_users;
        s_network_deadline_ms = now_ms() + BM24_NETWORK_WINDOW_MAX_MS;
        s_network_window = true;
    } else {
        if (s_network_users)
            --s_network_users;
        s_network_window = s_network_users != 0;
    }
    portEXIT_CRITICAL(&s_network_lock);
}
