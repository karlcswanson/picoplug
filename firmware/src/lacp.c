/*
 * lacp.c - see lacp.h.
 */
#include "lacp.h"

#include <string.h>

/* Offset of the slow-protocols payload (subtype byte), 0 if not LACP. */
static size_t lacp_offset(const uint8_t *f, size_t len)
{
    if (!f || len < 16) return 0;
    size_t o = 12;
    if (f[o] == 0x81 && f[o + 1] == 0x00) { o += 4; if (len < o + 3) return 0; }
    if (f[o] != (LACP_ETHERTYPE >> 8) || f[o + 1] != (LACP_ETHERTYPE & 0xFF)) return 0;
    if (f[o + 2] != 0x01) return 0;                     /* subtype 1 = LACP (2 marker, 3 OAM) */
    return o + 2;
}

bool lacp_is_lacp(const uint8_t *frame, size_t len) { return lacp_offset(frame, len) != 0; }

static void info(const uint8_t *p, lacp_info_t *i)
{
    i->sys_priority = (uint16_t)(p[0] << 8 | p[1]);
    memcpy(i->sys_mac, p + 2, 6);
    i->key = (uint16_t)(p[8] << 8 | p[9]);
    i->port_priority = (uint16_t)(p[10] << 8 | p[11]);
    i->port = (uint16_t)(p[12] << 8 | p[13]);
    i->state = p[14];
}

bool lacp_parse(const uint8_t *frame, size_t len, lacp_pdu_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    size_t o = lacp_offset(frame, len);
    /* subtype(1) version(1) actor TLV: type 1, len 20; partner TLV: type 2, len 20 */
    if (!o || len < o + 2 + 20 + 20) return false;
    memcpy(out->src_mac, frame + 6, 6);
    out->version = frame[o + 1];
    const uint8_t *a = frame + o + 2, *p = a + 20;
    if (a[0] != 1 || a[1] != 20 || p[0] != 2 || p[1] != 20) return false;
    info(a + 2, &out->actor);
    info(p + 2, &out->partner);
    return true;
}

void lacp_state_reset(lacp_state_t *s) { memset(s, 0, sizeof *s); }

void lacp_state_update(lacp_state_t *s, const lacp_pdu_t *p, uint32_t now_ms)
{
    bool same = s->seen && !memcmp(&s->actor, &p->actor, sizeof p->actor) && !memcmp(&s->partner, &p->partner, sizeof p->partner);
    if (s->seen && !same) s->changes++;
    if (!s->seen) s->first_ms = now_ms;
    else s->interval_ms = now_ms - s->last_ms;
    s->seen = true;
    s->count++;
    s->last_ms = now_ms;
    memcpy(s->src_mac, p->src_mac, 6);
    s->version = p->version;
    s->actor = p->actor;
    s->partner = p->partner;
    if (!same) s->generation++;
}
