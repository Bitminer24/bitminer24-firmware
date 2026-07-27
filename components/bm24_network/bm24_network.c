#include "bm24_network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"

#define CONNECTED_BIT BIT0
#define FORM_MAX      1024

static const char *TAG = "bm24_net";
static EventGroupHandle_t s_events;
static httpd_handle_t s_httpd;
static bm24_network_status s_status;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;

static const char PORTAL_HTML[] =
    "<!doctype html><html lang=de><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>BitMiner24 Setup</title><style>"
    "body{font:16px system-ui;background:#071018;color:#eef;max-width:34rem;"
    "margin:2rem auto;padding:0 1rem}form{display:grid;gap:.8rem}"
    "label{display:grid;gap:.25rem}input{font:inherit;padding:.7rem;"
    "border:1px solid #456;border-radius:.4rem}button{font:inherit;padding:.8rem;"
    "background:#f7931a;color:#111;border:0;border-radius:.4rem;font-weight:700}"
    "small{color:#abc}</style><h1>BitMiner24 2.0</h1>"
    "<p>ESP-IDF 5.5 Setup. Bitte WLAN und deine echte Mining-Adresse eintragen."
    "</p><form method=post action=/save>"
    "<label>WLAN-Name<input name=ssid maxlength=32 required></label>"
    "<label>WLAN-Passwort<input name=wifi_password type=password maxlength=64>"
    "<small>Leer lassen nur bei offenem WLAN.</small></label>"
    "<label>BTC-Adresse / Worker<input name=worker maxlength=128 required "
    "placeholder=\"bc1... oder Adresse.worker\"></label>"
    "<label>Pool-Host<input name=pool_host maxlength=128 "
    "value=\"public-pool.io\" required></label>"
    "<label>Pool-Port<input name=pool_port type=number min=1 max=65535 "
    "value=3333 required></label>"
    "<label>Pool-Passwort<input name=pool_password maxlength=128 value=x></label>"
    "<label><span><input name=pool_tls type=checkbox value=1> TLS verwenden"
    "</span></label><button>Speichern und starten</button></form>"
    "<p><small>Setup-WLAN ist WPA2-geschuetzt. Die Daten werden versioniert "
    "im NVS gespeichert.</small></p><hr><h2>Firmware-Update</h2>"
    "<p>Nur eine BitMiner24 <code>firmware.bin</code> auswaehlen.</p>"
    "<input id=firmware type=file accept=.bin><button type=button "
    "onclick=\"let f=document.querySelector('#firmware').files[0];"
    "if(!f)return alert('Datei waehlen');"
    "fetch('/ota',{method:'POST',headers:{'Content-Type':"
    "'application/octet-stream'},body:f}).then(r=>r.text()).then(alert)\">"
    "Update installieren</button></html>";

static void status_connected(bool connected)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.connected = connected;
    if (!connected) {
        s_status.ip[0] = '\0';
        s_status.rssi = 0;
    }
    portEXIT_CRITICAL(&s_status_lock);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id,
                       void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        status_connected(false);
        portENTER_CRITICAL(&s_status_lock);
        ++s_status.reconnects;
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        /* IDF serialisiert den Connect-Aufruf; sofortiger Retry vermeidet
           lange Mining-Pausen. Das Setup-AP wird nach dem Start-Timeout
           parallel zugeschaltet. */
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&s_status_lock);
        s_status.connected = true;
        strlcpy(s_status.ip, ip, sizeof(s_status.ip));
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupSetBits(s_events, CONNECTED_BIT);
        ESP_LOGI(TAG, "WLAN verbunden, IP %s", ip);
    }
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool url_decode(const char *in, size_t in_length, char *out,
                       size_t capacity)
{
    if (!in || !out || capacity == 0)
        return false;
    size_t used = 0;
    for (size_t i = 0; i < in_length; ++i) {
        unsigned char value;
        if (in[i] == '+') {
            value = ' ';
        } else if (in[i] == '%') {
            if (i + 2 >= in_length)
                return false;
            int hi = hex_value(in[i + 1]);
            int lo = hex_value(in[i + 2]);
            if (hi < 0 || lo < 0)
                return false;
            value = (unsigned char)((hi << 4) | lo);
            i += 2;
        } else {
            value = (unsigned char)in[i];
        }
        if (value == '\0' || used + 1 >= capacity)
            return false;
        out[used++] = (char)value;
    }
    out[used] = '\0';
    return true;
}

