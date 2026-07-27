#include "bm24_metrics.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "bm24_miner.h"
#include "bm24_network.h"

#define RESPONSE_CAPACITY (12u * 1024u)
#define FETCH_GAP_MS      5000u

typedef enum {
    ENDPOINT_PRICE = 0,
    ENDPOINT_HASHRATE,
    ENDPOINT_FEES,
    ENDPOINT_HEIGHT,
    ENDPOINT_RETARGET,
    ENDPOINT_SOLO,
    ENDPOINT_COUNT
} endpoint_id;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool overflow;
} response_buffer;

static const char *TAG = "bm24_metrics";
static const char *URLS[ENDPOINT_COUNT] = {
    [ENDPOINT_PRICE] =
        "https://api.coingecko.com/api/v3/simple/price"
        "?ids=bitcoin&vs_currencies=usd",
    [ENDPOINT_HASHRATE] =
        "https://mempool.space/api/v1/mining/hashrate/3d",
    [ENDPOINT_FEES] =
        "https://mempool.space/api/v1/fees/recommended",
    [ENDPOINT_HEIGHT] =
        "https://mempool.space/api/blocks/tip/height",
    [ENDPOINT_RETARGET] =
        "https://mempool.space/api/v1/difficulty-adjustment",
    [ENDPOINT_SOLO] =
        "https://bitminer24-solo-tracker.proud-dawn-de10.workers.dev"
};
static const uint32_t INTERVAL_MS[ENDPOINT_COUNT] = {
    [ENDPOINT_PRICE] = 300000u,
    [ENDPOINT_HASHRATE] = 300000u,
    [ENDPOINT_FEES] = 300000u,
    [ENDPOINT_HEIGHT] = 120000u,
    [ENDPOINT_RETARGET] = 600000u,
    [ENDPOINT_SOLO] = 1800000u
};

static char s_response[RESPONSE_CAPACITY];
static bm24_metrics_snapshot s_snapshot;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_buffer *response = (response_buffer *)event->user_data;
    if (!response || event->event_id != HTTP_EVENT_ON_DATA ||
        event->data_len <= 0)
        return ESP_OK;
    if (response->length + (size_t)event->data_len + 1 >
        response->capacity) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->data + response->length, event->data,
           (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static bool http_get(const char *url, size_t *length)
{
    response_buffer response = {
        .data = s_response,
        .capacity = sizeof(s_response)
    };
    s_response[0] = '\0';
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event,
        .user_data = &response,
        .timeout_ms = 7000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size = 1024
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;
    esp_http_client_set_header(client, "User-Agent", "BitMiner24/2.0-idf55");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");

    /* TLS und der native Miner teilen auf dem S3 SHA/AES-Steuerregister.
       Nur der deutlich kuerzere Netzwerkaufruf bekommt das Zeitfenster. */
    bm24_miner_set_network_window(true);
    esp_err_t err = esp_http_client_perform(client);
    bm24_miner_set_network_window(false);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || response.overflow ||
        response.length == 0) {
        ESP_LOGW(TAG, "HTTP %s: %s, Status %d",
                 url, esp_err_to_name(err), status);
        return false;
    }
    if (length)
        *length = response.length;
    return true;
}

static cJSON *number_item(const cJSON *object, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item : NULL;
}

