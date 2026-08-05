#include "bm24_api_v1.h"

#include <inttypes.h>
#include <stdio.h>

static bool valid_output(const char *json, size_t capacity, int written)
{
    return json && capacity > 0 && written > 0 && (size_t)written < capacity;
}

bool bm24_api_v1_format_info(const bm24_api_v1_info *info, char *json,
                             size_t capacity)
{
    if (!info || !info->device_id || !info->firmware_version ||
        !json || capacity == 0)
        return false;
    int written = snprintf(
        json, capacity,
        "{\"api_version\":\"1.0\",\"device_id\":\"%s\","
        "\"device_type\":\"nerdminer\",\"model\":\"NerdMiner V2\","
        "\"hardware_revision\":\"LilyGO T-Display S3\","
        "\"firmware_version\":\"%s\",\"capabilities\":{"
        "\"pool_config\":false,\"reset_stats\":false,"
        "\"restart\":false,\"ota\":false,"
        "\"power_reporting\":\"unavailable\"}}",
        info->device_id, info->firmware_version);
    return valid_output(json, capacity, written);
}

bool bm24_api_v1_format_status(const bm24_api_v1_status *status, char *json,
                               size_t capacity)
{
    if (!status || !status->device_id || !status->observed_at_json ||
        !status->status || !status->last_activity_at_json ||
        !json || capacity == 0)
        return false;

    char hashrate[32];
    char temperature[24];
    char submitted[32];
    char accepted[32];
    if (status->hashrate_available)
        snprintf(hashrate, sizeof(hashrate), "%.0f", status->hashrate_hps);
    else
        snprintf(hashrate, sizeof(hashrate), "null");
    if (status->temperature_available)
        snprintf(temperature, sizeof(temperature), "%.1f", status->temperature_c);
    else
        snprintf(temperature, sizeof(temperature), "null");
    if (status->shares_submitted_available)
        snprintf(submitted, sizeof(submitted), "%" PRIu64,
                 status->shares_submitted);
    else
        snprintf(submitted, sizeof(submitted), "null");
    if (status->shares_accepted_available)
        snprintf(accepted, sizeof(accepted), "%" PRIu64,
                 status->shares_accepted);
    else
        snprintf(accepted, sizeof(accepted), "null");

    int written = snprintf(
        json, capacity,
        "{\"device_id\":\"%s\",\"observed_at\":%s,"
        "\"status\":\"%s\",\"hashrate_hps\":%s,"
        "\"temperature_c\":%s,\"power_w\":null,"
        "\"shares\":{\"submitted\":%s,"
        "\"accepted\":%s,\"rejected\":null},"
        "\"best_difficulty\":%.8f,\"blocks_found\":%" PRIu64 ","
        "\"pool_connected\":%s,\"uptime_s\":%" PRIu64 ","
        "\"last_activity_at\":%s,\"sources\":{"
        "\"hashrate\":\"device_measured\","
        "\"temperature\":\"device_measured\","
        "\"power\":\"unavailable\",\"shares\":\"pool_reported\","
        "\"best_difficulty\":\"pool_reported\"}}",
        status->device_id, status->observed_at_json, status->status, hashrate,
        temperature, submitted, accepted,
        status->best_difficulty, status->blocks_found,
        status->pool_connected ? "true" : "false", status->uptime_s,
        status->last_activity_at_json);
    return valid_output(json, capacity, written);
}
