/*
 * ws.c - see ws.h.
 */
#include "ws.h"

#include <string.h>

/* ---- SHA-1 (FIPS 180-4), only for the handshake ---- */
static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(uint32_t h[5], const uint8_t b[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (bb & c) | (~bb & d);           k = 0x5A827999u; }
        else if (i < 40) { f = bb ^ c ^ d;                     k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (bb & c) | (bb & d) | (c & d);  k = 0x8F1BBCDCu; }
        else             { f = bb ^ c ^ d;                     k = 0xCA62C1D6u; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(bb, 30); bb = a; a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
}

/* SHA-1 of a then b (the key, then the RFC's fixed GUID) */
static void sha1_2(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    uint8_t blk[64];
    size_t used = 0, total = an + bn;
    for (size_t i = 0; i < total; i++) {
        blk[used++] = i < an ? a[i] : b[i - an];
        if (used == 64) { sha1_block(h, blk); used = 0; }
    }
    blk[used++] = 0x80;
    if (used > 56) { memset(blk + used, 0, 64 - used); sha1_block(h, blk); used = 0; }
    memset(blk + used, 0, 56 - used);
    uint64_t bits = (uint64_t)total * 8;
    for (int i = 0; i < 8; i++) blk[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_block(h, blk);
    for (int i = 0; i < 20; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
}

void ws_accept(const char *key, size_t key_len, char out[WS_ACCEPT_LEN + 1])
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t d[21];
    sha1_2((const uint8_t *)key, key_len, (const uint8_t *)guid, sizeof guid - 1, d);
    d[20] = 0;
    size_t o = 0;
    for (int i = 0; i < 21; i += 3) {   /* 20 bytes: six full groups, then two bytes and one '=' */
        uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2];
        out[o++] = b64[v >> 18 & 63];
        out[o++] = b64[v >> 12 & 63];
        out[o++] = b64[v >> 6 & 63];
        out[o++] = i + 2 < 20 ? b64[v & 63] : '=';
    }
    out[WS_ACCEPT_LEN] = '\0';
}

size_t ws_text_header(size_t len, uint8_t out[WS_HDR_MAX])
{
    out[0] = 0x81;                          /* FIN, text */
    if (len < 126) { out[1] = (uint8_t)len; return 2; }
    out[1] = 126;
    out[2] = (uint8_t)(len >> 8);
    out[3] = (uint8_t)len;
    return 4;
}

ws_parse_t ws_parse(const uint8_t *buf, size_t n, ws_frame_t *f)
{
    if (n < 2) return WS_NEED_MORE;
    if (!(buf[1] & 0x80)) return WS_BAD;    /* clients must mask */
    size_t len = buf[1] & 0x7F;
    if (len > 125) return WS_BAD;
    if (n < 6 + len) return WS_NEED_MORE;
    const uint8_t *mask = buf + 2;
    f->opcode = buf[0] & 0x0F;
    f->len = (uint8_t)len;
    for (size_t i = 0; i < len; i++) f->payload[i] = buf[6 + i] ^ mask[i & 3];
    f->frame_len = 6 + len;
    return WS_FRAME;
}
