/*
 * peer.c - see peer.h.
 *
 * Payload after the EtherType: "PCLP" version(1) type(1) seq(2), then per type:
 *   HELLO      uptime_s(4) speed(1) full(1) pvid(2) chassis(lenbyte+str) port(lenbyte+str) fw(lenbyte+str)
 *              [probe(1): number + 1, 0 none; absent from firmware before 0.3.1]
 *   ECHO_*     t_ms(4)
 *   BLINK_REQ  token(2) off_ms(2)
 *   BLINK_ACK  token(2)
 */
#include "peer.h"

#include <string.h>

static const uint8_t MAGIC[4] = {'P', 'C', 'L', 'P'};

static size_t payload_offset(const uint8_t *f, size_t len)
{
    if (!f || len < 14 + 8) return 0;
    if (f[12] != (PEER_ETHERTYPE >> 8) || f[13] != (PEER_ETHERTYPE & 0xFF)) return 0;
    if (memcmp(f + 14, MAGIC, 4) != 0 || f[18] != PEER_VERSION) return 0;
    return 14;
}

bool peer_is_peer(const uint8_t *frame, size_t len) { return payload_offset(frame, len) != 0; }

static bool get_str(const uint8_t *f, size_t len, size_t *o, char *out, size_t cap)
{
    if (*o + 1 > len) return false;
    size_t n = f[*o];
    if (*o + 1 + n > len) return false;
    size_t c = n < cap - 1 ? n : cap - 1;
    for (size_t i = 0; i < c; i++) { uint8_t ch = f[*o + 1 + i]; out[i] = (ch >= 32 && ch < 127) ? (char)ch : '?'; }
    out[c] = '\0';
    *o += 1 + n;
    return true;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool peer_parse(const uint8_t *f, size_t len, peer_msg_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    size_t o = payload_offset(f, len);
    if (!o) return false;
    memcpy(out->dst, f, 6);
    memcpy(out->src, f + 6, 6);
    out->type = f[o + 5];
    out->seq = rd16(f + o + 6);
    o += 8;
    switch (out->type) {
    case PEER_HELLO:
        if (o + 8 > len) return false;
        out->uptime_s = rd32(f + o);
        out->speed = f[o + 4];
        out->full = f[o + 5] != 0;
        out->pvid = rd16(f + o + 6);
        o += 8;
        out->probe = PEER_PROBE_NONE;
        if (!(get_str(f, len, &o, out->chassis, sizeof out->chassis) &&
              get_str(f, len, &o, out->port, sizeof out->port) &&
              get_str(f, len, &o, out->fw, sizeof out->fw))) return false;
        /* frames are zero-padded to 60 bytes, so 0 must mean "none" */
        if (o < len && f[o] >= 1 && f[o] <= 10) out->probe = (uint8_t)(f[o] - 1);
        return true;
    case PEER_ECHO_REQ:
    case PEER_ECHO_REP:
        if (o + 4 > len) return false;
        out->t_ms = rd32(f + o);
        return true;
    case PEER_BLINK_REQ:
        if (o + 4 > len) return false;
        out->token = rd16(f + o);
        out->off_ms = rd16(f + o + 2);
        return true;
    case PEER_BLINK_ACK:
        if (o + 2 > len) return false;
        out->token = rd16(f + o);
        return true;
    default:
        return false;
    }
}

static size_t put_str(uint8_t *b, size_t o, size_t cap, const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n > max) n = max;
    if (o + 1 + n > cap) return 0;
    b[o] = (uint8_t)n;
    memcpy(b + o + 1, s, n);
    return o + 1 + n;
}

static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

size_t peer_encode(const peer_msg_t *m, uint8_t *b, size_t cap)
{
    if (!m || !b || cap < 60) return 0;
    memset(b, 0, cap < 128 ? cap : 128);
    memcpy(b, m->dst, 6);
    memcpy(b + 6, m->src, 6);
    b[12] = PEER_ETHERTYPE >> 8; b[13] = PEER_ETHERTYPE & 0xFF;
    size_t o = 14;
    memcpy(b + o, MAGIC, 4);
    b[o + 4] = PEER_VERSION;
    b[o + 5] = m->type;
    wr16(b + o + 6, m->seq);
    o += 8;
    switch (m->type) {
    case PEER_HELLO:
        if (o + 8 > cap) return 0;
        wr32(b + o, m->uptime_s); b[o + 4] = m->speed; b[o + 5] = m->full ? 1 : 0; wr16(b + o + 6, m->pvid);
        o += 8;
        if (!(o = put_str(b, o, cap, m->chassis, sizeof m->chassis - 1))) return 0;
        if (!(o = put_str(b, o, cap, m->port, sizeof m->port - 1))) return 0;
        if (!(o = put_str(b, o, cap, m->fw, sizeof m->fw - 1))) return 0;
        if (o + 1 > cap) return 0;
        b[o++] = m->probe <= 9 ? (uint8_t)(m->probe + 1) : 0;
        break;
    case PEER_ECHO_REQ:
    case PEER_ECHO_REP:
        wr32(b + o, m->t_ms); o += 4; break;
    case PEER_BLINK_REQ:
        wr16(b + o, m->token); wr16(b + o + 2, m->off_ms); o += 4; break;
    case PEER_BLINK_ACK:
        wr16(b + o, m->token); o += 2; break;
    default:
        return 0;
    }
    return o < 60 ? 60 : o;
}

