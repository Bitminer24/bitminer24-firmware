#ifndef BM24_DISPLAY_H
#define BM24_DISPLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_DISPLAY_LINES 6
#define BM24_DISPLAY_LINE_MAX 47

typedef enum {
    BM24_DISPLAY_STYLE_STATUS = 0,
    BM24_DISPLAY_STYLE_BIG_VALUE,
    BM24_DISPLAY_STYLE_DASHBOARD
} bm24_display_style;

typedef struct {
    bm24_display_style style;
    char line[BM24_DISPLAY_LINES][BM24_DISPLAY_LINE_MAX + 1];
} bm24_display_frame;

/* Native esp_lcd/I80-Anbindung fuer das LilyGO T-Display S3. */
bool bm24_display_start(void);
void bm24_display_set(const bm24_display_frame *frame);
void bm24_display_setup(const char *ssid, const char *password);
void bm24_display_toggle_enabled(void);
void bm24_display_toggle_rotation(void);

#ifdef __cplusplus
}
#endif

#endif /* BM24_DISPLAY_H */
