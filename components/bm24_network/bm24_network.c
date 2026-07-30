#include "bm24_network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "bm24_dashboard_html.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"

#define CONNECTED_BIT BIT0
#define FORM_MAX      1024

static const char *TAG = "bm24_net";
static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_time_lock;
static httpd_handle_t s_httpd;
static TaskHandle_t s_dns_task;
static bm24_network_status s_status;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static bool s_sntp_started;

/* Das Formular wird in drei Stuecken gesendet, weil dazwischen die
   Ergebnisse des WLAN-Scans eingesetzt werden. Ohne Auswahlliste musste der
   Name von Hand getippt werden — der haeufigste Einrichtungsfehler. */
/* Oeffentliche Testadresse (Genesis-Coinbase). Sie erlaubt einen sofortigen
   Funktionstest ohne Tipparbeit, gehoert aber niemandem im Zugriff — das
   Formular weist deshalb deutlich darauf hin. Die Sperre gegen die
   Burn-Adresse in bm24_config bleibt davon unberuehrt. */
#define BM24_DEFAULT_WORKER "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa"

static const char PORTAL_HEAD[] =
    "<!doctype html><html lang=de><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<meta name=theme-color content=\"#050607\">"
    "<title>BitMiner24 | Einrichtung</title><style>" BM24_WEB_STYLE
    ".setup-hero{display:grid;grid-template-columns:1fr auto;gap:22px;"
    "align-items:end;margin:27px 0 14px;padding:28px 30px}.setup-hero>*{min-width:0}"
    ".setup-hero h1{margin:0;overflow-wrap:anywhere;"
    "font-size:clamp(27px,5vw,44px);line-height:1.03;letter-spacing:-.035em}"
    ".setup-hero h1 span{color:var(--orange)}.setup-hero p:not(.eyebrow){"
    "max-width:620px;margin:12px 0 0;color:var(--muted)}.steps{display:grid;"
    "grid-template-columns:repeat(3,1fr);gap:6px;min-width:260px}.steps span{"
    "padding:8px;border:1px solid var(--line);border-radius:6px;color:var(--muted);"
    "font-size:9px;font-weight:850;text-align:center;text-transform:uppercase;"
    "letter-spacing:.08em}.steps span:first-child{border-color:var(--line-hot);"
    "color:var(--orange);background:rgba(255,138,0,.07)}.setup-form{display:grid;"
    "gap:12px}.form-section{padding:24px}.section-title{display:flex;align-items:center;"
    "gap:13px;margin-bottom:20px}.section-title h2{margin:0;font-size:20px}"
    ".section-title p{margin:2px 0 0;color:var(--muted);font-size:12px}.step-no{"
    "display:grid;place-items:center;width:39px;height:39px;border:1px solid var(--line-hot);"
    "border-radius:8px;color:var(--orange);font:900 12px/1 ui-monospace,monospace;"
    "background:rgba(255,138,0,.06)}.fields{display:grid;grid-template-columns:"
    "repeat(2,minmax(0,1fr));gap:15px}.field{display:grid;align-content:start;gap:6px}"
    ".field.full{grid-column:1/-1}.label{color:#dce1e4;font-size:12px;font-weight:750}"
    "input:not([type=checkbox]):not([type=range]),select{width:100%;min-height:46px;"
    "padding:0 13px;border:1px solid var(--line);border-radius:7px;outline:none;"
    "background:#07090b;color:var(--text);font:14px ui-sans-serif,system-ui}"
    "input:focus,select:focus{border-color:var(--orange);box-shadow:0 0 0 3px "
    "rgba(255,138,0,.09)}input[type=range]{width:100%;accent-color:var(--orange)}"
    ".check{display:flex;align-items:center;gap:9px;min-height:46px;padding:0 12px;"
    "border:1px solid var(--line);border-radius:7px;background:#07090b}"
    ".check input{width:17px;height:17px;accent-color:var(--orange)}"
    ".hint{color:var(--muted);font-size:10px}.warn{margin:15px 0 0;padding:12px 14px;"
    "border-left:3px solid var(--orange);border-radius:4px;background:rgba(255,138,0,.07);"
    "color:#d5d9dc;font-size:11px}.submit-row{display:flex;align-items:center;"
    "justify-content:space-between;gap:14px;margin-top:3px;padding:20px 24px}"
    ".submit-row p{margin:0;color:var(--muted);font-size:11px}.submit-row .button{"
    "min-width:230px}.tools{display:grid;grid-template-columns:1fr auto;gap:18px;"
    "align-items:center;padding:24px}.tools h2{margin:0 0 5px;font-size:20px}"
    ".tools p{margin:0;color:var(--muted);font-size:12px}"
    ".setup-commerce{margin-top:28px}.setup-commerce .offer{min-height:165px}"
    "@media(max-width:760px){.setup-hero{grid-template-columns:minmax(0,1fr);padding:23px}"
    ".steps{min-width:0}.fields{grid-template-columns:1fr}.field.full{grid-column:auto}"
    ".tools{grid-template-columns:1fr}.submit-row{align-items:stretch;flex-direction:column}"
    ".submit-row .button{width:100%}}"
    "</style></head><body><header class=\"topbar wrap\">" BM24_WEB_BRAND
    "<div class=top-actions><span class=chip>ERSTEINRICHTUNG</span>"
    "<span class=\"state ok\">NERDMINER AP</span></div></header><main class=wrap>"
    "<section class=\"panel setup-hero\"><div><p class=eyebrow>BitMiner24 Setup</p>"
    "<h1>IN DREI SCHRITTEN <span>STARTKLAR.</span></h1>"
    "<p>Verbinde deinen Nerdminer V2 mit dem Heimnetz und hinterlege die "
    "Bitcoin-Adresse für einen möglichen Solo-Blockfund.</p></div>"
    "<div class=steps><span>01 WLAN</span><span>02 Mining</span>"
    "<span>03 Anzeige</span></div></section>"
    "<form class=setup-form method=post action=/save>"
    "<section class=\"panel form-section\"><div class=section-title>"
    "<span class=step-no>01</span><div><h2>WLAN verbinden</h2>"
    "<p>Wähle dein 2,4-GHz-Heimnetz aus.</p></div></div><div class=fields>"
    "<label class=\"field full\"><span class=label>WLAN-Netzwerk</span>"
    "<select name=ssid required>";

