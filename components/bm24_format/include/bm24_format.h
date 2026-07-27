#ifndef BM24_FORMAT_H
#define BM24_FORMAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Deutsche Tausenderpunkte: 179009 -> "179.009", -1234 -> "-1.234".
   Schreibt hoechstens out_len Bytes inkl. Nullterminierung; bei zu kleinem
   Puffer wird abgeschnitten und trotzdem terminiert. Rueckgabe: out. */
char *bm24_thousands(double v, char *out, size_t out_len);

/* Altersangabe eines Zeitpunkts: "gerade eben", "vor 12 Min.",
   "vor 3 Std.", "vor 5 Tagen". Gleiche Stufen wie in 1.x. */
char *bm24_age_text(long seconds_ago, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* BM24_FORMAT_H */
