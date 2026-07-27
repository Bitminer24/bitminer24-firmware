#include "bm24_work.h"

#include <string.h>

#include "bm24_sha.h"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode_hex_exact(const char *hex, uint8_t *out, size_t bytes)
{
    if (!hex || !out || strlen(hex) != bytes * 2)
        return false;

    for (size_t i = 0; i < bytes; ++i) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool append_hex(const char *hex, uint8_t *out, size_t capacity,
                       size_t *used)
{
    if (!hex || !out || !used)
        return false;
    size_t chars = strlen(hex);
    if ((chars & 1u) != 0 || chars / 2 > capacity - *used)
        return false;

    size_t bytes = chars / 2;
    if (!decode_hex_exact(hex, out + *used, bytes))
        return false;
    *used += bytes;
    return true;
}

static void reverse_bytes(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n / 2; ++i) {
        uint8_t tmp = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = tmp;
    }
}

static void reverse_u32_words(uint8_t *p, size_t bytes)
{
    for (size_t off = 0; off < bytes; off += 4)
        reverse_bytes(p + off, 4);
}

static bool format_extranonce2(uint64_t value, uint8_t size, char out[17])
{
    static const char HEX[] = "0123456789abcdef";
    if (size == 0 || size > 8)
        return false;
    if (size < 8 && value >= (UINT64_C(1) << (size * 8)))
        return false;

    size_t chars = (size_t)size * 2;
    out[chars] = '\0';
    for (size_t i = 0; i < chars; ++i) {
        out[chars - 1 - i] = HEX[value & 0xFu];
        value >>= 4;
    }
    return true;
}

bm24_work_status bm24_compact_target(const char *nbits_hex,
                                     uint8_t target_le[32])
{
    uint8_t compact[4];
    if (!target_le)
        return BM24_WORK_BAD_ARGUMENT;
    if (!decode_hex_exact(nbits_hex, compact, sizeof(compact)))
        return BM24_WORK_BAD_HEX;

    uint8_t exponent = compact[0];
    uint32_t coefficient = ((uint32_t)(compact[1] & 0x7fu) << 16) |
                           ((uint32_t)compact[2] << 8) |
                           (uint32_t)compact[3];
    if ((compact[1] & 0x80u) != 0 || coefficient == 0 ||
        exponent == 0 || exponent > 32)
        return BM24_WORK_BAD_COMPACT_TARGET;

    memset(target_le, 0, 32);
    if (exponent <= 3) {
        coefficient >>= 8 * (3 - exponent);
        for (size_t i = 0; i < 3 && coefficient != 0; ++i) {
            target_le[i] = (uint8_t)coefficient;
            coefficient >>= 8;
        }
    } else {
        size_t offset = (size_t)exponent - 3;
        if (offset + 3 > 32)
            return BM24_WORK_BAD_COMPACT_TARGET;
        target_le[offset]     = (uint8_t)coefficient;
        target_le[offset + 1] = (uint8_t)(coefficient >> 8);
        target_le[offset + 2] = (uint8_t)(coefficient >> 16);
    }
    for (size_t i = 0; i < 32; ++i) {
        if (target_le[i] != 0)
            return BM24_WORK_OK;
    }
    return BM24_WORK_BAD_COMPACT_TARGET;
}

bm24_work_status bm24_work_build(const bm24_work_input *input,
                                 const char *extranonce1_hex,
                                 uint64_t extranonce2,
                                 uint8_t extranonce2_size,
                                 bm24_work *out)
{
    if (!input || !extranonce1_hex || !out)
        return BM24_WORK_BAD_ARGUMENT;
    if (input->merkle_count > BM24_MAX_MERKLE_BRANCHES)
        return BM24_WORK_TOO_MANY_BRANCHES;

    memset(out, 0, sizeof(*out));
    if (!format_extranonce2(extranonce2, extranonce2_size,
                            out->extranonce2_hex))
        return BM24_WORK_BAD_EXTRANONCE2;

    uint8_t coinbase[BM24_MAX_COINBASE_BYTES];
    size_t coinbase_len = 0;
    const char *coinbase_parts[] = {
        input->coinbase1_hex, extranonce1_hex,
        out->extranonce2_hex, input->coinbase2_hex
    };
    for (size_t i = 0; i < sizeof(coinbase_parts) / sizeof(coinbase_parts[0]);
         ++i) {
        size_t before = coinbase_len;
        if (!append_hex(coinbase_parts[i], coinbase, sizeof(coinbase),
                        &coinbase_len)) {
            const char *part = coinbase_parts[i];
            if (part && (strlen(part) / 2 > sizeof(coinbase) - before))
                return BM24_WORK_COINBASE_TOO_LARGE;
            return BM24_WORK_BAD_HEX;
        }
    }

    bm24_double_sha(coinbase, coinbase_len, out->merkle_root);
    for (size_t i = 0; i < input->merkle_count; ++i) {
        uint8_t pair[64];
        memcpy(pair, out->merkle_root, 32);
        if (!decode_hex_exact(input->merkle_hex[i], pair + 32, 32))
            return BM24_WORK_BAD_HEX;
        bm24_double_sha(pair, sizeof(pair), out->merkle_root);
    }

    if (!decode_hex_exact(input->version_hex, out->header, 4) ||
        !decode_hex_exact(input->prev_hash_hex, out->header + 4, 32) ||
        !decode_hex_exact(input->ntime_hex, out->header + 68, 4) ||
        !decode_hex_exact(input->nbits_hex, out->header + 72, 4))
        return BM24_WORK_BAD_HEX;

    reverse_bytes(out->header, 4);
    reverse_u32_words(out->header + 4, 32);
    memcpy(out->header + 36, out->merkle_root, 32);
    reverse_bytes(out->header + 68, 4);
    reverse_bytes(out->header + 72, 4);
    memset(out->header + 76, 0, 4);

    bm24_work_status status =
        bm24_compact_target(input->nbits_hex, out->network_target_le);
    if (status != BM24_WORK_OK)
        return status;

    bm24_sha_midstate(out->header, out->midstate);
    return BM24_WORK_OK;
}

bool bm24_hash_meets_target(const uint8_t hash[32],
                            const uint8_t target_le[32])
{
    if (!hash || !target_le)
        return false;
    for (int i = 31; i >= 0; --i) {
        if (hash[i] < target_le[i]) return true;
        if (hash[i] > target_le[i]) return false;
    }
    return true;
}

double bm24_hash_difficulty(const uint8_t hash[32])
{
    /* 0x00000000FFFF0000... als double. Das ist der historische
       Bitcoin-diff-1-Target, nicht der jeweilige nBits-Netzwerk-Target. */
    static const double DIFF1 =
        26959535291011309493156476344723991336010898738574164086137773096960.0;

    if (!hash)
        return 0.0;
    double value = 0.0;
    for (int i = 31; i >= 0; --i)
        value = value * 256.0 + hash[i];
    if (value == 0.0)
        return DIFF1;
    return DIFF1 / value;
}

const char *bm24_work_status_string(bm24_work_status status)
{
    switch (status) {
    case BM24_WORK_OK:                 return "ok";
    case BM24_WORK_BAD_ARGUMENT:       return "bad argument";
    case BM24_WORK_BAD_HEX:            return "bad hex";
    case BM24_WORK_TOO_MANY_BRANCHES:  return "too many merkle branches";
    case BM24_WORK_COINBASE_TOO_LARGE: return "coinbase too large";
    case BM24_WORK_BAD_EXTRANONCE2:    return "bad extranonce2";
    case BM24_WORK_BAD_COMPACT_TARGET: return "bad compact target";
    default:                           return "unknown";
    }
}