static bool form_value(const char *form, const char *key, char *out,
                       size_t capacity)
{
    size_t key_length = strlen(key);
    const char *cursor = form;
    while (*cursor) {
        const char *end = strchr(cursor, '&');
        if (!end)
            end = cursor + strlen(cursor);
        const char *equals = memchr(cursor, '=', (size_t)(end - cursor));
        if (equals && (size_t)(equals - cursor) == key_length &&
            memcmp(cursor, key, key_length) == 0)
            return url_decode(equals + 1, (size_t)(end - equals - 1),
                              out, capacity);
        cursor = *end ? end + 1 : end;
    }
    if (capacity)
        out[0] = '\0';
    return false;
}

static esp_err_t portal_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static esp_err_t portal_save(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= FORM_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Formular zu gross");
        return ESP_FAIL;
    }

    char *form = malloc((size_t)req->content_len + 1);
    if (!form) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Kein Speicher");
        return ESP_ERR_NO_MEM;
    }
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, form + received,
                               (size_t)(req->content_len - received));
        if (n <= 0) {
            free(form);
            return ESP_FAIL;
        }
        received += n;
    }
    form[received] = '\0';

    bm24_config config;
    bm24_config_defaults(&config);
    char port_text[8];
    char tls_text[4];
    bool decoded =
        form_value(form, "ssid", config.wifi_ssid,
                   sizeof(config.wifi_ssid)) &&
        form_value(form, "wifi_password", config.wifi_password,
                   sizeof(config.wifi_password)) &&
        form_value(form, "worker", config.worker, sizeof(config.worker)) &&
        form_value(form, "pool_host", config.pool_host,
                   sizeof(config.pool_host)) &&
        form_value(form, "pool_port", port_text, sizeof(port_text)) &&
        form_value(form, "pool_password", config.pool_password,
                   sizeof(config.pool_password));
    config.pool_tls =
        form_value(form, "pool_tls", tls_text, sizeof(tls_text)) &&
        strcmp(tls_text, "1") == 0;
    free(form);

    char *end = NULL;
    unsigned long port = decoded ? strtoul(port_text, &end, 10) : 0;
    if (!decoded || !end || *end || port == 0 || port > UINT16_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Ungueltige Formulardaten");
        return ESP_FAIL;
    }
    config.pool_port = (uint16_t)port;

    bm24_config_status status = bm24_config_save(&config);
    if (status != BM24_CONFIG_OK) {
        char message[96];
        snprintf(message, sizeof(message), "Konfiguration ungueltig: %s",
                 bm24_config_status_string(status));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><meta charset=utf-8><title>Gespeichert</title>"
        "<h1>Gespeichert</h1><p>BitMiner24 startet jetzt neu.</p>");
    xTaskCreate(restart_task, "bm24Restart", 2048, NULL, 8, NULL);
    return ESP_OK;
}

static esp_err_t portal_ota(httpd_req_t *req)
{
    const char *content_type = httpd_req_get_hdr_value_len(
        req, "Content-Type") ? "present" : NULL;
    char type[48] = {0};
    if (!content_type ||
        httpd_req_get_hdr_value_str(req, "Content-Type", type,
                                    sizeof(type)) != ESP_OK ||
        strcmp(type, "application/octet-stream") != 0 ||
        req->content_len < 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "firmware.bin als Binaerdaten erwartet");
        return ESP_FAIL;
    }

    const esp_partition_t *partition =
        esp_ota_get_next_update_partition(NULL);
    if (!partition || (size_t)req->content_len > partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Firmware passt nicht in OTA-Slot");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(partition, (size_t)req->content_len, &ota);
    char buffer[2048];
    int remaining = req->content_len;
    while (err == ESP_OK && remaining > 0) {
        int wanted = remaining < (int)sizeof(buffer)
            ? remaining : (int)sizeof(buffer);
        int received = httpd_req_recv(req, buffer, wanted);
        if (received == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (received <= 0) {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(ota, buffer, (size_t)received);
        remaining -= received;
    }
    if (err == ESP_OK && remaining == 0)
        err = esp_ota_end(ota);
    else
        esp_ota_abort(ota);
    if (err == ESP_OK) {
        esp_app_desc_t description;
        err = esp_ota_get_partition_description(partition, &description);
        if (err == ESP_OK &&
            strcmp(description.project_name, "bitminer24_firmware") != 0) {
            ESP_LOGE(TAG, "OTA-Projekt abgelehnt: %s",
                     description.project_name);
            err = ESP_ERR_INVALID_ARG;
        }
    }
    if (err == ESP_OK)
        err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA fehlgeschlagen: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA-Pruefung fehlgeschlagen");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Update geprueft. Neustart in OTA-Slot.");
    xTaskCreate(restart_task, "bm24OtaRestart", 2048, NULL, 8, NULL);
    return ESP_OK;
}

static bool start_http_portal(void)
{
    if (s_httpd)
        return true;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &config) != ESP_OK)
        return false;

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = portal_get
    };
    const httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = portal_save
    };
    const httpd_uri_t ota = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = portal_ota
    };
    if (httpd_register_uri_handler(s_httpd, &root) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &save) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &ota) != ESP_OK) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        return false;
    }
    return true;
}

