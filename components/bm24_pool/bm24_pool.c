#include "bm24_pool.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_tls_errors.h"

#include "bm24_miner.h"
#include "bm24_network.h"
#include "bm24_stratum.h"
#include "bm24_work.h"

#define BM24_LINE_MAX              8192u
#define BM24_WIRE_MAX               512u
#define BM24_DEFAULT_DIFFICULTY  0.00015
#define BM24_JOB_TIMEOUT_MS       600000u
#define BM24_PENDING_SUBMITS          32u

typedef struct {
    bool used;
    bool network_block;
    uint32_t id;
} pending_submit;

typedef struct {
    esp_tls_t *transport;
    bm24_stratum_subscription subscription;
    bm24_stratum_job pending_job;
    bool has_subscription;
    bool has_pending_job;
    bm24_stratum_job active_job;
    char active_extranonce2[17];
    uint32_t active_tag;
    uint64_t extranonce2;
    double difficulty;
    uint32_t next_request_id;
    uint32_t last_job_ms;
    pending_submit submits[BM24_PENDING_SUBMITS];
} pool_session;

static volatile bool s_reconnect_requested;

static const char *TAG = "bm24_pool";
static bm24_config s_config;
static bm24_pool_stats s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static char s_line[BM24_LINE_MAX];
static bm24_stratum_message s_message;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void stats_error(const char *message)
{
    portENTER_CRITICAL(&s_stats_lock);
    strlcpy(s_stats.last_error, message ? message : "unknown",
            sizeof(s_stats.last_error));
    portEXIT_CRITICAL(&s_stats_lock);
}

static void stats_connection(bool connected)
{
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.connected = connected;
    if (!connected) {
        s_stats.subscribed = false;
        s_stats.authorized = false;
        s_stats.active_job_tag = 0;
    }
    portEXIT_CRITICAL(&s_stats_lock);
}

static bool transport_would_block(ssize_t result)
{
    return result == ESP_TLS_ERR_SSL_WANT_READ ||
           result == ESP_TLS_ERR_SSL_WANT_WRITE ||
           (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                          errno == EINPROGRESS));
}

