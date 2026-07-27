#ifndef BM24_CONFIG_H
#define BM24_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_CONFIG_SCHEMA_VERSION 1u
#define BM24_WIFI_SSID_MAX         32u
#define BM24_WIFI_PASSWORD_MAX     64u
#define BM24_POOL_HOST_MAX         128u
#define BM24_POOL_PASSWORD_MAX     128u
#define BM24_WORKER_MAX            128u

typedef struct {
    uint32_t schema_version;
    char wifi_ssid[BM24_WIFI_SSID_MAX + 1];
    char wifi_password[BM24_WIFI_PASSWORD_MAX + 1];
    char pool_host[BM24_POOL_HOST_MAX + 1];
    char pool_password[BM24_POOL_PASSWORD_MAX + 1];
    char worker[BM24_WORKER_MAX + 1];
    uint16_t pool_port;
    bool pool_tls;
} bm24_config;

typedef enum {
    BM24_CONFIG_OK = 0,
    BM24_CONFIG_NOT_FOUND,
    BM24_CONFIG_STORAGE_ERROR,
    BM24_CONFIG_BAD_SCHEMA,
    BM24_CONFIG_BAD_WIFI,
    BM24_CONFIG_BAD_POOL,
    BM24_CONFIG_BAD_WORKER
} bm24_config_status;

void bm24_config_defaults(bm24_config *config);
bm24_config_status bm24_config_validate(const bm24_config *config);
bool bm24_config_is_provisioned(const bm24_config *config);

/* Auf dem ESP32 liegt die Konfiguration als einzelner versionierter NVS-Blob.
   Host-Builds liefern BM24_CONFIG_STORAGE_ERROR und testen nur die reine
   Validierung. */
bm24_config_status bm24_config_load(bm24_config *config);
bm24_config_status bm24_config_save(const bm24_config *config);
bm24_config_status bm24_config_erase(void);

const char *bm24_config_status_string(bm24_config_status status);

#ifdef __cplusplus
}
#endif

#endif /* BM24_CONFIG_H */