static const char PORTAL_TAIL_BEFORE_PASSWORD[] =
    "</select><span class=hint>Die Liste wurde direkt vom Nerdminer gescannt.</span>"
    "</label><label class=\"field full\"><span class=label>WLAN-Passwort</span>"
    "<input name=wifi_password type=password maxlength=64 autocomplete=current-password "
    "placeholder=\"Passwort des Heimnetzes\"><span class=hint>Nur bei einem offenen "
    "WLAN leer lassen.</span></label></div></section>"
    "<section class=\"panel form-section\"><div class=section-title>"
    "<span class=step-no>02</span><div><h2>Solo Mining</h2>"
    "<p>Adresse und Pool für deine Shares.</p></div></div><div class=fields>"
    "<label class=\"field full\"><span class=label>BTC-Adresse / Worker</span>"
    "<input name=worker maxlength=128 required spellcheck=false value=\""
    BM24_DEFAULT_WORKER "\"><span class=hint>Hierhin zahlt der Solo-Pool einen "
    "möglichen Blockfund aus.</span></label>"
    "<label class=field><span class=label>Pool-Host</span>"
    "<input name=pool_host maxlength=128 value=\"public-pool.io\" required "
    "spellcheck=false></label><label class=field><span class=label>Pool-Port</span>"
    "<input name=pool_port type=number min=1 max=65535 value=3333 required></label>"
    "<label class=field><span class=label>Pool-Passwort</span>"
    "<input name=pool_password maxlength=128 value=x></label>"
    "<label class=field><span class=label>Verbindung</span><span class=check>"
    "<input name=pool_tls type=checkbox value=1> TLS verwenden</span></label>"
    "</div><p class=warn><strong>Wichtig:</strong> Voreingetragen ist eine "
    "öffentliche Testadresse. Vor dem Dauerbetrieb durch deine eigene "
    "Bitcoin-Adresse ersetzen, sonst geht ein Fund nicht an dich.</p></section>"
    "<section class=\"panel form-section\"><div class=section-title>"
    "<span class=step-no>03</span><div><h2>Anzeige</h2>"
    "<p>Display passend zu deinem Standort einstellen.</p></div></div>"
    "<div class=fields><label class=field><span class=label>Helligkeit</span>"
    "<input name=brightness type=range min=10 max=255 value=130></label>"
    "<label class=field><span class=label>Darstellung</span><span class=check>"
    "<input name=invert type=checkbox value=1> Helles Thema</span></label>"
    "<label class=\"field full\"><span class=label>Zeitzone</span>"
    "<select name=timezone>"
    "<option value=1 selected>Deutschland, Österreich, Schweiz (Sommerzeit)"
    "</option>"
    "<option value=0>UTC</option><option value=2>UTC+2</option>"
    "<option value=-5>UTC-5</option><option value=-8>UTC-8</option>"
    "</select><span class=hint>Nur die erste Wahl stellt Sommer- und Winterzeit "
    "automatisch um.</span></label></div></section>"
    "<section class=\"panel submit-row\"><p>Die Daten werden versioniert und "
    "lokal im geschützten Gerätespeicher abgelegt.</p>"
    "<button class=\"button primary\" type=submit>Speichern &amp; Mining starten"
    "</button></section></form>"
    "<div class=section-head><div><p class=eyebrow>Wartung</p>"
    "<h2>Firmware aktualisieren</h2></div></div>"
    "<section class=\"panel tools\"><div><h2>BitMiner24 Web-Updater</h2>"
    "<p>Nerdminer per USB-C mit einem Computer verbinden und den Web-Updater "
    "in Chrome, Edge, Brave oder Opera öffnen.</p></div>"
    "<a class=\"button primary\" target=_blank rel=noopener href=\""
    BM24_WEB_UPDATER "\">Web-Updater öffnen</a>"
    "</section><section class=\"panel tools\"><div><h2>Gerätepasswort</h2>"
    "<p>Für geschützte Änderungen im Heimnetz: <code>";

