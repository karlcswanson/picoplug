/*
 * stp.c - see stp.h.
 */
#include "stp.h"

#include <string.h>

static const uint8_t STP_MCAST[6]  = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};
static const uint8_t PVST_MCAST[6] = {0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCD};
static const uint8_t PVST_SNAP[8]  = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x0C, 0x01, 0x0B};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Returns the offset of the BPDU (protocol identifier) or 0. */
static size_t bpdu_offset(const uint8_t *f, size_t len, stp_bpdu_t *o)
{
    if (len < 14 + 3 + 4) return 0;
    bool pvst = memcmp(f, PVST_MCAST, 6) == 0;
    if (!pvst && memcmp(f, STP_MCAST, 6) != 0) return 0;
    size_t p = 12;
    uint16_t et = rd16(f + p);
    if (et == 0x8100) {
        if (len < p + 4 + 2) return 0;
        if (o) { o->tagged = true; o->vlan = rd16(f + p + 2) & 0x0FFF; }
        p += 4;
        et = rd16(f + p);
    }
    if (et > 1500) return 0;                 /* 802.3 length field expected */
    size_t end = p + 2 + et;
    if (end > len) return 0;
    p += 2;
    if (pvst) {
        if (p + 8 > end || memcmp(f + p, PVST_SNAP, 8) != 0) return 0;
        p += 8;
    } else {
        if (p + 3 > end || f[p] != 0x42 || f[p + 1] != 0x42 || f[p + 2] != 0x03) return 0;
        p += 3;
    }
    if (o) o->pvst = pvst;
    return p + 4 <= end ? p : 0;
}

bool stp_is_bpdu(const uint8_t *frame, size_t len)
{
    return frame && bpdu_offset(frame, len, NULL) != 0;
}

bool stp_parse(const uint8_t *frame, size_t len, stp_bpdu_t *out)
{
    if (!frame || !out) return false;
    memset(out, 0, sizeof *out);
    size_t p = bpdu_offset(frame, len, out);
    if (!p) return false;
    size_t llen = rd16(frame + (out->tagged ? 16 : 12));
    size_t end = (out->tagged ? 18 : 14) + llen;
    memcpy(out->src_mac, frame + 6, 6);
    const uint8_t *b = frame + p;
    if (rd16(b) != 0) return false;          /* protocol identifier */
    out->version = b[2];
    out->type = b[3];
    if (out->type == STP_TYPE_TCN) return true;
    if (out->type != STP_TYPE_CONFIG && out->type != STP_TYPE_RST) return false;
    if (p + 4 + 31 > end) return false;
    const uint8_t *c = b + 4;
    out->flags = c[0];
    out->root_priority = rd16(c + 1);
    memcpy(out->root_mac, c + 3, 6);
    out->root_cost = rd32(c + 9);
    out->bridge_priority = rd16(c + 13);
    memcpy(out->bridge_mac, c + 15, 6);
    out->port_id = rd16(c + 21);
    out->msg_age_256 = rd16(c + 23);
    out->max_age_256 = rd16(c + 25);
    out->hello_256 = rd16(c + 27);
    out->fwd_delay_256 = rd16(c + 29);
    size_t q = p + 4 + 31;
    if (out->type == STP_TYPE_RST) q += 1;   /* version 1 length */
    if (out->version == STP_VERSION_MSTP && q + 2 <= end) {
        size_t v3 = rd16(frame + q);
        const uint8_t *m = frame + q + 2;
        if (v3 >= 64 && q + 2 + v3 <= end) {
            /* selector(1) name(32) revision(2) digest(16) int cost(4) CIST bridge id(8) hops(1) then 16-byte MSTI records */
            out->mst = true;
            size_t nl = 0;
            while (nl < 32 && m[1 + nl]) nl++;
            for (size_t i = 0; i < nl; i++) out->mst_region[i] = (char)((m[1 + i] >= 0x20 && m[1 + i] < 0x7F) ? m[1 + i] : '?');
            out->mst_region[nl] = '\0';
            out->mst_revision = rd16(m + 33);
            out->cist_internal_cost = rd32(m + 51);
            out->regional_root_priority = out->bridge_priority;
            memcpy(out->regional_root_mac, out->bridge_mac, 6);
            out->bridge_priority = rd16(m + 55);
            memcpy(out->bridge_mac, m + 57, 6);
            out->cist_hops = m[63];
            out->msti_count = (uint8_t)((v3 - 64) / 16);
        }
        q += 2 + v3;
    }
    /* PVST+ trailer: type(2)=0 len(2)=2 vlan(2) */
    if (out->pvst && q + 6 <= end && rd16(frame + q) == 0 && rd16(frame + q + 2) == 2)
        out->vlan = rd16(frame + q + 4) & 0x0FFF;
    return true;
}

const char *stp_version_name(uint8_t v)
{
    switch (v) { case STP_VERSION_STP: return "stp"; case STP_VERSION_RSTP: return "rstp"; case STP_VERSION_MSTP: return "mstp"; }
    return "unknown";
}
const char *stp_type_name(uint8_t t)
{
    switch (t) { case STP_TYPE_CONFIG: return "config"; case STP_TYPE_RST: return "rst"; case STP_TYPE_TCN: return "tcn"; }
    return "unknown";
}
const char *stp_role_name(uint8_t flags)
{
    switch ((flags >> 2) & 3) {
    case STP_ROLE_ALTERNATE:  return "alternate";
    case STP_ROLE_ROOT:       return "root";
    case STP_ROLE_DESIGNATED: return "designated";
    }
    return "unknown";
}
const char *stp_state_name(uint8_t flags)
{
    if (flags & STP_FLAG_FORWARDING) return "forwarding";
    if (flags & STP_FLAG_LEARNING) return "learning";
    return "discarding";
}

void stp_state_reset(stp_state_t *s) { memset(s, 0, sizeof *s); }

void stp_state_update(stp_state_t *s, const stp_bpdu_t *b, uint32_t now_ms)
{
    bool has_body = b->type != STP_TYPE_TCN;
    if (!s->seen) { s->first_ms = now_ms; s->generation++; }
    else {
        s->interval_ms = now_ms - s->last_ms;
        if (has_body && s->last.type != STP_TYPE_TCN &&
            (s->last.root_priority != b->root_priority || memcmp(s->last.root_mac, b->root_mac, 6) != 0)) {
            s->root_changes++; s->generation++;
        }
        if (has_body && s->last.type != STP_TYPE_TCN &&
            ((s->last.flags ^ b->flags) & (STP_FLAG_FORWARDING | STP_FLAG_LEARNING | 0x0C)))
            s->generation++;                 /* role or state changed */
    }
    if (b->type == STP_TYPE_TCN || (has_body && (b->flags & STP_FLAG_TC))) { s->tc_count++; s->generation++; }
    if (has_body || !s->seen) s->last = *b;  /* a TCN does not overwrite the last config BPDU */
    else { s->last.type = s->last.type; }
    s->seen = true;
    s->count++;
    s->last_ms = now_ms;
}
