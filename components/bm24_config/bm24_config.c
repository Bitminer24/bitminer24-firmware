#include "bm24_config.h"

#include <ctype.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "nvs.h"
#endif

#define BM24_NVS_NAMESPACE "bm24"
#define BM24_NVS_CONFIG_KEY "config"
#define BM24_NVS_STATS_KEY  "stats"

static bool bounded_string(const char *value, size_t capacity, size_t *length)
{
    if (!value || capacity == 0)
        return false;
    size_t n = strnlen(value, capacity);
    if (n == capacity)
        return false;
    if (length)
        *length = n;
    return true;
}

static bool valid_pool_host(const char *host)
{
    size_t length;
    if (!bounded_string(host, BM24_POOL_HOST_MAX + 1, &length) ||
        length == 0)
        return false;

    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (!(isalnum(c) || c == '.' || c == '-' || c == ':' || c == '_'))
            return false;
    }
    return true;
}

static bool valid_worker(const char *worker)
{
    size_t length;
    if (!bounded_string(worker, BM24_WORKER_MAX + 1, &length) ||
        length < 14)
        return false;

    static const char *invalid[] = {
        "yourBtcAddress",
        "1BitcoinEaterAddressDontSendf59kuE"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        if (strcmp(worker, invalid[i]) == 0)
            return false;
    }

    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)worker[i];
        if (iscntrl(c) || isspace(c))
            return false;
    }
    return true;
}

void bm24_config_defaults(bm24_config *config)
{
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    config->schema_version = BM24_CONFIG_SCHEMA_VERSION;
    strcpy(config->pool_host, "public-pool.io");
    strcpy(config->pool_password, "x");
    config->pool_port = 3333;
    config->brightness = 130;      /* in 1.x vermessener Wert           */
    config->invert_colors = false;
    config->timezone_offset = 1;   /* Europe/Berlin mit Sommerzeit      */
}

bm24_config_status bm24_config_validate(const bm24_config *config)
{
    if (!config || config->schema_version != BM24_CONFIG_SCHEMA_VERSION)
        return BM24_CONFIG_BAD_SCHEMA;

    size_t ssid_length, password_length;
    if (!bounded_string(config->wifi_ssid, sizeof(config->wifi_ssid),
                        &ssid_length) ||
        !bounded_string(config->wifi_password, sizeof(config->wifi_password),
                        &password_length) ||
        ssid_length == 0 ||
        (password_length > 0 && password_length < 8))
        return BM24_CONFIG_BAD_WIFI;

    if (!valid_pool_host(config->pool_host) || config->pool_port == 0 ||
        !bounded_string(config->pool_password,
                        sizeof(config->pool_password), NULL))
        return BM24_CONFIG_BAD_POOL;

    if (!valid_worker(config->worker))
        return BM24_CONFIG_BAD_WORKER;
    return BM24_CONFIG_OK;
}

bool bm24_config_is_provisioned(const bm24_config *config)
{
    return bm24_config_validate(config) == BM24_CONFIG_OK;
}

#ifdef ESP_PLATFORM

bm24_config_status bm24_config_load(bm24_config *config)
{
    if (!config)
        return BM24_CONFIG_STORAGE_ERROR;

    bm24_config_defaults(config);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BM24_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return BM24_CONFIG_NOT_FOUND;
    if (err != ESP_OK)
        return BM24_CONFIG_STORAGE_ERROR;

    /* Kleinere Datensaetze sind aeltere Schemata und werden angehoben.
       Die Vorgabewerte stehen schon im Puffer, ein kuerzerer Blob
       ueberschreibt nur den vorderen Teil — die neuen Felder behalten
       damit ihre Vorgaben. Wer aus 1.x kommt, verliert so weder WLAN
       noch Adresse. */
    size_t size = sizeof(*config);
    err = nvs_get_blob(handle, BM24_NVS_CONFIG_KEY, config, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        bm24_config_defaults(config);
        return BM24_CONFIG_NOT_FOUND;
    }
    if (err != ESP_OK || size > sizeof(*config)) {
        bm24_config_defaults(config);
        return BM24_CONFIG_STORAGE_ERROR;
    }
    if (config->schema_version < BM24_CONFIG_SCHEMA_VERSION) {
        if (config->brightness == 0)
            config->brightness = 130;
        config->schema_version = BM24_CONFIG_SCHEMA_VERSION;
    }
    return bm24_config_validate(config);
}

