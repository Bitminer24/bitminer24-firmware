/* BitMiner24 2.0 — Hintergrundgrafiken der Geraeteoberflaeche.

   Dieselben RGB565-Bilder wie in 1.8.3-bm1, damit die Optik auf dem
   Auslieferungsgeraet erhalten bleibt. Jedes Bild ist 320x170 und liegt
   als const im Flash; im RAM wird immer nur ein Zeilenband gehalten. */

#ifndef BM24_MEDIA_H
#define BM24_MEDIA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_IMG_WIDTH  320
#define BM24_IMG_HEIGHT 170
#define BM24_IMG_PIXELS (BM24_IMG_WIDTH * BM24_IMG_HEIGHT)

extern const uint16_t bm24_img_setup[BM24_IMG_PIXELS];    /* Setup-Portal   */
extern const uint16_t bm24_img_init[BM24_IMG_PIXELS];     /* Start/Verbinden*/
extern const uint16_t bm24_img_miner[BM24_IMG_PIXELS];    /* Seite 1        */
extern const uint16_t bm24_img_clock[BM24_IMG_PIXELS];    /* Seite 2        */
extern const uint16_t bm24_img_network[BM24_IMG_PIXELS];  /* Seite 3        */
extern const uint16_t bm24_img_price[BM24_IMG_PIXELS];    /* Seite 4        */

#ifdef __cplusplus
}
#endif

#endif /* BM24_MEDIA_H */
