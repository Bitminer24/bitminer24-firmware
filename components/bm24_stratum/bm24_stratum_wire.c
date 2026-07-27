#include "bm24_stratum.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *out;
    size_t capacity;
    size_t length;
    bool ok;
} writer;

static void put_char(writer *w, char c)
{
    if (!w->ok)
        return;
    if (w->length + 1 >= w->capacity) {
        w->ok = false;
        return;
    }
    w->out[w->length++] = c;
}

static void put_raw(writer *w, const char *s)
{
    if (!s) {
        w->ok = false;
        return;
    }
    while (*s)
        put_char(w, *s++);
}

static void put_json_string(writer *w, const char *s)
{
    static const char HEX[] = "0123456789abcdef";
    if (!s) {
        w->ok = false;
        return;
    }
    put_char(w, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        switch (*p) {
        case '"':  put_raw(w, "\\\""); break;
        case '\\': put_raw(w, "\\\\"); break;
        case '\b': put_raw(w, "\\b");  break;
        case '\f': put_raw(w, "\\f");  break;
        case '\n': put_raw(w, "\\n");  break;
        case '\r': put_raw(w, "\\r");  break;
        case '\t': put_raw(w, "\\t");  break;
        default:
            if (*p < 0x20) {
                put_raw(w, "\\u00");
                put_char(w, HEX[*p >> 4]);
                put_char(w, HEX[*p & 0x0f]);
            } else {
                put_char(w, (char)*p);
            }
        }
    }
    put_char(w, '"');
}

static void put_u32(writer *w, uint32_t value)
{
    char tmp[11];
    snprintf(tmp, sizeof(tmp), "%" PRIu32, value);
    put_raw(w, tmp);
}

static size_t finish(writer *w)
{
    if (!w->out || w->capacity == 0)
        return 0;
    if (!w->ok) {
        w->out[0] = '\0';
        return 0;
    }
    w->out[w->length] = '\0';
    return w->length;
}

static writer begin(char *out, size_t capacity)
{
    writer w = {
        .out = out,
        .capacity = capacity,
        .length = 0,
        .ok = out != NULL && capacity > 0
    };
    if (out && capacity)
        out[0] = '\0';
    return w;
}

size_t bm24_stratum_write_subscribe(char *out, size_t capacity, uint32_t id,
                                    const char *user_agent)
{
    writer w = begin(out, capacity);
    put_raw(&w, "{\"id\":");
    put_u32(&w, id);
    put_raw(&w, ",\"method\":\"mining.subscribe\",\"params\":[");
    put_json_string(&w, user_agent);
    put_raw(&w, "]}\n");
    return finish(&w);
}

size_t bm24_stratum_write_authorize(char *out, size_t capacity, uint32_t id,
                                    const char *worker, const char *password)
{
    writer w = begin(out, capacity);
    put_raw(&w, "{\"id\":");
    put_u32(&w, id);
    put_raw(&w, ",\"method\":\"mining.authorize\",\"params\":[");
    put_json_string(&w, worker);
    put_char(&w, ',');
    put_json_string(&w, password);
    put_raw(&w, "]}\n");
    return finish(&w);
}

size_t bm24_stratum_write_suggest_difficulty(char *out, size_t capacity,
                                             uint32_t id, double difficulty)
{
    if (!(difficulty > 0.0))
        return 0;

    char number[32];
    int n = snprintf(number, sizeof(number), "%.10g", difficulty);
    if (n <= 0 || (size_t)n >= sizeof(number))
        return 0;

    writer w = begin(out, capacity);
    put_raw(&w, "{\"id\":");
    put_u32(&w, id);
    put_raw(&w, ",\"method\":\"mining.suggest_difficulty\",\"params\":[");
    put_raw(&w, number);
    put_raw(&w, "]}\n");
    return finish(&w);
}

size_t bm24_stratum_write_submit(char *out, size_t capacity, uint32_t id,
                                 const char *worker, const char *job_id,
                                 const char *extranonce2_hex,
                                 const char *ntime_hex, uint32_t nonce)
{
    char nonce_hex[9];
    snprintf(nonce_hex, sizeof(nonce_hex), "%08" PRIx32, nonce);

    writer w = begin(out, capacity);
    put_raw(&w, "{\"id\":");
    put_u32(&w, id);
    put_raw(&w, ",\"method\":\"mining.submit\",\"params\":[");
    put_json_string(&w, worker);
    put_char(&w, ',');
    put_json_string(&w, job_id);
    put_char(&w, ',');
    put_json_string(&w, extranonce2_hex);
    put_char(&w, ',');
    put_json_string(&w, ntime_hex);
    put_char(&w, ',');
    put_json_string(&w, nonce_hex);
    put_raw(&w, "]}\n");
    return finish(&w);
}

void bm24_stratum_job_view(const bm24_stratum_job *job,
                           bm24_work_input *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!job)
        return;

    out->version_hex = job->version_hex;
    out->prev_hash_hex = job->prev_hash_hex;
    out->coinbase1_hex = job->coinbase1_hex;
    out->coinbase2_hex = job->coinbase2_hex;
    out->ntime_hex = job->ntime_hex;
    out->nbits_hex = job->nbits_hex;
    out->merkle_count = job->merkle_count;
    for (size_t i = 0; i < job->merkle_count; ++i)
        out->merkle_hex[i] = job->merkle_hex[i];
}
