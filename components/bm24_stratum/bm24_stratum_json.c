#include "bm24_stratum.h"

#include <limits.h>
#include <string.h>

#include "cJSON.h"

static bool copy_json_string(const cJSON *item, char *out, size_t capacity)
{
    if (!cJSON_IsString(item) || !item->valuestring || !out || capacity == 0)
        return false;
    size_t n = strlen(item->valuestring);
    if (n >= capacity)
        return false;
    memcpy(out, item->valuestring, n + 1);
    return true;
}

static bool json_u32(const cJSON *item, uint32_t *out)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)UINT32_MAX)
        return false;
    uint32_t value = (uint32_t)item->valuedouble;
    if ((double)value != item->valuedouble)
        return false;
    *out = value;
    return true;
}

static bool parse_error(const cJSON *root, bm24_stratum_message *out)
{
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (!error || cJSON_IsNull(error))
        return true;

    if (cJSON_IsArray(error)) {
        const cJSON *code = cJSON_GetArrayItem(error, 0);
        const cJSON *message = cJSON_GetArrayItem(error, 1);
        if (cJSON_IsNumber(code))
            out->error_code = code->valueint;
        if (cJSON_IsString(message))
            copy_json_string(message, out->error_message,
                             sizeof(out->error_message));
        return false;
    }
    copy_json_string(error, out->error_message, sizeof(out->error_message));
    return false;
}

static bool parse_notify(const cJSON *params, bm24_stratum_job *job)
{
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 9)
        return false;

    memset(job, 0, sizeof(*job));
    if (!copy_json_string(cJSON_GetArrayItem(params, 0), job->job_id,
                          sizeof(job->job_id)) ||
        !copy_json_string(cJSON_GetArrayItem(params, 1), job->prev_hash_hex,
                          sizeof(job->prev_hash_hex)) ||
        !copy_json_string(cJSON_GetArrayItem(params, 2), job->coinbase1_hex,
                          sizeof(job->coinbase1_hex)) ||
        !copy_json_string(cJSON_GetArrayItem(params, 3), job->coinbase2_hex,
                          sizeof(job->coinbase2_hex)) ||
        !copy_json_string(cJSON_GetArrayItem(params, 5), job->version_hex,
                          sizeof(job->version_hex)) ||
        !copy_json_string(cJSON_GetArrayItem(params, 6), job->nbits_hex,
                          sizeof(job->nbits_hex)) ||
        !copy_json_string(cJSON_GetArrayItem(params, 7), job->ntime_hex,
                          sizeof(job->ntime_hex)))
        return false;

    const cJSON *branches = cJSON_GetArrayItem(params, 4);
    if (!cJSON_IsArray(branches))
        return false;
    int branch_count = cJSON_GetArraySize(branches);
    if (branch_count < 0 || branch_count > BM24_MAX_MERKLE_BRANCHES)
        return false;
    job->merkle_count = (size_t)branch_count;
    for (int i = 0; i < branch_count; ++i) {
        if (!copy_json_string(cJSON_GetArrayItem(branches, i),
                              job->merkle_hex[i],
                              sizeof(job->merkle_hex[i])))
            return false;
    }

    const cJSON *clean = cJSON_GetArrayItem(params, 8);
    if (!cJSON_IsBool(clean))
        return false;
    job->clean_jobs = cJSON_IsTrue(clean);
    return true;
}

static bool parse_subscription(const cJSON *result,
                               bm24_stratum_subscription *subscription)
{
    if (!cJSON_IsArray(result) || cJSON_GetArraySize(result) < 3)
        return false;
    memset(subscription, 0, sizeof(*subscription));

    const cJSON *details = cJSON_GetArrayItem(result, 0);
    if (cJSON_IsArray(details) && cJSON_GetArraySize(details) > 0) {
        const cJSON *first = cJSON_GetArrayItem(details, 0);
        if (cJSON_IsArray(first) && cJSON_GetArraySize(first) > 1) {
            const cJSON *session = cJSON_GetArrayItem(first, 1);
            if (cJSON_IsString(session) &&
                !copy_json_string(session, subscription->session_id,
                                  sizeof(subscription->session_id)))
                return false;
        }
    }

    if (!copy_json_string(cJSON_GetArrayItem(result, 1),
                          subscription->extranonce1_hex,
                          sizeof(subscription->extranonce1_hex)))
        return false;
    const cJSON *size = cJSON_GetArrayItem(result, 2);
    uint32_t value;
    if (!json_u32(size, &value) || value == 0 || value > 8)
        return false;
    subscription->extranonce2_size = (uint8_t)value;
    return true;
}

bool bm24_stratum_parse_line(const char *line, bm24_stratum_message *out)
{
    if (!line || !out)
        return false;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(line);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (id && !cJSON_IsNull(id) && !json_u32(id, &out->id)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    bool ok = true;

    if (cJSON_IsString(method) && method->valuestring) {
        if (strcmp(method->valuestring, "mining.notify") == 0) {
            out->type = BM24_STRATUM_MSG_NOTIFY;
            ok = parse_notify(params, &out->job);
        } else if (strcmp(method->valuestring, "mining.set_difficulty") == 0) {
            out->type = BM24_STRATUM_MSG_SET_DIFFICULTY;
            const cJSON *difficulty = cJSON_IsArray(params)
                ? cJSON_GetArrayItem(params, 0) : NULL;
            ok = cJSON_IsNumber(difficulty) && difficulty->valuedouble > 0.0;
            if (ok)
                out->difficulty = difficulty->valuedouble;
        } else if (strcmp(method->valuestring, "mining.set_extranonce") == 0) {
            out->type = BM24_STRATUM_MSG_SET_EXTRANONCE;
            const cJSON *extra1 = cJSON_IsArray(params)
                ? cJSON_GetArrayItem(params, 0) : NULL;
            const cJSON *size = cJSON_IsArray(params)
                ? cJSON_GetArrayItem(params, 1) : NULL;
            uint32_t value;
            ok = copy_json_string(extra1,
                                  out->subscription.extranonce1_hex,
                                  sizeof(out->subscription.extranonce1_hex)) &&
                 json_u32(size, &value) && value > 0 && value <= 8;
            if (ok)
                out->subscription.extranonce2_size = (uint8_t)value;
        } else {
            out->type = BM24_STRATUM_MSG_UNKNOWN;
        }
    } else {
        out->type = BM24_STRATUM_MSG_RESPONSE;
        bool no_error = parse_error(root, out);
        const cJSON *result =
            cJSON_GetObjectItemCaseSensitive(root, "result");
        if (no_error && parse_subscription(result, &out->subscription)) {
            out->response_ok = true;
        } else {
            out->response_ok = no_error && cJSON_IsTrue(result);
        }
    }

    cJSON_Delete(root);
    if (!ok)
        out->type = BM24_STRATUM_MSG_INVALID;
    return ok;
}