const char *peer_type_name(uint8_t t)
{
    switch (t) {
    case PEER_HELLO: return "hello";
    case PEER_ECHO_REQ: return "echo-req";
    case PEER_ECHO_REP: return "echo-rep";
    case PEER_BLINK_REQ: return "blink-req";
    case PEER_BLINK_ACK: return "blink-ack";
    default: return "unknown";
    }
}

void peer_census_reset(peer_table_t *t) { t->nmacs = 0; t->macs_full = false; }

void peer_table_reset(peer_table_t *t)
{
    uint32_t g = t->generation;
    memset(t, 0, sizeof *t);
    t->generation = g + 1;
}

void peer_census(peer_table_t *t, const uint8_t mac[6], const uint8_t self[6])
{
    if (mac[0] & 0x01) return;                          /* group address as source: bogus */
    if (self && !memcmp(mac, self, 6)) return;
    for (int i = 0; i < t->nmacs; i++) if (!memcmp(t->macs[i], mac, 6)) return;
    if (t->nmacs >= PEER_MACS_MAX) { t->macs_full = true; return; }
    memcpy(t->macs[t->nmacs++], mac, 6);
}

peer_t *peer_find(peer_table_t *t, const uint8_t mac[6])
{
    for (int i = 0; i < PEER_MAX; i++) if (t->p[i].used && !memcmp(t->p[i].mac, mac, 6)) return &t->p[i];
    return NULL;
}

peer_t *peer_hello(peer_table_t *t, const peer_msg_t *m, uint32_t now_ms)
{
    peer_t *p = peer_find(t, m->src);
    if (!p) {
        for (int i = 0; i < PEER_MAX && !p; i++) if (!t->p[i].used) p = &t->p[i];
        if (!p) return NULL;
        memset(p, 0, sizeof *p);
        p->used = true;
        memcpy(p->mac, m->src, 6);
        p->first_ms = now_ms;
        t->generation++;
    } else if (strcmp(p->info.chassis, m->chassis) || strcmp(p->info.port, m->port) || p->info.pvid != m->pvid ||
               p->info.speed != m->speed || p->info.probe != m->probe) {
        t->generation++;
    }
    p->last_ms = now_ms;
    p->hellos++;
    p->info = *m;
    return p;
}

void peer_echo_reply(peer_table_t *t, const peer_msg_t *m, uint32_t now_ms)
{
    peer_t *p = peer_find(t, m->src);
    if (!p) return;
    uint32_t rtt = now_ms - m->t_ms;
    p->echoes++;
    p->rtt_last_ms = rtt;
    if (p->echoes == 1 || rtt < p->rtt_min_ms) p->rtt_min_ms = rtt;
    p->last_ms = now_ms;
}

void peer_expire(peer_table_t *t, uint32_t now_ms, uint32_t max_age_ms)
{
    for (int i = 0; i < PEER_MAX; i++)
        if (t->p[i].used && now_ms - t->p[i].last_ms > max_age_ms) { t->p[i].used = false; t->generation++; }
}

int peer_count(const peer_table_t *t)
{
    int n = 0;
    for (int i = 0; i < PEER_MAX; i++) n += t->p[i].used;
    return n;
}

path_verdict_t path_verdict(bool acked, bool own_link_dropped, bool peer_went_silent, bool peer_came_back)
{
    if (!acked) return PATH_NO_ANSWER;
    if (own_link_dropped) return PATH_DIRECT;
    if (peer_went_silent) return peer_came_back ? PATH_SWITCHED : PATH_SWITCHED_PEER_LOST;
    return PATH_INCONCLUSIVE;
}

const char *path_verdict_name(path_verdict_t v)
{
    switch (v) {
    case PATH_DIRECT: return "direct";
    case PATH_SWITCHED: return "switched";
    case PATH_SWITCHED_PEER_LOST: return "switched-peer-lost";
    case PATH_NO_ANSWER: return "no-answer";
    case PATH_INCONCLUSIVE: return "inconclusive";
    default: return "none";
    }
}