static bool transport_write_all(esp_tls_t *transport,
                                const char *data, size_t length)
{
    size_t sent = 0;
    uint32_t deadline = now_ms() + 10000u;
    while (sent < length && (int32_t)(deadline - now_ms()) > 0) {
        if (s_config.pool_tls)
            bm24_miner_set_network_window(true);
        ssize_t n = esp_tls_conn_write(transport, data + sent,
                                       length - sent);
        if (s_config.pool_tls)
            bm24_miner_set_network_window(false);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (!transport_would_block(n))
            return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return sent == length;
}

static bool send_subscribe(pool_session *session)
{
    char wire[BM24_WIRE_MAX];
    size_t length = bm24_stratum_write_subscribe(
        wire, sizeof(wire), 1, "BitMiner24/2.0-idf55");
    if (!length || !transport_write_all(session->transport, wire, length))
        return false;

    length = bm24_stratum_write_authorize(
        wire, sizeof(wire), 2, s_config.worker, s_config.pool_password);
    if (!length || !transport_write_all(session->transport, wire, length))
        return false;

    length = bm24_stratum_write_suggest_difficulty(
        wire, sizeof(wire), 3, BM24_DEFAULT_DIFFICULTY);
    return length &&
           transport_write_all(session->transport, wire, length);
}

static bool activate_work(pool_session *session,
                          const bm24_stratum_job *job,
                          bool new_extranonce)
{
    if (!session->has_subscription || !job)
        return false;

    if (new_extranonce)
        ++session->extranonce2;
    bm24_work_input input;
    bm24_stratum_job_view(job, &input);
    bm24_work work;
    bm24_work_status status = bm24_work_build(
        &input, session->subscription.extranonce1_hex,
        session->extranonce2, session->subscription.extranonce2_size,
        &work);
    if (status != BM24_WORK_OK) {
        char error[96];
        snprintf(error, sizeof(error), "Jobaufbereitung: %s",
                 bm24_work_status_string(status));
        stats_error(error);
        return false;
    }

    uint32_t tag = session->active_tag + 1;
    if (tag == 0)
        tag = 1;
    bm24_miner_job miner_job = {
        .tag = tag,
        .pool_difficulty = session->difficulty
    };
    memcpy(miner_job.header, work.header, sizeof(miner_job.header));
    memcpy(miner_job.network_target_le, work.network_target_le,
           sizeof(miner_job.network_target_le));
    if (!bm24_miner_set_job(&miner_job)) {
        stats_error("Miner hat Pooljob abgelehnt");
        return false;
    }

    session->active_job = *job;
    strlcpy(session->active_extranonce2, work.extranonce2_hex,
            sizeof(session->active_extranonce2));
    session->active_tag = tag;
    session->last_job_ms = now_ms();
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.active_job_tag = tag;
    s_stats.difficulty = session->difficulty;
    ++s_stats.jobs;
    portEXIT_CRITICAL(&s_stats_lock);
    ESP_LOGI(TAG, "Job %" PRIu32 " aktiv, Pool-Diff %.10g",
             tag, session->difficulty);
    return true;
}

static void remember_submit(pool_session *session, uint32_t id,
                            bool network_block)
{
    pending_submit *slot =
        &session->submits[id % BM24_PENDING_SUBMITS];
    slot->used = true;
    slot->network_block = network_block;
    slot->id = id;
}

static bool take_submit(pool_session *session, uint32_t id,
                        bool *network_block)
{
    pending_submit *slot =
        &session->submits[id % BM24_PENDING_SUBMITS];
    if (!slot->used || slot->id != id)
        return false;
    if (network_block)
        *network_block = slot->network_block;
    slot->used = false;
    return true;
}

static bool submit_share(pool_session *session,
                         const bm24_miner_share *share)
{
    if (share->job_tag != session->active_tag) {
        portENTER_CRITICAL(&s_stats_lock);
        ++s_stats.stale;
        portEXIT_CRITICAL(&s_stats_lock);
        return true;
    }

    uint32_t id = ++session->next_request_id;
    if (id < 10)
        id = session->next_request_id = 10;
    char wire[BM24_WIRE_MAX];
    size_t length = bm24_stratum_write_submit(
        wire, sizeof(wire), id, s_config.worker,
        session->active_job.job_id, session->active_extranonce2,
        session->active_job.ntime_hex, share->nonce);
    if (!length || !transport_write_all(session->transport, wire, length))
        return false;
    remember_submit(session, id, share->network_block);
    portENTER_CRITICAL(&s_stats_lock);
    ++s_stats.submitted;
    if (share->difficulty > s_stats.best_difficulty)
        s_stats.best_difficulty = share->difficulty;
    portEXIT_CRITICAL(&s_stats_lock);
    ESP_LOGI(TAG,
             "Share TX id=%" PRIu32 " diff=%.8g nonce=%08" PRIx32 "%s",
             id, share->difficulty, share->nonce,
             share->network_block ? " NETZWERK-BLOCK" : "");
    return true;
}

static bool process_response(pool_session *session,
                             const bm24_stratum_message *message)
{
    if (message->id == 1) {
        if (!message->response_ok ||
            message->subscription.extranonce1_hex[0] == '\0' ||
            message->subscription.extranonce2_size == 0) {
            stats_error("Pool hat Subscribe abgelehnt");
            return false;
        }
        session->subscription = message->subscription;
        session->has_subscription = true;
        portENTER_CRITICAL(&s_stats_lock);
        s_stats.subscribed = true;
        portEXIT_CRITICAL(&s_stats_lock);
        if (session->has_pending_job)
            activate_work(session, &session->pending_job, true);
        return true;
    }
    if (message->id == 2) {
        if (!message->response_ok) {
            stats_error("Pool hat Worker-Autorisierung abgelehnt");
            return false;
        }
        portENTER_CRITICAL(&s_stats_lock);
        s_stats.authorized = true;
        portEXIT_CRITICAL(&s_stats_lock);
        return true;
    }
    bool network_block = false;
    if (take_submit(session, message->id, &network_block)) {
        portENTER_CRITICAL(&s_stats_lock);
        if (message->response_ok) {
            ++s_stats.accepted;
            if (network_block)
                ++s_stats.found_blocks;
        } else {
            ++s_stats.rejected;
        }
        portEXIT_CRITICAL(&s_stats_lock);
        if (network_block && message->response_ok)
            ESP_LOGW(TAG, "BLOCK GEFUNDEN UND VOM POOL BESTÄTIGT, id=%"
                     PRIu32, message->id);
        else
            ESP_LOGI(TAG, "Share id=%" PRIu32 " %s", message->id,
                     message->response_ok ? "AKZEPTIERT" : "ABGELEHNT");
    }
    return true;
}

static bool process_line(pool_session *session, const char *line)
{
    if (!bm24_stratum_parse_line(line, &s_message) ||
        s_message.type == BM24_STRATUM_MSG_INVALID) {
        portENTER_CRITICAL(&s_stats_lock);
        ++s_stats.protocol_errors;
        portEXIT_CRITICAL(&s_stats_lock);
        stats_error("Ungültige Stratum-Nachricht");
        return true; /* Eine kaputte Zeile beendet nicht sofort die Session. */
    }

    switch (s_message.type) {
    case BM24_STRATUM_MSG_NOTIFY:
        session->pending_job = s_message.job;
        session->has_pending_job = true;
        if (session->has_subscription &&
            !activate_work(session, &s_message.job, true))
            return false;
        break;
    case BM24_STRATUM_MSG_SET_DIFFICULTY:
        session->difficulty = s_message.difficulty;
        portENTER_CRITICAL(&s_stats_lock);
        s_stats.difficulty = session->difficulty;
        portEXIT_CRITICAL(&s_stats_lock);
        if (session->active_tag &&
            !activate_work(session, &session->active_job, false))
            return false;
        break;
    case BM24_STRATUM_MSG_SET_EXTRANONCE:
        session->subscription = s_message.subscription;
        session->has_subscription = true;
        session->extranonce2 = 0;
        if (session->has_pending_job &&
            !activate_work(session, &session->pending_job, true))
            return false;
        break;
    case BM24_STRATUM_MSG_RESPONSE:
        return process_response(session, &s_message);
    case BM24_STRATUM_MSG_UNKNOWN:
    default:
        break;
    }
    return true;
}

static esp_tls_t *connect_pool(void)
{
    if (s_config.pool_tls) {
        if (!bm24_network_sync_time(15000)) {
            stats_error("Keine sichere Zeit für TLS");
            return NULL;
        }
    }

    tls_keep_alive_cfg_t keepalive = {
        .keep_alive_enable = true,
        .keep_alive_idle = 60,
        .keep_alive_interval = 10,
        .keep_alive_count = 3
    };
    esp_tls_cfg_t config = {
        .non_block = true,
        .timeout_ms = 15000,
        .crt_bundle_attach =
            s_config.pool_tls ? esp_crt_bundle_attach : NULL,
        .is_plain_tcp = !s_config.pool_tls,
        .keep_alive_cfg = &keepalive
    };
    esp_tls_t *transport = esp_tls_init();
    if (!transport)
        return NULL;

    if (s_config.pool_tls)
        bm24_miner_set_network_window(true);
    int connected = esp_tls_conn_new_sync(
        s_config.pool_host, strlen(s_config.pool_host),
        s_config.pool_port, &config, transport);
    if (s_config.pool_tls)
        bm24_miner_set_network_window(false);
    if (connected != 1) {
        esp_tls_conn_destroy(transport);
        return NULL;
    }
    return transport;
}

static bool run_session(esp_tls_t *transport)
{
    pool_session session = {
        .transport = transport,
        .difficulty = BM24_DEFAULT_DIFFICULTY,
        .next_request_id = 9,
        .last_job_ms = now_ms()
    };
    if (!send_subscribe(&session)) {
        stats_error("Stratum-Handshake konnte nicht gesendet werden");
        return false;
    }

    size_t line_length = 0;
    for (;;) {
        /* Von aussen angeforderter Neuaufbau: die Sitzung wird verlassen,
           die Schleife im Task verbindet danach neu. */
        if (s_reconnect_requested) {
            s_reconnect_requested = false;
            ESP_LOGW(TAG, "Neuaufbau angefordert");
            return false;
        }
        bm24_miner_share share;
        while (bm24_miner_get_share(&share, 0)) {
            if (!submit_share(&session, &share))
                return false;
        }

        char chunk[512];
        if (s_config.pool_tls)
            bm24_miner_set_network_window(true);
        ssize_t received =
            esp_tls_conn_read(transport, chunk, sizeof(chunk));
        if (s_config.pool_tls)
            bm24_miner_set_network_window(false);
        if (received == 0) {
            stats_error("Pool hat Verbindung geschlossen");
            return false;
        }
        if (received < 0) {
            if (!transport_would_block(received)) {
                stats_error("Pool-Lesefehler");
                return false;
            }
        } else {
            for (ssize_t i = 0; i < received; ++i) {
                char c = chunk[i];
                if (c == '\n') {
                    s_line[line_length] = '\0';
                    if (line_length && !process_line(&session, s_line))
                        return false;
                    line_length = 0;
                } else if (c != '\r') {
                    if (line_length + 1 >= sizeof(s_line)) {
                        stats_error("Stratum-Zeile zu lang");
                        portENTER_CRITICAL(&s_stats_lock);
                        ++s_stats.protocol_errors;
                        portEXIT_CRITICAL(&s_stats_lock);
                        return false;
                    }
                    s_line[line_length++] = c;
                }
            }
        }

        if (session.active_tag &&
            (uint32_t)(now_ms() - session.last_job_ms) >
                BM24_JOB_TIMEOUT_MS) {
            stats_error("Pool liefert seit 10 Minuten keinen Job");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void pool_task(void *arg)
{
    (void)arg;
    uint32_t failures = 0;
    uint32_t low_water = UINT32_MAX;
    /* BEWUSST OHNE Task-Watchdog: dieser Task wartet zwischen zwei Jobs
       voellig regulaer minutenlang blockierend am Netz. Ein Watchdog haelt
       das faelschlich fuer einen Haenger und startet das Geraet mitten im
       Betrieb neu — genau das ist beim ersten Versuch passiert. Ueberwacht
       wird stattdessen im Supervisor, ob ueberhaupt noch Jobs eintreffen. */
    for (;;) {
        /* Kleinsten je gesehenen Stapelrest melden, wenn er neu unterboten
           wird. So faellt eine schrumpfende Reserve auf, bevor sie zum
           Ueberlauf wird. */
        uint32_t free_stack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        if (free_stack < low_water) {
            low_water = free_stack;
            ESP_LOGI(TAG, "Pool-Task: kleinster Stapelrest %" PRIu32 " Byte",
                     low_water);
        }
        if (!bm24_network_wait_connected(10000)) {
            bm24_miner_clear_job();
            continue;
        }

        ESP_LOGI(TAG, "Verbinde %s:%u%s", s_config.pool_host,
                 (unsigned)s_config.pool_port,
                 s_config.pool_tls ? " (TLS)" : "");
        esp_tls_t *transport = connect_pool();
        if (!transport) {
            stats_error("Pool-Verbindung fehlgeschlagen");
        } else {
            failures = 0;
            stats_connection(true);
            portENTER_CRITICAL(&s_stats_lock);
            s_stats.difficulty = BM24_DEFAULT_DIFFICULTY;
            portEXIT_CRITICAL(&s_stats_lock);
            run_session(transport);
            esp_tls_conn_destroy(transport);
        }

        stats_connection(false);
        bm24_miner_clear_job();
        portENTER_CRITICAL(&s_stats_lock);
        ++s_stats.reconnects;
        portEXIT_CRITICAL(&s_stats_lock);

        ++failures;
        uint32_t maximum = failures < 6 ? failures * 5u : 30u;
        uint32_t delay_s = 1u + esp_random() % maximum;
        char last_error[sizeof(s_stats.last_error)];
        portENTER_CRITICAL(&s_stats_lock);
        strlcpy(last_error, s_stats.last_error, sizeof(last_error));
        portEXIT_CRITICAL(&s_stats_lock);
        ESP_LOGW(TAG, "Reconnect in %" PRIu32 " s: %s", delay_s,
                 last_error);
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000u));
    }
}

bool bm24_pool_start(const bm24_config *config)
{
    if (s_task)
        return true;
    if (!config || bm24_config_validate(config) != BM24_CONFIG_OK)
        return false;
    s_config = *config;
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.difficulty = BM24_DEFAULT_DIFFICULTY;
    BaseType_t ok = xTaskCreatePinnedToCore(
        /* 16 KB reichten nicht: bm24_work_build legt allein 1 KB Coinbase
           auf den Stapel, dazu kommen JSON-Parser und der TLS-Unterbau.
           Das Ergebnis war ein Stapelueberlauf, sobald der erste echte Job
           eintraf — also genau nach der Einrichtung, nicht im Leerlauf.
           Der Rest wird jetzt gemeldet, damit die Reserve messbar bleibt. */
        pool_task, "bm24Pool", 28672, NULL, 4, &s_task, 1);
    return ok == pdPASS;
}

void bm24_pool_get_stats(bm24_pool_stats *out)
{
    if (!out)
        return;
    portENTER_CRITICAL(&s_stats_lock);
    memcpy(out, &s_stats, sizeof(*out));
    portEXIT_CRITICAL(&s_stats_lock);
}

/* Von aussen anstossen, dass die Pool-Sitzung neu aufgebaut wird. Wird
   genutzt, wenn ueber lange Zeit kein Job mehr eintraf: die Verbindung
   sieht dann noch offen aus, liefert aber nichts mehr. */
void bm24_pool_reconnect(void)
{
    s_reconnect_requested = true;
}
