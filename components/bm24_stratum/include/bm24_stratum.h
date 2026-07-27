#ifndef BM24_STRATUM_H
#define BM24_STRATUM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bm24_work.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_STRATUM_JOB_ID_MAX       128
#define BM24_STRATUM_SESSION_MAX      128
#define BM24_STRATUM_EXTRANONCE1_MAX   64
#define BM24_STRATUM_COINBASE_HEX_MAX (BM24_MAX_COINBASE_BYTES * 2)
#define BM24_STRATUM_ERROR_MAX         160

typedef struct {
    char session_id[BM24_STRATUM_SESSION_MAX + 1];
    char extranonce1_hex[BM24_STRATUM_EXTRANONCE1_MAX + 1];
    uint8_t extranonce2_size;
} bm24_stratum_subscription;

typedef struct {
    char job_id[BM24_STRATUM_JOB_ID_MAX + 1];
    char prev_hash_hex[65];
    char coinbase1_hex[BM24_STRATUM_COINBASE_HEX_MAX + 1];
    char coinbase2_hex[BM24_STRATUM_COINBASE_HEX_MAX + 1];
    char merkle_hex[BM24_MAX_MERKLE_BRANCHES][65];
    size_t merkle_count;
    char version_hex[9];
    char nbits_hex[9];
    char ntime_hex[9];
    bool clean_jobs;
} bm24_stratum_job;

typedef enum {
    BM24_STRATUM_MSG_INVALID = 0,
    BM24_STRATUM_MSG_UNKNOWN,
    BM24_STRATUM_MSG_NOTIFY,
    BM24_STRATUM_MSG_SET_DIFFICULTY,
    BM24_STRATUM_MSG_SET_EXTRANONCE,
    BM24_STRATUM_MSG_RESPONSE
} bm24_stratum_message_type;

typedef struct {
    bm24_stratum_message_type type;
    uint32_t id;
    bool response_ok;
    double difficulty;
    bm24_stratum_job job;
    bm24_stratum_subscription subscription;
    int error_code;
    char error_message[BM24_STRATUM_ERROR_MAX + 1];
} bm24_stratum_message;

/* Sichere JSON-RPC-Ausgabe. Rueckgabe ist die Laenge ohne NUL, 0 bei
   ungueltigem Argument oder zu kleinem Puffer. Jede Nachricht endet mit \n. */
size_t bm24_stratum_write_subscribe(char *out, size_t capacity, uint32_t id,
                                    const char *user_agent);
size_t bm24_stratum_write_authorize(char *out, size_t capacity, uint32_t id,
                                    const char *worker, const char *password);
size_t bm24_stratum_write_suggest_difficulty(char *out, size_t capacity,
                                             uint32_t id, double difficulty);
size_t bm24_stratum_write_submit(char *out, size_t capacity, uint32_t id,
                                 const char *worker, const char *job_id,
                                 const char *extranonce2_hex,
                                 const char *ntime_hex, uint32_t nonce);

/* IDF-cJSON-Parser fuer eine vollstaendige, newline-freie Stratum-Zeile. */
bool bm24_stratum_parse_line(const char *line, bm24_stratum_message *out);

/* Null-Allokations-Adapter zur kryptografischen Jobaufbereitung. Die
   Zeiger im Ergebnis bleiben nur so lange gueltig wie job. */
void bm24_stratum_job_view(const bm24_stratum_job *job,
                           bm24_work_input *out);

#ifdef __cplusplus
}
#endif

#endif /* BM24_STRATUM_H */