static const char PORTAL_TAIL_AFTER_PASSWORD[] =
    "</code>. Der Benutzername ist beliebig.</p></div></section>"
    "<section class=setup-commerce><div class=section-head><div>"
    "<p class=eyebrow>Wenn dein Miner läuft</p><h2>Dein BitMiner24-Setup</h2>"
    "</div><p>Links benötigen nach der Einrichtung eine Internetverbindung.</p>"
    "</div><div class=commerce>"
    "<a class=\"offer featured\" target=_blank rel=noopener href=\""
    BM24_SHOP_NERDNOS "\"><span class=offer-tag>Nächste Leistungsstufe</span>"
    "<h3>NerdNOS</h3><p>Kompakter ASIC-Solo-Miner für deutlich mehr Hashes "
    "pro Sekunde.</p><span class=offer-cta>Mehr erfahren <span>&rarr;</span>"
    "</span></a><a class=offer target=_blank rel=noopener href=\""
    BM24_GUIDES "\"><span class=offer-tag>Einfach erklärt</span>"
    "<h3>Einrichtung &amp; Wissen</h3><p>Guides zu WLAN, Wallet, Pool und "
    "Solo Mining.</p><span class=offer-cta>Guides öffnen <span>&rarr;</span>"
    "</span></a><a class=offer target=_blank rel=noopener href=\""
    BM24_SUPPORT "\"><span class=offer-tag>Aus Stuttgart</span>"
    "<h3>BitMiner24 Support</h3><p>Direkte Hilfe, wenn bei Einrichtung oder "
    "Betrieb etwas hakt.</p><span class=offer-cta>Support kontaktieren "
    "<span>&rarr;</span></span></a></div></section></main>"
    "<footer class=\"footer wrap\"><span>Setup-WLAN NerdminerAP &middot; "
    "WPA2-geschützt</span><span>BitMiner24 Firmware 2.0</span></footer>"
    "</body></html>";

