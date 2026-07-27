#ifndef BM24_DISPLAY_H
#define BM24_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_DISPLAY_LINES 10
#define BM24_DISPLAY_LINE_MAX 47

typedef enum {
    BM24_DISPLAY_STYLE_STATUS = 0,
    BM24_DISPLAY_STYLE_BIG_VALUE,
    BM24_DISPLAY_STYLE_DASHBOARD
} bm24_display_style;

/* Freie Platzierung eines Wertes. Die Hintergrundgrafiken tragen ihre
   Beschriftungen bereits im Bild; die Werte muessen deshalb in deren
   Kaestchen sitzen statt auf festen Zeilen. x ist bei right = true die
   RECHTE Kante, wie in der 1.x-Vorlage. */
typedef struct {
    int16_t x;
    int16_t y;
    uint8_t scale;
    bool right;
} bm24_display_slot;

typedef struct {
    bm24_display_style style;
    /* Sind Plaetze gesetzt (slot_count > 0), gelten sie statt der
       Standardzeilen. */
    bm24_display_slot slot[BM24_DISPLAY_LINES];
    uint8_t slot_count;
    /* Optionale Hintergrundgrafik (320x170 RGB565, im Flash). NULL laesst
       es bei der einfarbigen Flaeche. Siehe bm24_media. */
    const uint16_t *background;
    char line[BM24_DISPLAY_LINES][BM24_DISPLAY_LINE_MAX + 1];
} bm24_display_frame;

/* Native esp_lcd/I80-Anbindung fuer das LilyGO T-Display S3. */
bool bm24_display_start(void);
void bm24_display_set(const bm24_display_frame *frame);
void bm24_display_setup(const char *ssid, const char *password);
/* Vom Portal gesetzte Anzeigewerte uebernehmen. Helligkeit 10..255. */
void bm24_display_apply_settings(uint8_t brightness, bool invert);
void bm24_display_toggle_enabled(void);
void bm24_display_toggle_rotation(void);

#ifdef __cplusplus
}
#endif

#endif /* BM24_DISPLAY_H */