static bool parse_price(size_t length)
{
    cJSON *root = cJSON_ParseWithLength(s_response, length);
    cJSON *bitcoin = root
        ? cJSON_GetObjectItemCaseSensitive(root, "bitcoin") : NULL;
    cJSON *usd = cJSON_IsObject(bitcoin) ? number_item(bitcoin, "usd") : NULL;
    if (!usd) {
        cJSON_Delete(root);
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    s_snapshot.btc_usd = usd->valuedouble;
    s_snapshot.price_valid = true;
    portEXIT_CRITICAL(&s_lock);
    cJSON_Delete(root);
    return true;
}

static bool parse_hashrate(size_t length)
{
    cJSON *root = cJSON_ParseWithLength(s_response, length);
    cJSON *hashrate = root ? number_item(root, "currentHashrate") : NULL;
    cJSON *difficulty = root ? number_item(root, "currentDifficulty") : NULL;
    if (!hashrate || !difficulty) {
        cJSON_Delete(root);
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    s_snapshot.global_hash_eh = hashrate->valuedouble / 1.0e18;
    s_snapshot.network_difficulty_t = difficulty->valuedouble / 1.0e12;
    s_snapshot.chain_valid = true;
    portEXIT_CRITICAL(&s_lock);
    cJSON_Delete(root);
    return true;
}

static bool parse_fees(size_t length)
{
    cJSON *root = cJSON_ParseWithLength(s_response, length);
    cJSON *fee = root ? number_item(root, "halfHourFee") : NULL;
    if (!fee || fee->valuedouble < 0.0) {
        cJSON_Delete(root);
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    s_snapshot.half_hour_fee = (uint32_t)fee->valuedouble;
    portEXIT_CRITICAL(&s_lock);
    cJSON_Delete(root);
    return true;
}

static bool parse_height(size_t length)
{
    errno = 0;
    char *end = NULL;
    unsigned long height = strtoul(s_response, &end, 10);
    while (end && (size_t)(end - s_response) < length &&
           isspace((unsigned char)*end))
        ++end;
    if (errno || !end || (size_t)(end - s_response) != length ||
        height == 0 || height > UINT32_MAX)
        return false;
    uint32_t halving = 210000u - ((uint32_t)height % 210000u);
    portENTER_CRITICAL(&s_lock);
    s_snapshot.block_height = (uint32_t)height;
    s_snapshot.halving_blocks = halving;
    s_snapshot.chain_valid = true;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

static bool parse_retarget(size_t length)
{
    cJSON *root = cJSON_ParseWithLength(s_response, length);
    cJSON *blocks = root ? number_item(root, "remainingBlocks") : NULL;
    cJSON *change = root ? number_item(root, "difficultyChange") : NULL;
    if (!blocks || !change || blocks->valuedouble < 0.0) {
        cJSON_Delete(root);
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    s_snapshot.retarget_blocks = (uint32_t)blocks->valuedouble;
    s_snapshot.retarget_change = change->valuedouble;
    portEXIT_CRITICAL(&s_lock);
    cJSON_Delete(root);
    return true;
}

static bool parse_solo(size_t length)
{
    cJSON *root = cJSON_ParseWithLength(s_response, length);
    cJSON *stats = root
        ? cJSON_GetObjectItemCaseSensitive(root, "stats") : NULL;
    cJSON *jackpot = root
        ? cJSON_GetObjectItemCaseSensitive(root, "jackpot") : NULL;
    cJSON *last_height = cJSON_IsObject(stats)
        ? number_item(stats, "lastFindHeight") : NULL;
    cJSON *last_time = cJSON_IsObject(stats)
        ? number_item(stats, "lastFindTimestamp") : NULL;
    cJSON *total = cJSON_IsObject(stats)
        ? number_item(stats, "totalBlocks") : NULL;
    cJSON *year = cJSON_IsObject(stats)
        ? number_item(stats, "blocksThisYear") : NULL;
    cJSON *average = cJSON_IsObject(stats)
        ? number_item(stats, "avgDaysBetween") : NULL;
    cJSON *eur = cJSON_IsObject(jackpot)
        ? number_item(jackpot, "eur") : NULL;
    if (!last_height || !last_time || !total || !year || !average || !eur) {
        cJSON_Delete(root);
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    s_snapshot.solo_last_height = (uint32_t)last_height->valuedouble;
    s_snapshot.solo_last_timestamp = (int64_t)last_time->valuedouble;
    s_snapshot.solo_total_blocks = (uint32_t)total->valuedouble;
    s_snapshot.solo_blocks_this_year = (uint32_t)year->valuedouble;
    s_snapshot.solo_avg_days = (uint32_t)average->valuedouble;
    s_snapshot.jackpot_eur = eur->valuedouble;
    s_snapshot.solo_valid = true;
    portEXIT_CRITICAL(&s_lock);
    cJSON_Delete(root);
    return true;
}

static bool fetch_endpoint(endpoint_id endpoint)
{
    size_t length;
    if (!http_get(URLS[endpoint], &length))
        return false;
    switch (endpoint) {
    case ENDPOINT_PRICE:    return parse_price(length);
    case ENDPOINT_HASHRATE: return parse_hashrate(length);
    case ENDPOINT_FEES:     return parse_fees(length);
    case ENDPOINT_HEIGHT:   return parse_height(length);
    case ENDPOINT_RETARGET: return parse_retarget(length);
    case ENDPOINT_SOLO:     return parse_solo(length);
    default:                return false;
    }
}

static void metrics_task(void *arg)
{
    (void)arg;
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    uint32_t due[ENDPOINT_COUNT];
    uint32_t started = now_ms();
    for (uint32_t i = 0; i < ENDPOINT_COUNT; ++i)
        due[i] = started + i * FETCH_GAP_MS;
    uint32_t last_fetch = started - FETCH_GAP_MS;

    for (;;) {
        if (!bm24_network_wait_connected(10000))
            continue;
        if (bm24_network_sync_time(15000)) {
            portENTER_CRITICAL(&s_lock);
            s_snapshot.time_synced = true;
            portEXIT_CRITICAL(&s_lock);
        }

        uint32_t now = now_ms();
        if ((uint32_t)(now - last_fetch) < FETCH_GAP_MS) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        endpoint_id selected = ENDPOINT_COUNT;
        for (uint32_t i = 0; i < ENDPOINT_COUNT; ++i) {
            if ((int32_t)(now - due[i]) >= 0) {
                selected = (endpoint_id)i;
                break;
            }
        }
        if (selected == ENDPOINT_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        bool ok = fetch_endpoint(selected);
        portENTER_CRITICAL(&s_lock);
        if (ok)
            ++s_snapshot.successful_requests;
        else
            ++s_snapshot.failed_requests;
        portEXIT_CRITICAL(&s_lock);
        due[selected] = now_ms() + INTERVAL_MS[selected];
        last_fetch = now_ms();
    }
}

bool bm24_metrics_start(void)
{
    if (s_task)
        return true;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    return xTaskCreatePinnedToCore(metrics_task, "bm24Metrics", 10240, NULL,
                                   3, &s_task, 1) == pdPASS;
}

void bm24_metrics_get(bm24_metrics_snapshot *out)
{
    if (!out)
        return;
    portENTER_CRITICAL(&s_lock);
    memcpy(out, &s_snapshot, sizeof(*out));
    portEXIT_CRITICAL(&s_lock);
}