static bool start_http_portal(void);   /* definiert weiter unten */
static bm24_status_provider s_status_provider;

void bm24_network_set_status_provider(bm24_status_provider provider)
{
    s_status_provider = provider;
}

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
        /* Im Heimnetz erreichbar machen: der Webserver bleibt an und
           zeigt das Dashboard unter der vergebenen IP, die auch auf dem
           Display steht.
           bitminer24.local waere schoener, mDNS liegt seit IDF 5 aber in
           der Komponenten-Registry, und der PlatformIO-Build loest
           verwaltete Komponenten hier nicht auf. Nachrusten, sobald der
           Build ueber idf.py laeuft oder die Komponente mitgeliefert wird. */
        start_http_portal();
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

/* HTML-Sonderzeichen entschaerfen: WLAN-Namen sind Fremdeingaben. */
static void html_escape(const char *in, char *out, size_t capacity)
{
    size_t w = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && w + 7 < capacity; ++p) {
        const char *rep = NULL;
        switch (*p) {
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '&': rep = "&amp;"; break;
        case '"': rep = "&quot;"; break;
        default: break;
        }
        if (rep) { size_t n = strlen(rep); memcpy(out + w, rep, n); w += n; }
        else if (*p >= 0x20) out[w++] = (char)*p;
    }
    out[w] = 0;
}

/* Umgebende Netze auflisten. Laeuft im APSTA-Modus, damit das Setup-WLAN
   waehrend des Scans erreichbar bleibt. */
static esp_err_t send_scan_options(httpd_req_t *req)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_AP)
        esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_scan_config_t scan = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan, true) != ESP_OK)
        return httpd_resp_sendstr_chunk(req,
            "<option value=\"\">Scan fehlgeschlagen</option>");

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found > 20) found = 20;
    wifi_ap_record_t *records = calloc(found ? found : 1, sizeof(*records));
    if (!records) {
        esp_wifi_scan_stop();
        return httpd_resp_sendstr_chunk(req,
            "<option value=\"\">Kein Speicher</option>");
    }
    esp_wifi_scan_get_ap_records(&found, records);

    char safe[100], line[256];
    for (uint16_t i = 0; i < found; ++i) {
        if (records[i].ssid[0] == 0)
            continue;
        html_escape((const char *)records[i].ssid, safe, sizeof(safe));
        snprintf(line, sizeof(line),
                 "<option value=\"%s\">%s (%d dBm)</option>",
                 safe, safe, records[i].rssi);
        httpd_resp_sendstr_chunk(req, line);
    }
    free(records);
    if (!found)
        httpd_resp_sendstr_chunk(req,
            "<option value=\"\">Kein WLAN gefunden</option>");
    return ESP_OK;
}

/* JSON fuer das Dashboard. Ohne angemeldeten Lieferanten bleibt es leer,
   statt zu raten. */
/* Schreibende Zugriffe absichern.

   Im Setup-WLAN genuegt der WPA2-Schluessel: wer dort ist, hat sich bereits
   ausgewiesen. Im Heimnetz ist das anders — dort ist der Webserver fuer
   jedes Geraet im Netz erreichbar, und ohne Pruefung koennte jeder die
   Wallet-Adresse aendern. Deshalb verlangt /save dort HTTP-Basic-Auth mit
   einem je Geraet aus der MAC abgeleiteten Passwort. */
static const char *device_web_password(void)
{
    static char password[16];
    if (!password[0]) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf(password, sizeof(password), "bm24-%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
    }
    return password;
}

static bool write_access_allowed(httpd_req_t *req)
{
    bool portal;
    portENTER_CRITICAL(&s_status_lock);
    portal = s_status.portal_active;
    portEXIT_CRITICAL(&s_status_lock);
    if (portal)
        return true;

    char header[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header,
                                    sizeof(header)) != ESP_OK)
        return false;
    if (strncmp(header, "Basic ", 6) != 0)
        return false;

    unsigned char decoded[96];
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                              (const unsigned char *)header + 6,
                              strlen(header + 6)) != 0)
        return false;
    decoded[decoded_len] = 0;

    const char *colon = strchr((const char *)decoded, 0x3A);
    if (!colon)
        return false;
    /* Benutzername ist beliebig, entscheidend ist das Passwort. */
    return strcmp(colon + 1, device_web_password()) == 0;
}

