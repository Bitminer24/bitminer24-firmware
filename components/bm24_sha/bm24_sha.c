#include "bm24_sha.h"

#include <string.h>

/* Kompression nach FIPS 180-4. Eine Schleife statt Unrolling: dieser Pfad
   ist Referenz und Selbsttest, nicht der Miner. */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const uint32_t IV[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void compress(uint32_t state[8], const uint32_t w_in[16])
{
    uint32_t w[64];
    memcpy(w, w_in, 16 * sizeof(uint32_t));
    for (int t = 16; t < 64; ++t) {
        uint32_t s0 = ROTR(w[t - 15], 7) ^ ROTR(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = ROTR(w[t - 2], 17) ^ ROTR(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int t = 0; t < 64; ++t) {
        uint32_t S1  = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch  = (e & f) ^ (~e & g);
        uint32_t t1  = h + S1 + ch + K[t] + w[t];
        uint32_t S0  = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2  = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void bm24_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint32_t state[8];
    memcpy(state, IV, sizeof(IV));

    size_t full = len / 64;
    for (size_t i = 0; i < full; ++i) {
        uint32_t w[16];
        for (int j = 0; j < 16; ++j)
            w[j] = load_be32(data + i * 64 + j * 4);
        compress(state, w);
    }

    /* Padding: 0x80, Nullen, 64-Bit-Laenge in Bits. */
    uint8_t block[128] = {0};
    size_t rest = len % 64;
    memcpy(block, data + full * 64, rest);
    block[rest] = 0x80;
    size_t total = (rest + 9 <= 64) ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; ++i)
        block[total - 1 - i] = (uint8_t)(bits >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[16];
        for (int j = 0; j < 16; ++j)
            w[j] = load_be32(block + off + j * 4);
        compress(state, w);
    }

    for (int i = 0; i < 8; ++i)
        store_be32(out + i * 4, state[i]);
}

void bm24_double_sha(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint8_t first[32];
    bm24_sha256(data, len, first);
    bm24_sha256(first, 32, out);
}

void bm24_sha_midstate(const uint8_t header64[64], uint32_t state[8])
{
    uint32_t w[16];
    memcpy(state, IV, sizeof(IV));
    for (int j = 0; j < 16; ++j)
        w[j] = load_be32(header64 + j * 4);
    compress(state, w);
}

void bm24_double_sha_from_midstate(const uint32_t state_in[8],
                                   const uint8_t tail12[12],
                                   uint32_t nonce,
                                   uint8_t out[32])
{
    /* Block 2 des Headers: 12 Byte Rest, Nonce little-endian (Bitcoin-
       Serialisierung), dann Padding fuer 80 Byte Gesamtlaenge (640 Bit). */
    uint32_t state[8];
    memcpy(state, state_in, sizeof(state));

    uint32_t w[16] = {0};
    w[0] = load_be32(tail12 + 0);
    w[1] = load_be32(tail12 + 4);
    w[2] = load_be32(tail12 + 8);
    {
        uint8_t nb[4] = {
            (uint8_t)nonce, (uint8_t)(nonce >> 8),
            (uint8_t)(nonce >> 16), (uint8_t)(nonce >> 24)
        };
        w[3] = load_be32(nb);
    }
    w[4]  = 0x80000000;
    w[15] = 640;
    compress(state, w);

    /* Zweiter SHA ueber die 32 Byte des ersten: ein Block, Laenge 256 Bit. */
    uint32_t w2[16] = {0};
    for (int i = 0; i < 8; ++i)
        w2[i] = state[i];
    w2[8]  = 0x80000000;
    w2[15] = 256;

    uint32_t st2[8];
    memcpy(st2, IV, sizeof(IV));
    compress(st2, w2);

    for (int i = 0; i < 8; ++i)
        store_be32(out + i * 4, st2[i]);
}