static bool start_setup_ap(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[33];
    snprintf(ssid, sizeof(ssid), "BitMiner24-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, BM24_SETUP_PASSWORD,
            sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = true;

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK)
        return false;
    if (mode == WIFI_MODE_STA)
        mode = WIFI_MODE_APSTA;
    else if (mode == WIFI_MODE_NULL)
        mode = WIFI_MODE_AP;
    if (esp_wifi_set_mode(mode) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK)
        return false;

    /* Wenn Wi-Fi bereits als STA lief, aktiviert set_mode(APSTA) das AP
       unmittelbar; vor esp_wifi_start() wird es beim Start aktiviert. */
    portENTER_CRITICAL(&s_status_lock);
    s_status.portal_active = true;
    strlcpy(s_status.setup_ssid, ssid, sizeof(s_status.setup_ssid));
    portEXIT_CRITICAL(&s_status_lock);

    bool ok = start_http_portal();
    ESP_LOGW(TAG, "Setup: WLAN %s, Passwort %s, http://192.168.4.1",
             ssid, BM24_SETUP_PASSWORD);
    return ok;
}

bool bm24_network_open_portal(void)
{
    if (!s_initialized)
        return false;
    return start_setup_ap();
}

bool bm24_network_start(const bm24_config *config, uint32_t timeout_ms)
{
    if (s_initialized)
        return bm24_network_wait_connected(timeout_ms);
    s_events = xEventGroupCreate();
    if (!s_events)
        return false;

    if (esp_netif_init() != ESP_OK ||
        esp_event_loop_create_default() != ESP_OK)
        return false;
    if (!esp_netif_create_default_wifi_sta() ||
        !esp_netif_create_default_wifi_ap())
        return false;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK)
        return false;
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   wifi_event, NULL) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   wifi_event, NULL) != ESP_OK)
        return false;
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK)
        return false;

    bool provisioned = bm24_config_is_provisioned(config);
    if (provisioned) {
        wifi_config_t station = {0};
        /* Die IDF-Felder sind 32/64 Byte breit und duerfen bei maximaler
           Laenge ohne abschliessende Null belegt sein. strlcpy wuerde hier
           eine gueltige 32-Byte-SSID bzw. 64-Byte-PSK abschneiden. */
        memcpy(station.sta.ssid, config->wifi_ssid,
               strlen(config->wifi_ssid));
        memcpy(station.sta.password, config->wifi_password,
               strlen(config->wifi_password));
        station.sta.threshold.authmode = WIFI_AUTH_OPEN;
        station.sta.pmf_cfg.capable = true;
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
            esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK)
            return false;
    } else {
        if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
            !start_setup_ap())
            return false;
    }

    if (esp_wifi_start() != ESP_OK)
        return false;
    s_initialized = true;

    if (!provisioned)
        return false;
    if (esp_wifi_connect() != ESP_OK)
        ESP_LOGW(TAG, "Erster WLAN-Connect konnte nicht gestartet werden");
    if (bm24_network_wait_connected(timeout_ms))
        return true;

    ESP_LOGW(TAG, "WLAN-Timeout; Setup-AP wird parallel gestartet");
    start_setup_ap();
    return false;
}

bool bm24_network_wait_connected(uint32_t timeout_ms)
{
    if (!s_events)
        return false;
    EventBits_t bits = xEventGroupWaitBits(
        s_events, CONNECTED_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & CONNECTED_BIT) != 0;
}

void bm24_network_get_status(bm24_network_status *out)
{
    if (!out)
        return;
    portENTER_CRITICAL(&s_status_lock);
    memcpy(out, &s_status, sizeof(*out));
    portEXIT_CRITICAL(&s_status_lock);

    if (out->connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            out->rssi = ap.rssi;
    }
}