static esp_err_t deny_write(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate",
                       "Basic realm=\"BitMiner24\"");
    httpd_resp_sendstr(req,
        "Anmeldung nötig. Das Gerätepasswort steht auf der "
        "Einrichtungsseite und im seriellen Protokoll.");
    return ESP_OK;
}

static esp_err_t status_get(httpd_req_t *req)
{
    char json[512];
    json[0] = 0;
    if (s_status_provider)
        s_status_provider(json, sizeof(json));
    if (!json[0])
        strlcpy(json, "{}", sizeof(json));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t dashboard_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, DASHBOARD_HTML);
}

static esp_err_t setup_get(httpd_req_t *req);

/* Im Heimnetz zeigt / das Dashboard, im Setup-WLAN das Formular. So bleibt
   der gewohnte Weg ueber 192.168.4.1 erhalten. */
static esp_err_t root_get(httpd_req_t *req)
{
    bool portal;
    portENTER_CRITICAL(&s_status_lock);
    portal = s_status.portal_active;
    portEXIT_CRITICAL(&s_status_lock);
    return portal ? setup_get(req) : dashboard_get(req);
}

static esp_err_t setup_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, PORTAL_HEAD);
    send_scan_options(req);
    httpd_resp_sendstr_chunk(req, PORTAL_TAIL_BEFORE_PASSWORD);
    httpd_resp_sendstr_chunk(req, device_web_password());
    httpd_resp_sendstr_chunk(req, PORTAL_TAIL_AFTER_PASSWORD);
    return httpd_resp_sendstr_chunk(req, NULL);
}

/* Captive Portal: jede unbekannte Adresse wird auf das Formular umgeleitet.
   Erst dadurch oeffnet das Betriebssystem die Anmeldeseite von selbst,
   so wie man es vom alten WiFiManager kennt. */
/* Winziger DNS-Server: beantwortet jede A-Anfrage mit der eigenen Adresse.
   Das ist der Teil, der ein Setup-WLAN zum Captive Portal macht; Android,
   iOS und Windows pruefen nach dem Verbinden eine bekannte URL und zeigen
   die Anmeldeseite nur, wenn die Antwort umgeleitet wird. */
static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS-Socket fehlgeschlagen");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "DNS-Bind fehlgeschlagen");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t packet[256];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, packet, sizeof(packet), 0,
                           (struct sockaddr *)&from, &from_len);
        /* Kleinste sinnvolle Anfrage: 12 Byte Kopf + Name + Typ/Klasse.
           Alles darunter oder ohne Platz fuer die Antwort wird verworfen. */
        if (len < 12 + 5 || len + 16 > (int)sizeof(packet))
            continue;

        packet[2] = 0x84;   /* Antwort, autoritativ */
        packet[3] = 0x00;
        packet[7] = packet[5];   /* so viele Antworten wie Fragen */

        uint8_t *answer = packet + len;
        *answer++ = 0xC0; *answer++ = 0x0C;          /* Zeiger auf den Namen */
        *answer++ = 0x00; *answer++ = 0x01;          /* Typ A               */
        *answer++ = 0x00; *answer++ = 0x01;          /* Klasse IN           */
        *answer++ = 0x00; *answer++ = 0x00;
        *answer++ = 0x00; *answer++ = 0x3C;          /* TTL 60 s            */
        *answer++ = 0x00; *answer++ = 0x04;          /* Laenge 4            */
        *answer++ = 192; *answer++ = 168;
        *answer++ = 4;   *answer++ = 1;

        sendto(sock, packet, len + 16, 0,
               (struct sockaddr *)&from, from_len);
    }
}