bm24_config_status bm24_config_save(const bm24_config *config)
{
    bm24_config_status status = bm24_config_validate(config);
    if (status != BM24_CONFIG_OK)
        return status;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BM24_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return BM24_CONFIG_STORAGE_ERROR;
    err = nvs_set_blob(handle, BM24_NVS_CONFIG_KEY, config, sizeof(*config));
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK ? BM24_CONFIG_OK : BM24_CONFIG_STORAGE_ERROR;
}

bm24_config_status bm24_config_erase(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BM24_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return BM24_CONFIG_OK;
    if (err != ESP_OK)
        return BM24_CONFIG_STORAGE_ERROR;
    err = nvs_erase_key(handle, BM24_NVS_CONFIG_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK ? BM24_CONFIG_OK : BM24_CONFIG_STORAGE_ERROR;
}

#else

bm24_config_status bm24_config_load(bm24_config *config)
{
    bm24_config_defaults(config);
    return BM24_CONFIG_STORAGE_ERROR;
}

bm24_config_status bm24_config_save(const bm24_config *config)
{
    (void)config;
    return BM24_CONFIG_STORAGE_ERROR;
}

bm24_config_status bm24_config_erase(void)
{
    return BM24_CONFIG_STORAGE_ERROR;
}

#endif

const char *bm24_config_status_string(bm24_config_status status)
{
    switch (status) {
    case BM24_CONFIG_OK:            return "ok";
    case BM24_CONFIG_NOT_FOUND:     return "not found";
    case BM24_CONFIG_STORAGE_ERROR: return "storage error";
    case BM24_CONFIG_BAD_SCHEMA:    return "bad schema";
    case BM24_CONFIG_BAD_WIFI:      return "bad wifi";
    case BM24_CONFIG_BAD_POOL:      return "bad pool";
    case BM24_CONFIG_BAD_WORKER:    return "bad worker";
    default:                        return "unknown";
    }
}

/* Ab hier wieder geraeteabhaengig: die Host-Tests pruefen nur die reine
   Logik und kennen kein NVS. */
#ifdef ESP_PLATFORM

void bm24_stats_load(bm24_runtime_stats *stats)
{
    if (!stats)
        return;
    memset(stats, 0, sizeof(*stats));
    nvs_handle_t handle;
    if (nvs_open(BM24_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        return;
    size_t size = sizeof(*stats);
    if (nvs_get_blob(handle, BM24_NVS_STATS_KEY, stats, &size) != ESP_OK ||
        size != sizeof(*stats))
        memset(stats, 0, sizeof(*stats));
    nvs_close(handle);
}

void bm24_stats_save(const bm24_runtime_stats *stats)
{
    if (!stats)
        return;
    nvs_handle_t handle;
    if (nvs_open(BM24_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return;
    if (nvs_set_blob(handle, BM24_NVS_STATS_KEY, stats, sizeof(*stats)) == ESP_OK)
        nvs_commit(handle);
    nvs_close(handle);
}

#else  /* Host: Zaehler bleiben fluechtig, damit die Logik testbar bleibt */

void bm24_stats_load(bm24_runtime_stats *stats)
{
    if (stats)
        memset(stats, 0, sizeof(*stats));
}

void bm24_stats_save(const bm24_runtime_stats *stats)
{
    (void)stats;
}

#endif /* ESP_PLATFORM */
