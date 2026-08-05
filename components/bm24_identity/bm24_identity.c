#include "bm24_identity.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_random.h"
#include "nvs.h"
#endif

#define BM24_IDENTITY_NAMESPACE "bm24id"
#define BM24_IDENTITY_KEY       "uuid"

bool bm24_identity_format_uuid(const uint8_t bytes[16], char *out,
                               size_t capacity)
{
    if (!bytes || !out || capacity < BM24_DEVICE_ID_LENGTH + 1)
        return false;
    int written = snprintf(
        out, capacity,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
        "%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return written == (int)BM24_DEVICE_ID_LENGTH;
}

#ifdef ESP_PLATFORM

static bool valid_uuid(const char *value)
{
    if (!value || strlen(value) != BM24_DEVICE_ID_LENGTH)
        return false;
    for (size_t i = 0; i < BM24_DEVICE_ID_LENGTH; ++i) {
        bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        char c = value[i];
        if (separator) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return value[14] == '4' &&
           (value[19] == '8' || value[19] == '9' ||
            value[19] == 'a' || value[19] == 'b');
}

bool bm24_identity_get(char out[BM24_DEVICE_ID_LENGTH + 1])
{
    if (!out)
        return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BM24_IDENTITY_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return false;

    size_t length = BM24_DEVICE_ID_LENGTH + 1;
    err = nvs_get_str(handle, BM24_IDENTITY_KEY, out, &length);
    if (err == ESP_OK && valid_uuid(out)) {
        nvs_close(handle);
        return true;
    }

    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = (uint8_t)((bytes[6] & 0x0f) | 0x40); /* UUIDv4 */
    bytes[8] = (uint8_t)((bytes[8] & 0x3f) | 0x80); /* RFC variant */
    if (!bm24_identity_format_uuid(bytes, out,
                                   BM24_DEVICE_ID_LENGTH + 1)) {
        nvs_close(handle);
        return false;
    }

    err = nvs_set_str(handle, BM24_IDENTITY_KEY, out);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        out[0] = '\0';
        return false;
    }
    return true;
}

#else

bool bm24_identity_get(char out[BM24_DEVICE_ID_LENGTH + 1])
{
    if (out)
        out[0] = '\0';
    return false;
}

#endif
