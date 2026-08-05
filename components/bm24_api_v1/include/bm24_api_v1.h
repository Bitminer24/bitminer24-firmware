#ifndef BM24_API_V1_H
#define BM24_API_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *device_id;
    const char *firmware_version;
} bm24_api_v1_info;

typedef struct {
    const char *device_id;
    const char *observed_at_json;
    const char *status;
    bool hashrate_available;
    double hashrate_hps;
    bool temperature_available;
    double temperature_c;
    bool shares_submitted_available;
    uint64_t shares_submitted;
    bool shares_accepted_available;
    uint64_t shares_accepted;
    double best_difficulty;
    uint64_t blocks_found;
    bool pool_connected;
    uint64_t uptime_s;
    const char *last_activity_at_json;
} bm24_api_v1_status;

bool bm24_api_v1_format_info(const bm24_api_v1_info *info, char *json,
                             size_t capacity);
bool bm24_api_v1_format_status(const bm24_api_v1_status *status, char *json,
                               size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* BM24_API_V1_H */
