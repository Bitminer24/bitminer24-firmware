#ifndef BM24_NETWORK_H
#define BM24_NETWORK_H

#include <stdbool.h>
#include <stdint.h>

#include "bm24_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_SETUP_PASSWORD "MineYourCoins"

typedef struct {
    bool connected;
    bool portal_active;
    int8_t rssi;
    uint32_t reconnects;
    char ip[16];
    char setup_ssid[33];
} bm24_network_status;

/* Initialisiert Netif, Eventloop und Wi-Fi. Mit gueltiger Konfiguration wird
   bis zu timeout_ms auf eine Station-IP gewartet. Ohne Konfiguration oder
   nach Timeout bleibt ein WPA2-Setup-AP mit HTTP-Portal aktiv. */
bool bm24_network_start(const bm24_config *config, uint32_t timeout_ms);
bool bm24_network_wait_connected(uint32_t timeout_ms);
bool bm24_network_open_portal(void);
void bm24_network_get_status(bm24_network_status *out);

#ifdef __cplusplus
}
#endif

#endif /* BM24_NETWORK_H */
