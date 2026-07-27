#ifndef BM24_WORK_H
#define BM24_WORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_MAX_MERKLE_BRANCHES 32
#define BM24_MAX_COINBASE_BYTES  1024

/* Reine Datenansicht eines mining.notify. Der spaetere JSON-Parser besitzt
   die Strings; bm24_work_build() liest sie nur waehrend des Aufrufs. */
typedef struct {
    const char *version_hex;       /* 4 Byte / 8 Hexzeichen */
    const char *prev_hash_hex;     /* 32 Byte */
    const char *coinbase1_hex;
    const char *coinbase2_hex;
    const char *ntime_hex;         /* 4 Byte */
    const char *nbits_hex;         /* Bitcoin compact target, 4 Byte */
    const char *merkle_hex[BM24_MAX_MERKLE_BRANCHES];
    size_t merkle_count;
} bm24_work_input;

typedef struct {
    uint8_t header[80];            /* serialisierter Bitcoin-Blockheader */
    uint8_t merkle_root[32];       /* internes Digestformat im Header */
    uint8_t network_target_le[32]; /* 256-Bit-Ziel, little-endian */
    uint32_t midstate[8];          /* portable FIPS-u32 fuer bm24_sha */
    char extranonce2_hex[17];      /* bis 8 Byte plus NUL */
} bm24_work;

typedef enum {
    BM24_WORK_OK = 0,
    BM24_WORK_BAD_ARGUMENT,
    BM24_WORK_BAD_HEX,
    BM24_WORK_TOO_MANY_BRANCHES,
    BM24_WORK_COINBASE_TOO_LARGE,
    BM24_WORK_BAD_EXTRANONCE2,
    BM24_WORK_BAD_COMPACT_TARGET
} bm24_work_status;

/* Baut aus einem Stratum-Job genau den 80-Byte-Header, den HW- und
   SW-Kernel verarbeiten. extranonce2_size ist in Bytes. */
bm24_work_status bm24_work_build(const bm24_work_input *input,
                                 const char *extranonce1_hex,
                                 uint64_t extranonce2,
                                 uint8_t extranonce2_size,
                                 bm24_work *out);

/* Wandelt nBits (z.B. "1d00ffff") in ein little-endian 256-Bit-Ziel. */
bm24_work_status bm24_compact_target(const char *nbits_hex,
                                     uint8_t target_le[32]);

/* Bitcoin interpretiert die 32 SHA-Ausgabebytes als little-endian Zahl. */
bool bm24_hash_meets_target(const uint8_t hash[32],
                            const uint8_t target_le[32]);

/* Share-Difficulty relativ zum klassischen Bitcoin-diff-1-Ziel. */
double bm24_hash_difficulty(const uint8_t hash[32]);

const char *bm24_work_status_string(bm24_work_status status);

#ifdef __cplusplus
}
#endif

#endif /* BM24_WORK_H */
