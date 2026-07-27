#ifndef BM24_CONFIG_H
#define BM24_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version 2: Anzeige-Einstellungen und Zeitzone kamen dazu. Aeltere
   Datensaetze werden beim Laden angehoben, statt sie zu verwerfen — sonst
   verliert ein Kunde beim Update seine Zugangsdaten. */
#define BM24_CONFIG_SCHEMA_VERSION 2u
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

    /* Anzeige und Ortszeit, in 1.x einstellbar und beim Neubau
       zunaechst fest verdrahtet. */
    uint8_t brightness;      /* 10..255, Hintergrundbeleuchtung          */
    bool invert_colors;      /* helles Thema statt dunkel               */
    int8_t timezone_offset;  /* Stunden zu UTC; 1 = Europe/Berlin mit
                                automatischer Sommerzeit, sonst fest    */
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

/* Betriebszaehler, die einen Neustart ueberleben sollen. In 1.x hiess das
   saveStats; ohne das steht nach jedem Stromausfall alles wieder bei null,
   und gerade die Gesamtlaufzeit und die beste Difficulty sind die Zahlen,
   an denen Besitzer ihr Geraet messen. Bewusst getrennt von der
   Konfiguration gespeichert: sie aendern sich staendig, die Konfiguration
   fast nie. */
typedef struct {
    uint64_t total_seconds;   /* Gesamtlaufzeit ueber alle Starts        */
    uint64_t accepted;        /* angenommene Shares                      */
    double best_difficulty;   /* lokale Bestmarke                        */
    uint32_t restarts;        /* Anzahl Starts                           */
} bm24_runtime_stats;

void bm24_stats_load(bm24_runtime_stats *stats);
void bm24_stats_save(const bm24_runtime_stats *stats);

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