static esp_err_t portal_redirect(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;
    bool portal;
    portENTER_CRITICAL(&s_status_lock);
    portal = s_status.portal_active;
    portEXIT_CRITICAL(&s_status_lock);
    if (portal) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "Nicht gefunden");
    }
    return ESP_OK;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static esp_err_t portal_save(httpd_req_t *req)
{
    if (!write_access_allowed(req))
        return deny_write(req);
    if (req->content_len <= 0 || req->content_len >= FORM_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Formular zu groß");
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
    config.invert_colors =
        form_value(form, "invert", tls_text, sizeof(tls_text)) &&
        strcmp(tls_text, "1") == 0;

    /* Anzeigewerte sind Komfort, kein Muss: fehlen oder stoeren sie, bleibt
       es bei den Vorgaben, statt die ganze Einrichtung abzulehnen. */
    char text[8];
    if (form_value(form, "brightness", text, sizeof(text))) {
        unsigned long value = strtoul(text, NULL, 10);
        if (value >= 10 && value <= 255)
            config.brightness = (uint8_t)value;
    }
    if (form_value(form, "timezone", text, sizeof(text))) {
        long value = strtol(text, NULL, 10);
        if (value >= -12 && value <= 14)
            config.timezone_offset = (int8_t)value;
    }
    free(form);

    char *end = NULL;
    unsigned long port = decoded ? strtoul(port_text, &end, 10) : 0;
    if (!decoded || !end || *end || port == 0 || port > UINT16_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Ungültige Formulardaten");
        return ESP_FAIL;
    }
    config.pool_port = (uint16_t)port;

    bm24_config_status status = bm24_config_save(&config);
    if (status != BM24_CONFIG_OK) {
        char message[96];
        snprintf(message, sizeof(message), "Konfiguration ungültig: %s",
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

static bool start_http_portal(void)
{
    if (s_httpd)
        return true;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &config) != ESP_OK)
        return false;

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get
    };
    const httpd_uri_t setup_page = {
        .uri = "/setup",
        .method = HTTP_GET,
        .handler = setup_get
    };
    const httpd_uri_t status_page = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get
    };
    const httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = portal_save
    };
    if (httpd_register_uri_handler(s_httpd, &root) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &setup_page) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &status_page) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &save) != ESP_OK) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        return false;
    }

    /* Jede unbekannte Adresse landet auf dem Formular. Zusammen mit dem
       DNS-Umleiter ergibt das das gewohnte Verhalten: WLAN auswaehlen,
       Anmeldeseite oeffnet sich von allein. */
    /* Nur waehrend der Einrichtung umleiten; im Heimnetz soll ein Tippfehler
       ein ehrliches 404 liefern statt endlos aufs Dashboard zu springen. */
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, portal_redirect);
    bool portal_active;
    portENTER_CRITICAL(&s_status_lock);
    portal_active = s_status.portal_active;
    portEXIT_CRITICAL(&s_status_lock);
    if (portal_active && !s_dns_task)
        xTaskCreate(dns_task, "bm24dns", 3072, NULL, 4, &s_dns_task);
    return true;
}

static bool start_setup_ap(void)
{
    const char *ssid = BM24_SETUP_SSID;

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
    ESP_LOGW(TAG, "Gerätepasswort für Änderungen im Heimnetz: %s",
             device_web_password());
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
    s_time_lock = xSemaphoreCreateMutex();
    if (!s_events || !s_time_lock)
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
    /* Das Dashboard muss auch im Heimnetz über die Geräte-IP erreichbar
       sein. Im Einrichtungsmodus läuft derselbe Server bereits. */
    if (!start_http_portal())
        return false;

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

bool bm24_network_sync_time(uint32_t timeout_ms)
{
    time_t now;
    time(&now);
    if (now >= 1704067200)
        return true;
    if (!s_time_lock ||
        xSemaphoreTake(s_time_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return false;

    bool ok = false;
    time(&now);
    if (now >= 1704067200) {
        ok = true;
    } else {
        if (!s_sntp_started) {
            esp_sntp_config_t config =
                ESP_NETIF_SNTP_DEFAULT_CONFIG("europe.pool.ntp.org");
            if (esp_netif_sntp_init(&config) == ESP_OK)
                s_sntp_started = true;
        }
        if (s_sntp_started &&
            esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK)
            ok = true;
    }
    xSemaphoreGive(s_time_lock);
    return ok;
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
