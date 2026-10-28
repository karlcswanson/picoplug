/*
 * cdp_tx.c - see cdp_tx.h.
 */
#include "cdp_tx.h"
#include "cdp.h"

#include <string.h>

static const uint8_t CDP_MCAST[6] = {0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCC};
static const uint8_t CDP_SNAP[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x0C, 0x20, 0x00};

typedef struct { uint8_t *b; size_t cap, pos; bool overflow; } wr_t;

static void put(wr_t *w, const void *p, size_t n)
{
    if (w->pos + n > w->cap) { w->overflow = true; return; }
    memcpy(w->b + w->pos, p, n);
    w->pos += n;
}
static void put8(wr_t *w, uint8_t v)   { put(w, &v, 1); }
static void put16(wr_t *w, uint16_t v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; put(w, b, 2); }

/* type(2) length(2, includes header) value */
static void tlv(wr_t *w, uint16_t type, const void *v, size_t n)
{
    put16(w, type);
    put16(w, (uint16_t)(n + 4));
    put(w, v, n);
}
static void tlv_str(wr_t *w, uint16_t type, const char *s)
{
    size_t n = strlen(s);
    if (n > 255) n = 255;
    tlv(w, type, s, n);
}

size_t cdp_tx_build(uint8_t *buf, size_t cap, const cdp_tx_cfg_t *cfg)
{
    if (!buf || !cfg || !cfg->mac || !cfg->device_id || !cfg->port_id) return 0;
    wr_t w = { buf, cap, 0, false };

    put(&w, CDP_MCAST, 6);
    put(&w, cfg->mac, 6);
    put16(&w, 0);                        /* 802.3 length, patched below */
    put(&w, CDP_SNAP, 8);
    size_t cdp_start = w.pos;

    put8(&w, 2);                         /* version */
    put8(&w, cfg->ttl_s);
    put16(&w, 0);                        /* checksum, patched below */

    tlv_str(&w, CDP_TLV_DEVICE_ID, cfg->device_id);
    if (cfg->has_ip) {
        /* count(4) | proto_type(1)=1 NLPID, proto_len(1)=1, proto(1)=0xCC IP, addr_len(2)=4, addr(4) */
        uint8_t a[13] = {0, 0, 0, 1, 1, 1, 0xCC, 0, 4, cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3]};
        tlv(&w, CDP_TLV_ADDRESSES, a, sizeof a);
    }
    tlv_str(&w, CDP_TLV_PORT_ID, cfg->port_id);
    { uint8_t c[4] = {0, 0, 0, CDP_CAP_HOST}; tlv(&w, CDP_TLV_CAPABILITIES, c, 4); }
    if (cfg->sw_version) {
        /* Keep the payload even so no receiver has to agree with us on the
         * odd-length checksum rule: IOS version strings end in a newline
         * anyway, so one is appended when the total would come out odd. */
        size_t sw = strlen(cfg->sw_version); if (sw > 254) sw = 254;
        size_t rest = (cfg->platform ? 4 + (strlen(cfg->platform) > 255 ? 255 : strlen(cfg->platform)) : 0)
                    + 5;    /* Duplex TLV */
        bool odd = ((w.pos - cdp_start) + 4 + sw + rest) & 1;
        put16(&w, CDP_TLV_SW_VERSION);
        put16(&w, (uint16_t)(sw + 4 + (odd ? 1 : 0)));
        put(&w, cfg->sw_version, sw);
        if (odd) put8(&w, '\n');
    }
    if (cfg->platform) tlv_str(&w, CDP_TLV_PLATFORM, cfg->platform);
    { uint8_t d = cfg->full_duplex ? 1 : 0; tlv(&w, CDP_TLV_DUPLEX, &d, 1); }
    if (w.overflow) return 0;
    size_t cdp_len = w.pos - cdp_start;
    uint16_t llen = (uint16_t)(cdp_len + 8);          /* LLC/SNAP + CDP */
    buf[12] = (uint8_t)(llen >> 8); buf[13] = (uint8_t)llen;
    uint16_t ck = cdp_checksum(buf + cdp_start, cdp_len);
    buf[cdp_start + 2] = (uint8_t)(ck >> 8); buf[cdp_start + 3] = (uint8_t)ck;
    return w.pos;
}
