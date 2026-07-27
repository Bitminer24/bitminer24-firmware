#include "bm24_format.h"

#include <stdio.h>
#include <string.h>

char *bm24_thousands(double v, char *out, size_t out_len)
{
    if (out_len == 0) return out;

    char raw[24];
    snprintf(raw, sizeof(raw), "%.0f", v);

    size_t len   = strlen(raw);
    size_t lead  = (raw[0] == '-') ? 1 : 0;
    size_t w     = 0;

    for (size_t i = 0; i < len && w + 1 < out_len; ++i) {
        size_t from_end = len - i;
        if (i > lead && (from_end % 3) == 0 && w + 1 < out_len)
            out[w++] = '.';
        if (w + 1 < out_len)
            out[w++] = raw[i];
    }
    out[w] = '\0';
    return out;
}

char *bm24_age_text(long seconds_ago, char *out, size_t out_len)
{
    if (out_len == 0) return out;

    if (seconds_ago < 0) {
        snprintf(out, out_len, "gerade eben");
        return out;
    }
    long h = seconds_ago / 3600;
    if (h < 1)
        snprintf(out, out_len, "vor %ld Min.", seconds_ago / 60);
    else if (h < 48)
        snprintf(out, out_len, "vor %ld Std.", h);
    else
        snprintf(out, out_len, "vor %ld Tagen", h / 24);
    return out;
}
