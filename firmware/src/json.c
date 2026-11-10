/*
 * json.c - see json.h.
 */
#include "json.h"
#include <stdint.h>
#include "lldp.h"
#include "cdp.h"
#include "neighbor_table.h"

#include <string.h>

/* ---- low-level writer -------------------------------------------------- */

static void putc_(jw_t *w, char c)
{
    if (w->pos + 1 < w->cap) w->buf[w->pos] = c;
    w->pos++;
    if (w->cap) w->buf[w->pos < w->cap ? w->pos : w->cap - 1] = '\0';
}

static void puts_(jw_t *w, const char *s)
{
    while (*s) putc_(w, *s++);
}

void jw_init(jw_t *w, char *buf, size_t cap)
{
    memset(w, 0, sizeof *w);
    w->buf = buf;
    w->cap = cap;
    if (cap) buf[0] = '\0';
}

bool jw_overflow(const jw_t *w) { return w->pos + 1 > w->cap; }
size_t jw_len(const jw_t *w) { return w->pos; }

/* Called before every value (and every key). */
static void sep(jw_t *w)
{
    if (w->after_key) { w->after_key = false; return; }
    if (w->has_item[w->depth]) putc_(w, ',');
    w->has_item[w->depth] = true;
}

static void push(jw_t *w)
{
    if (w->depth + 1 < JW_MAX_DEPTH) w->depth++;
    w->has_item[w->depth] = false;
}
static void pop(jw_t *w)
{
    if (w->depth) w->depth--;
}

void jw_obj_begin(jw_t *w) { sep(w); putc_(w, '{'); push(w); }
void jw_obj_end(jw_t *w)   { pop(w); putc_(w, '}'); }
void jw_arr_begin(jw_t *w) { sep(w); putc_(w, '['); push(w); }
void jw_arr_end(jw_t *w)   { pop(w); putc_(w, ']'); }

static void write_string(jw_t *w, const char *s, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    putc_(w, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  puts_(w, "\\\""); break;
        case '\\': puts_(w, "\\\\"); break;
        case '\n': puts_(w, "\\n"); break;
        case '\r': puts_(w, "\\r"); break;
        case '\t': puts_(w, "\\t"); break;
        default:
            if (c < 0x20 || c >= 0x7F) {
                /* Control bytes and anything non-ASCII are escaped as
                 * \u00XX (Latin-1 view) so the output is always valid JSON
                 * even if the switch sent garbage. */
                puts_(w, "\\u00");
                putc_(w, hx[c >> 4]);
                putc_(w, hx[c & 0xF]);
            } else {
                putc_(w, (char)c);
            }
        }
    }
    putc_(w, '"');
}

void jw_key(jw_t *w, const char *key)
{
    sep(w);
    write_string(w, key, strlen(key));
    putc_(w, ':');
    w->after_key = true;
}

void jw_strn(jw_t *w, const char *s, size_t n) { sep(w); write_string(w, s, n); }
void jw_str(jw_t *w, const char *s) { jw_strn(w, s, strlen(s)); }

static void write_uint(jw_t *w, uint32_t v)
{
    char tmp[11];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) putc_(w, tmp[--n]);
}

void jw_uint(jw_t *w, uint32_t v) { sep(w); write_uint(w, v); }
void jw_int(jw_t *w, int32_t v)
{
    sep(w);
    if (v < 0) { putc_(w, '-'); write_uint(w, (uint32_t)(-(v + 1)) + 1u); }
    else write_uint(w, (uint32_t)v);
}
void jw_bool(jw_t *w, bool v) { sep(w); puts_(w, v ? "true" : "false"); }
void jw_null(jw_t *w) { sep(w); puts_(w, "null"); }

void jw_fixed(jw_t *w, uint32_t value, unsigned scale)
{
    sep(w);
    uint32_t div = 1;
    for (unsigned i = 0; i < scale; i++) div *= 10;
    write_uint(w, value / div);
    if (scale) {
        putc_(w, '.');
        uint32_t frac = value % div;
        for (uint32_t d = div / 10; d; d /= 10) putc_(w, (char)('0' + (frac / d) % 10));
    }
}

void jw_kv_str(jw_t *w, const char *k, const char *v)  { jw_key(w, k); jw_str(w, v); }
void jw_kv_uint(jw_t *w, const char *k, uint32_t v)    { jw_key(w, k); jw_uint(w, v); }
void jw_kv_bool(jw_t *w, const char *k, bool v)        { jw_key(w, k); jw_bool(w, v); }

/* ---- status message ---------------------------------------------------- */

static void mac_str(const uint8_t *m, char *out /* 18 */)
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        out[i * 3] = hx[m[i] >> 4];
        out[i * 3 + 1] = hx[m[i] & 0xF];
        out[i * 3 + 2] = i == 5 ? '\0' : ':';
    }
}

static void ip4_str(const uint8_t *ip, char *out /* 16 */)
{
    char *p = out;
    for (int i = 0; i < 4; i++) {
        unsigned v = ip[i];
        if (v >= 100) *p++ = (char)('0' + v / 100);
        if (v >= 10) *p++ = (char)('0' + v / 10 % 10);
        *p++ = (char)('0' + v % 10);
        *p++ = i < 3 ? '.' : '\0';
    }
}
static void jw_kv_ip(jw_t *w, const char *k, const uint8_t *ip) { char s[16]; ip4_str(ip, s); jw_kv_str(w, k, s); }
static void jw_kv_ips(jw_t *w, const char *k, const uint8_t (*ips)[4], unsigned n)
{
    if (!n) return;
    char s[16];
    jw_key(w, k); jw_arr_begin(w);
    for (unsigned i = 0; i < n; i++) { ip4_str(ips[i], s); jw_str(w, s); }
    jw_arr_end(w);
}
static void jw_kv_int(jw_t *w, const char *k, int32_t v) { jw_key(w, k); jw_int(w, v); }
static void jw_kv_mac(jw_t *w, const char *k, const uint8_t *mac) { char s[18]; mac_str(mac, s); jw_kv_str(w, k, s); }

/* The DHCP reply as decoded from the wire (dhcp_info.c): everything the
 * server offered beyond what lwIP keeps. */
static void write_dhcp_detail(jw_t *w, const dhcp_info_state_t *d)
{
    jw_kv_uint(w, "offers", d->offers);
    jw_kv_uint(w, "acks", d->acks);
    if (d->naks) jw_kv_uint(w, "naks", d->naks);
    if (d->acquire_ms) jw_kv_uint(w, "acquire_ms", d->acquire_ms);
    jw_kv_ips(w, "servers", d->servers, d->n_servers);
    const dhcp_reply_t *r = &d->ack;
    if (!r->valid) return;
    jw_kv_str(w, "msg", r->msg_type == 5 ? "ack" : r->msg_type == 2 ? "offer" : "nak");
    jw_kv_mac(w, "server_mac", r->server_mac);
    jw_kv_ip(w, "server_id", r->server_id);
    if (r->relayed) jw_kv_ip(w, "relay", r->giaddr);
    if (r->t1_s) jw_kv_uint(w, "t1_s", r->t1_s);
    if (r->t2_s) jw_kv_uint(w, "t2_s", r->t2_s);
    jw_kv_ips(w, "routers", r->routers, r->n_routers);
    jw_kv_ips(w, "dns_all", r->dns, r->n_dns);
    jw_kv_ips(w, "ntp", r->ntp, r->n_ntp);
    jw_kv_ips(w, "wins", r->wins, r->n_wins);
    jw_kv_ips(w, "tftp_ips", r->tftp_ips, r->n_tftp_ips);
    if (r->has_broadcast) jw_kv_ip(w, "broadcast", r->broadcast);
    if (r->domain[0]) jw_kv_str(w, "domain", r->domain);
    if (r->search[0]) jw_kv_str(w, "search", r->search);
    if (r->tftp_name[0]) jw_kv_str(w, "tftp", r->tftp_name);
    if (r->bootfile[0]) jw_kv_str(w, "bootfile", r->bootfile);
    if (r->wpad[0]) jw_kv_str(w, "wpad", r->wpad);
    if (r->vendor_len) {
        static const char hex[] = "0123456789abcdef";
        char v[DHCP_INFO_VENDOR * 2 + 1];
        for (unsigned i = 0; i < r->vendor_len; i++) { v[2 * i] = hex[r->vendor[i] >> 4]; v[2 * i + 1] = hex[r->vendor[i] & 15]; }
        v[2 * r->vendor_len] = '\0';
        jw_kv_str(w, "vendor", v);
        if (r->vendor_total > r->vendor_len) jw_kv_uint(w, "vendor_len", r->vendor_total);
    }
    jw_key(w, "options"); jw_arr_begin(w);
    for (unsigned i = 0; i < r->n_opts; i++) jw_uint(w, r->opts[i]);
    jw_arr_end(w);
    if (r->raw_len) {
        static const char hex[] = "0123456789abcdef";
        static char h[DHCP_INFO_RAW * 2 + 1];
        for (unsigned i = 0; i < r->raw_len; i++) { h[2 * i] = hex[r->raw[i] >> 4]; h[2 * i + 1] = hex[r->raw[i] & 15]; }
        h[2 * r->raw_len] = '\0';
        jw_kv_str(w, "raw", h);
        if (r->raw_truncated) jw_kv_bool(w, "raw_truncated", true);
    }
    if (r->truncated) jw_kv_bool(w, "truncated", true);
}

static void write_stp(jw_t *w, const stp_state_t *s, uint32_t link_up_for_s, uint32_t now_ms)
{
    jw_key(w, "stp"); jw_obj_begin(w);
    jw_kv_bool(w, "seen", s->seen);
    if (link_up_for_s) jw_kv_uint(w, "link_up_for_s", link_up_for_s);
    if (s->seen) {
        const stp_bpdu_t *b = &s->last;
        jw_kv_uint(w, "count", s->count);
        jw_kv_uint(w, "tc_count", s->tc_count);
        if (s->root_changes) jw_kv_uint(w, "root_changes", s->root_changes);
        if (s->interval_ms) jw_kv_uint(w, "interval_ms", s->interval_ms);
        jw_kv_uint(w, "last_s", (now_ms - s->last_ms) / 1000u);
        jw_kv_uint(w, "first_s", (now_ms - s->first_ms) / 1000u);
        jw_kv_str(w, "proto", stp_version_name(b->version));
        jw_kv_str(w, "type", stp_type_name(b->type));
        if (b->pvst) jw_kv_bool(w, "pvst", true);
        if (b->vlan) jw_kv_uint(w, "vlan", b->vlan);
        if (b->tagged) jw_kv_bool(w, "tagged", true);
        jw_kv_mac(w, "src_mac", b->src_mac);
        if (b->type != STP_TYPE_TCN) {
            jw_kv_uint(w, "flags", b->flags);
            if (b->version != STP_VERSION_STP) jw_kv_str(w, "role", stp_role_name(b->flags));
            jw_kv_str(w, "state", b->version == STP_VERSION_STP ? "unknown" : stp_state_name(b->flags));
            if (b->flags & STP_FLAG_TC) jw_kv_bool(w, "tc", true);
            if (b->flags & STP_FLAG_PROPOSAL) jw_kv_bool(w, "proposal", true);
            if (b->flags & STP_FLAG_AGREEMENT) jw_kv_bool(w, "agreement", true);
            if (b->flags & STP_FLAG_TC_ACK) jw_kv_bool(w, "tc_ack", true);
            bool is_root = b->root_priority == b->bridge_priority && memcmp(b->root_mac, b->bridge_mac, 6) == 0;
            jw_key(w, "root"); jw_obj_begin(w);
            jw_kv_uint(w, "priority", b->root_priority);
            jw_kv_mac(w, "mac", b->root_mac);
            jw_kv_uint(w, "cost", b->root_cost);
            jw_kv_bool(w, "is_neighbor", is_root);
            jw_obj_end(w);
            jw_key(w, "bridge"); jw_obj_begin(w);
            jw_kv_uint(w, "priority", b->bridge_priority);
            jw_kv_mac(w, "mac", b->bridge_mac);
            jw_obj_end(w);
            if (b->mst) {
                jw_key(w, "mst"); jw_obj_begin(w);
                jw_kv_str(w, "region", b->mst_region);
                jw_kv_uint(w, "revision", b->mst_revision);
                jw_key(w, "regional_root"); jw_obj_begin(w);
                jw_kv_uint(w, "priority", b->regional_root_priority);
                jw_kv_mac(w, "mac", b->regional_root_mac);
                jw_kv_bool(w, "is_neighbor", b->regional_root_priority == b->bridge_priority && memcmp(b->regional_root_mac, b->bridge_mac, 6) == 0);
                jw_obj_end(w);
                jw_kv_uint(w, "internal_cost", b->cist_internal_cost);
                jw_kv_uint(w, "hops", b->cist_hops);
                jw_kv_uint(w, "instances", b->msti_count);
                jw_obj_end(w);
            }
            jw_kv_uint(w, "port_id", b->port_id);
            jw_kv_uint(w, "port_priority", (b->port_id >> 12) * 16u);
            jw_kv_uint(w, "port_number", b->port_id & 0x0FFF);
            jw_key(w, "msg_age_s"); jw_fixed(w, (b->msg_age_256 * 100u + 128u) / 256u, 2);
            jw_kv_uint(w, "max_age_s", b->max_age_256 / 256u);
            jw_kv_uint(w, "hello_s", b->hello_256 / 256u);
            jw_kv_uint(w, "fwd_delay_s", b->fwd_delay_256 / 256u);
        }
    }
    jw_obj_end(w);
}

static void write_tool(jw_t *w, const tool_state_t *t, uint32_t now_ms)
{
    jw_key(w, "tool"); jw_obj_begin(w);
    jw_kv_str(w, "kind", t->kind == TOOL_PING ? "ping" : t->kind == TOOL_TRACE ? "trace" : "idle");
    if (t->kind != TOOL_IDLE) {
        jw_kv_bool(w, "running", t->running);
        jw_kv_str(w, "host", t->host);
        if (t->resolved) jw_kv_ip(w, "addr", t->addr);
        if (t->error[0]) jw_kv_str(w, "error", t->error);
        jw_kv_uint(w, "age_s", (now_ms - (t->running ? t->started_ms : t->finished_ms)) / 1000u);
        jw_kv_uint(w, "generation", t->generation);
        if (t->kind == TOOL_PING) {
            jw_kv_uint(w, "count", t->count);
            jw_kv_uint(w, "sent", t->sent);
            jw_kv_uint(w, "recv", t->recv);
            if (t->recv) {
                jw_kv_uint(w, "min_ms", t->min_ms);
                jw_kv_uint(w, "avg_ms", t->sum_ms / t->recv);
                jw_kv_uint(w, "max_ms", t->max_ms);
                jw_kv_uint(w, "last_ms", t->last_ms);
            }
            /* rtts: the last min(sent, TOOL_PING_MAX) probes in order; first_seq is the 1-based number of rtts[0] */
            unsigned n = t->sent < TOOL_PING_MAX ? t->sent : TOOL_PING_MAX;
            jw_kv_uint(w, "first_seq", t->sent - n + 1);
            jw_key(w, "rtts"); jw_arr_begin(w);
            for (unsigned i = 0; i < n; i++) { uint16_t r = t->rtts[(t->sent - n + i) % TOOL_PING_MAX]; if (r == TOOL_RTT_TIMEOUT) jw_null(w); else jw_uint(w, r); }
            jw_arr_end(w);
        } else {
            jw_kv_bool(w, "reached", t->reached);
            jw_key(w, "hops"); jw_arr_begin(w);
            for (unsigned i = 0; i < t->n_hops; i++) {
                const tool_hop_t *h = &t->hops[i];
                jw_obj_begin(w);
                jw_kv_uint(w, "ttl", h->ttl);
                if (h->answered) { jw_kv_ip(w, "addr", h->addr); jw_kv_uint(w, "ms", h->ms); }
                if (h->reached) jw_kv_bool(w, "reached", true);
                if (h->unreachable) jw_kv_bool(w, "unreachable", true);
                if (!h->answered && !(t->running && i + 1 == t->n_hops)) jw_kv_bool(w, "timeout", true);
                jw_obj_end(w);
            }
            jw_arr_end(w);
        }
    }
    jw_obj_end(w);
}

static void write_dot1x(jw_t *w, const eapol_state_t *d, uint32_t now_ms)
{
    jw_key(w, "dot1x"); jw_obj_begin(w);
    jw_kv_bool(w, "seen", d->seen);
    if (d->seen) {
        jw_kv_uint(w, "requests", d->requests);
        if (d->failures) jw_kv_uint(w, "failures", d->failures);
        jw_kv_uint(w, "first_s", (now_ms - d->first_ms) / 1000u);
        if (d->requests) {
            jw_kv_uint(w, "last_s", (now_ms - d->last_ms) / 1000u);
            if (d->interval_ms) jw_kv_uint(w, "interval_ms", d->interval_ms);
            jw_kv_mac(w, "authenticator", d->authenticator);
            jw_kv_uint(w, "version", d->version);
            if (d->eap_type) jw_kv_str(w, "eap", eap_type_name(d->eap_type));
        }
        jw_kv_uint(w, "generation", d->generation);
    }
    jw_obj_end(w);
}

static void write_lacp_info(jw_t *w, const char *key, const lacp_info_t *i)
{
    jw_key(w, key); jw_obj_begin(w);
    jw_kv_uint(w, "system_priority", i->sys_priority);
    jw_kv_mac(w, "system", i->sys_mac);
    jw_kv_uint(w, "key", i->key);
    jw_kv_uint(w, "port_priority", i->port_priority);
    jw_kv_uint(w, "port", i->port);
    jw_kv_uint(w, "state", i->state);
    jw_kv_bool(w, "active", i->state & LACP_ST_ACTIVE);
    jw_kv_bool(w, "fast", i->state & LACP_ST_FAST);
    jw_kv_bool(w, "aggregatable", i->state & LACP_ST_AGGREGATABLE);
    jw_kv_bool(w, "sync", i->state & LACP_ST_SYNC);
    jw_kv_bool(w, "collecting", i->state & LACP_ST_COLLECTING);
    jw_kv_bool(w, "distributing", i->state & LACP_ST_DISTRIBUTING);
    jw_kv_bool(w, "defaulted", i->state & LACP_ST_DEFAULTED);
    jw_kv_bool(w, "expired", i->state & LACP_ST_EXPIRED);
    jw_obj_end(w);
}

static void write_lacp(jw_t *w, const lacp_state_t *l, uint32_t now_ms)
{
    jw_key(w, "lacp"); jw_obj_begin(w);
    jw_kv_bool(w, "seen", l->seen);
    if (l->seen) {
        jw_kv_uint(w, "count", l->count);
        jw_kv_uint(w, "first_s", (now_ms - l->first_ms) / 1000u);
        jw_kv_uint(w, "last_s", (now_ms - l->last_ms) / 1000u);
        if (l->interval_ms) jw_kv_uint(w, "interval_ms", l->interval_ms);
        if (l->changes) jw_kv_uint(w, "changes", l->changes);
        jw_kv_mac(w, "src_mac", l->src_mac);
        jw_kv_uint(w, "version", l->version);
        write_lacp_info(w, "actor", &l->actor);
        write_lacp_info(w, "partner", &l->partner);
        jw_kv_uint(w, "generation", l->generation);
    }
    jw_obj_end(w);
}

static void write_igmp(jw_t *w, const igmp_state_t *g, uint32_t now_ms)
{
    jw_key(w, "igmp"); jw_obj_begin(w);
    jw_kv_bool(w, "seen", g->seen);
    if (g->reports) jw_kv_uint(w, "reports", g->reports);
    if (g->seen) {
        jw_kv_ip(w, "querier", g->querier_ip);
        jw_kv_mac(w, "querier_mac", g->querier_mac);
        jw_kv_uint(w, "version", g->version);
        jw_kv_uint(w, "queries", g->queries);
        if (g->group_queries) jw_kv_uint(w, "group_queries", g->group_queries);
        if (g->querier_changes) { jw_kv_uint(w, "querier_changes", g->querier_changes); jw_kv_ip(w, "prev_querier", g->prev_querier_ip); }
        if (g->version == 3) { jw_kv_uint(w, "qrv", g->qrv); jw_kv_bool(w, "s_flag", g->s_flag); }
        if (g->interval_ms) jw_kv_uint(w, "interval_s", (g->interval_ms + 500) / 1000u);
        if (g->vlan) jw_kv_uint(w, "vlan", g->vlan);
        if (g->n_other_vlans) {
            jw_key(w, "other_vlans"); jw_arr_begin(w);
            for (int i = 0; i < g->n_other_vlans; i++) jw_uint(w, g->other_vlans[i]);
            jw_arr_end(w);
        }
        if (g->qqi_s) jw_kv_uint(w, "qqi_s", g->qqi_s);
        jw_kv_uint(w, "max_resp_ds", g->max_resp_ds);
        jw_kv_uint(w, "first_s", (now_ms - g->first_ms) / 1000u);
        jw_kv_uint(w, "last_s", (now_ms - g->last_ms) / 1000u);
    }
    if (g->pim_seen) {
        jw_key(w, "mrouter"); jw_obj_begin(w);
        jw_kv_ip(w, "ip", g->pim_ip);
        jw_kv_mac(w, "mac", g->pim_mac);
        jw_kv_uint(w, "hellos", g->pim_hellos);
        jw_kv_uint(w, "holdtime_s", g->pim_holdtime_s);
        jw_kv_uint(w, "dr_priority", g->pim_dr_priority);
        jw_kv_uint(w, "last_s", (now_ms - g->pim_last_ms) / 1000u);
        jw_obj_end(w);
    }
    if (g->seen || g->pim_seen) jw_kv_uint(w, "generation", g->generation);
    jw_obj_end(w);
}

static void jw_kv_clockid(jw_t *w, const char *k, const uint8_t *id, unsigned n)
{
    static const char hex[] = "0123456789abcdef";
    char s[8 * 3];
    unsigned p = 0;
    for (unsigned i = 0; i < n; i++) {
        if (i) s[p++] = ':';
        s[p++] = hex[id[i] >> 4]; s[p++] = hex[id[i] & 15];
    }
    jw_key(w, k); jw_strn(w, s, p);
}

static void write_ptp_one(jw_t *w, const ptp_state_t *p, uint32_t now_ms)
{
    jw_key(w, p->version == 1 ? "v1" : "v2"); jw_obj_begin(w);
    jw_kv_str(w, "transport", p->l2 ? "l2" : "udp");
    jw_kv_uint(w, "domain", p->domain);
    if (p->version == 1 && p->subdomain[0]) jw_kv_str(w, "subdomain", p->subdomain);
    if (p->other_domains) jw_kv_uint(w, "other_domains", p->other_domains);
    jw_kv_uint(w, "syncs", p->syncs);
    jw_kv_uint(w, "announces", p->announces);
    if (p->follow_ups) jw_kv_uint(w, "follow_ups", p->follow_ups);
    if (p->delay_reqs) jw_kv_uint(w, "delay_reqs", p->delay_reqs);
    jw_kv_uint(w, "first_s", (now_ms - p->first_ms) / 1000u);
    if (p->syncs) {
        jw_kv_clockid(w, "master", p->master_id, p->version == 1 ? 6 : 8);
        static const uint8_t zero4[4] = {0, 0, 0, 0};
        if (memcmp(p->master_ip, zero4, 4) != 0) jw_kv_ip(w, "master_ip", p->master_ip);
        jw_kv_mac(w, "master_mac", p->master_mac);
        jw_kv_bool(w, "two_step", p->two_step);
        if (p->sync_interval_ms) jw_kv_uint(w, "sync_interval_ms", p->sync_interval_ms);
        if (p->version == 2) jw_kv_int(w, "log_sync_interval", p->log_sync_interval);
        jw_kv_uint(w, "last_sync_s", (now_ms - p->last_sync_ms) / 1000u);
    }
    if (p->have_gm) {
        jw_key(w, "grandmaster"); jw_obj_begin(w);
        jw_kv_clockid(w, "id", p->gm_id, 8);
        jw_kv_uint(w, "priority1", p->priority1);
        jw_kv_uint(w, "priority2", p->priority2);
        jw_kv_uint(w, "clock_class", p->clock_class);
        jw_kv_uint(w, "clock_accuracy", p->clock_accuracy);
        jw_kv_str(w, "time_source", ptp_time_source_name(p->time_source));
        jw_kv_uint(w, "steps_removed", p->steps_removed);
        jw_kv_int(w, "utc_offset", p->utc_offset);
        jw_kv_int(w, "log_announce_interval", p->log_announce_interval);
        if (p->gm_changes) jw_kv_uint(w, "changes", p->gm_changes);
        jw_kv_uint(w, "last_s", (now_ms - p->last_announce_ms) / 1000u);
        jw_obj_end(w);
    }
    jw_kv_uint(w, "generation", p->generation);
    jw_obj_end(w);
}

static void write_ptp(jw_t *w, const ptp_state_t *p1, const ptp_state_t *p2, uint32_t now_ms)
{
    jw_key(w, "ptp"); jw_obj_begin(w);
    jw_kv_bool(w, "seen", p1->seen || p2->seen);
    if (p1->seen) write_ptp_one(w, p1, now_ms);
    if (p2->seen) write_ptp_one(w, p2, now_ms);
    jw_obj_end(w);
}

static void write_blink(jw_t *w, const blink_state_t *b, uint32_t remaining_s)
{
    jw_key(w, "blink"); jw_obj_begin(w);
    jw_kv_bool(w, "active", b->active);
    if (b->active) {
        jw_kv_uint(w, "remaining_s", remaining_s);
        jw_kv_uint(w, "cycles", b->cycles);
        jw_kv_uint(w, "link_ups", b->link_ups);
        if (b->pulse) jw_kv_bool(w, "pulse", true);
    }
    jw_kv_uint(w, "generation", b->generation);
    jw_obj_end(w);
}

static void write_peers(jw_t *w, const peer_table_t *t, const uint8_t *self, uint32_t now_ms)
{
    char me[18] = "";
    if (self) mac_str(self, me);
    jw_key(w, "peers"); jw_obj_begin(w);
    int n = peer_count(t);
    jw_kv_uint(w, "count", (uint32_t)n);
    /* source MACs heard on the port that are not probes: none on a bare probe-to-probe cable */
    uint32_t others = 0;
    for (int i = 0; i < t->nmacs; i++) {
        bool is_peer = false;
        for (int j = 0; j < PEER_MAX; j++) if (t->p[j].used && !memcmp(t->p[j].mac, t->macs[i], 6)) is_peer = true;
        if (!is_peer) others++;
    }
    jw_kv_uint(w, "other_macs", others);
    if (t->macs_full) jw_kv_bool(w, "other_macs_capped", true);
    jw_key(w, "list"); jw_arr_begin(w);
    int listed = 0;
    for (int i = 0; i < PEER_MAX && listed < 4; i++) {
        const peer_t *p = &t->p[i];
        if (!p->used) continue;
        listed++;
        jw_obj_begin(w);
        jw_kv_mac(w, "mac", p->mac);
        jw_kv_uint(w, "age_s", (now_ms - p->last_ms) / 1000u);
        jw_kv_uint(w, "uptime_s", p->info.uptime_s);
        if (p->info.speed) { jw_kv_uint(w, "speed", p->info.speed); jw_kv_bool(w, "full_duplex", p->info.full); }
        if (p->info.chassis[0]) jw_kv_str(w, "chassis", p->info.chassis);
        if (p->info.port[0]) jw_kv_str(w, "port", p->info.port);
        if (p->info.pvid) jw_kv_uint(w, "pvid", p->info.pvid);
        if (me[0] && !strcmp(p->info.chassis, me)) jw_kv_bool(w, "sees_us", true);
        jw_kv_str(w, "fw", p->info.fw);
        if (p->info.probe <= 9) jw_kv_uint(w, "probe", p->info.probe);
        if (p->echoes) { jw_kv_uint(w, "rtt_ms", p->rtt_last_ms); jw_kv_uint(w, "rtt_min_ms", p->rtt_min_ms); }
        jw_obj_end(w);
    }
    jw_arr_end(w);
    jw_kv_uint(w, "generation", t->generation);
    jw_obj_end(w);
}

static const char *path_phase_name(path_phase_t p)
{
    switch (p) {
    case PT_ASK: return "asking";
    case PT_WATCH: return "watching";
    case PT_RECOVER: return "recovering";
    case PT_DONE: return "done";
    default: return "idle";
    }
}

static void write_path(jw_t *w, const path_test_t *p, uint32_t now_ms)
{
    jw_key(w, "path"); jw_obj_begin(w);
    jw_kv_str(w, "phase", path_phase_name(p->phase));
    if (p->phase != PT_IDLE) {
        jw_kv_mac(w, "peer", p->peer);
        if (p->verdict != PATH_NONE) jw_kv_str(w, "verdict", path_verdict_name(p->verdict));
        jw_kv_bool(w, "acked", p->acked_ms != 0);
        jw_kv_bool(w, "own_link_dropped", p->own_link_dropped);
        if (p->own_link_dropped) jw_kv_uint(w, "own_down_ms", p->own_down_ms);
        jw_kv_bool(w, "peer_went_silent", p->peer_went_silent);
        jw_kv_bool(w, "peer_came_back", p->peer_came_back);
        if (p->peer_came_back) jw_kv_uint(w, "peer_back_ms", p->peer_back_ms);
        uint32_t end = p->phase == PT_DONE ? p->finished_ms : now_ms;
        jw_kv_uint(w, "elapsed_ms", end - p->started_ms);
        if (p->phase == PT_DONE) jw_kv_uint(w, "age_s", (now_ms - p->finished_ms) / 1000u);
    }
    jw_kv_uint(w, "generation", p->generation);
    jw_obj_end(w);
}

static const char *power_priority_str(uint8_t p)
{
    switch (p) { case 1: return "critical"; case 2: return "high"; case 3: return "low"; default: return "unknown"; }
}

/* ---- neighbor: identity, capabilities, management addresses ---- */
static void write_neighbor_ids(jw_t *w, const lldp_neighbor_t *n)
{
    char s[LLDP_ID_STR_LEN];
    jw_kv_str(w, "proto", n->proto == NEIGHBOR_PROTO_CDP ? "cdp" : "lldp");
    if (lldp_neighbor_is_picoplug(n)) jw_kv_bool(w, "picoplug", true);
    lldp_format_chassis_id(&n->chassis_id, s, sizeof s);
    jw_kv_str(w, "chassis_id", s);
    lldp_format_port_id(&n->port_id, s, sizeof s);
    jw_kv_str(w, "port_id", s);
    mac_str(n->src_mac, s);
    jw_kv_str(w, "src_mac", s);
    if (n->vlan_tagged) jw_kv_uint(w, "frame_vlan", n->tag_vid);

    if (n->has_port_desc) jw_kv_str(w, "port_desc", n->port_desc);
    if (n->has_sys_name)  jw_kv_str(w, "sys_name", n->sys_name);
    if (n->has_sys_desc)  jw_kv_str(w, "sys_desc", n->sys_desc);

    if (n->has_caps) {
        jw_key(w, "caps"); jw_arr_begin(w);
        for (unsigned b = 0; b < 11; b++) if (n->caps_enabled & (1u << b)) jw_str(w, lldp_cap_name(b));
        jw_arr_end(w);
        jw_key(w, "caps_supported"); jw_arr_begin(w);
        for (unsigned b = 0; b < 11; b++) if (n->caps_supported & (1u << b)) jw_str(w, lldp_cap_name(b));
        jw_arr_end(w);
    }

    if (n->n_mgmt_addrs) {
        jw_key(w, "mgmt_addr"); jw_arr_begin(w);
        for (int i = 0; i < n->n_mgmt_addrs; i++) {
            const lldp_mgmt_addr_t *m = &n->mgmt_addrs[i];
            lldp_format_addr(m->afi, m->addr, m->len, s, sizeof s);
            jw_str(w, s);
        }
        jw_arr_end(w);
    }
}

/* ---- neighbor: port VLAN, VLAN names, voice and other MED policies ---- */
static void write_neighbor_vlans(jw_t *w, const lldp_neighbor_t *n)
{
    if (n->has_pvid) jw_kv_uint(w, "pvid", n->pvid);
    if (n->has_mgmt_vid) jw_kv_uint(w, "mgmt_vid", n->mgmt_vid);
    if (n->n_ppvids) {
        jw_key(w, "ppvids"); jw_arr_begin(w);
        for (int i = 0; i < n->n_ppvids; i++) {
            jw_obj_begin(w);
            jw_kv_uint(w, "id", n->ppvids[i].id);
            jw_kv_bool(w, "supported", n->ppvids[i].supported);
            jw_kv_bool(w, "enabled", n->ppvids[i].enabled);
            jw_obj_end(w);
        }
        jw_arr_end(w);
    }
    if (n->protocols[0]) jw_kv_str(w, "protocols", n->protocols);
    if (n->n_vlans) {
        jw_key(w, "vlans"); jw_arr_begin(w);
        for (int i = 0; i < n->n_vlans; i++) {
            jw_obj_begin(w);
            jw_kv_uint(w, "id", n->vlans[i].id);
            jw_kv_str(w, "name", n->vlans[i].name);
            jw_obj_end(w);
        }
        jw_arr_end(w);
    }

    /* a policy with the U flag set says "no policy defined" (Cisco sends one when the port has
       no voice VLAN), so its VLAN 0 is not a voice VLAN */
    const lldp_med_policy_t *voice = lldp_find_med_policy(n, LLDP_MED_APP_VOICE);
    if (voice && !voice->unknown_policy) {
        jw_key(w, "voice_vlan"); jw_obj_begin(w);
        jw_kv_uint(w, "id", voice->vlan_id);
        jw_kv_bool(w, "tagged", voice->tagged);
        if (!voice->no_qos) {
            jw_kv_uint(w, "priority", voice->l2_priority);
            jw_kv_uint(w, "dscp", voice->dscp);
        }
        jw_obj_end(w);
    }
    if (n->n_med_policies) {
        jw_key(w, "med_policies"); jw_arr_begin(w);
        for (int i = 0; i < n->n_med_policies; i++) {
            const lldp_med_policy_t *p = &n->med_policies[i];
            jw_obj_begin(w);
            jw_kv_str(w, "app", lldp_med_app_name(p->app_type));
            jw_kv_uint(w, "id", p->vlan_id);
            jw_kv_bool(w, "tagged", p->tagged);
            if (!p->no_qos) {
                jw_kv_uint(w, "priority", p->l2_priority);
                jw_kv_uint(w, "dscp", p->dscp);
            }
            if (p->unknown_policy) jw_kv_bool(w, "unknown_policy", true);
            jw_obj_end(w);
        }
        jw_arr_end(w);
    }
}

/* ---- neighbor: 802.3 and MED power ---- */
static void write_neighbor_power(jw_t *w, const lldp_neighbor_t *n)
{
    if (n->power_mdi.present) {
        const lldp_power_mdi_t *p = &n->power_mdi;
        jw_key(w, "poe"); jw_obj_begin(w);
        jw_kv_str(w, "port_class", p->pse ? "pse" : "pd");
        jw_kv_bool(w, "supported", p->mdi_power_supported);
        jw_kv_bool(w, "enabled", p->mdi_power_enabled);
        jw_kv_str(w, "pairs", p->power_pairs == 2 ? "spare" : "signal");
        if (p->power_class != 0xFF) jw_kv_uint(w, "class", p->power_class);
        if (p->dll_present) {
            jw_key(w, "req_w");   jw_fixed(w, p->pd_requested_dw, 1);
            jw_key(w, "alloc_w"); jw_fixed(w, p->pse_allocated_dw, 1);
            jw_kv_str(w, "priority", power_priority_str(p->power_priority));
            jw_kv_uint(w, "type", p->power_type);
            if (p->pd_4pid) jw_kv_bool(w, "pd_4pid", true);
        }
        if (p->bt_present) {
            static const char *const pse_st[] = {NULL, "2-pair", "4-pair single", "4-pair dual"};
            static const char *const pd_st[] = {NULL, "single", "2-pair dual", "4-pair dual"};
            static const char *const pairs[] = {NULL, "a", "b", "both"};
            jw_key(w, "bt"); jw_obj_begin(w);
            if (p->class_ext >= 1 && p->class_ext <= 8) jw_kv_uint(w, "class", p->class_ext);
            else if (p->class_ext == 15) jw_kv_str(w, "class", "dual");
            if (p->class_a >= 1 && p->class_a <= 5) jw_kv_uint(w, "class_a", p->class_a);
            if (p->class_b >= 1 && p->class_b <= 5) jw_kv_uint(w, "class_b", p->class_b);
            if (p->type_ext <= 5) jw_kv_uint(w, "type", p->type_ext <= 1 ? 3 + p->type_ext : p->type_ext <= 3 ? 3 : 4);
            if (p->type_ext <= 5) jw_kv_str(w, "device", p->type_ext <= 1 ? "pse" : "pd");
            if (pse_st[p->pse_status]) jw_kv_str(w, "powering", pse_st[p->pse_status]);
            if (pd_st[p->pd_status]) jw_kv_str(w, "pd", pd_st[p->pd_status]);
            if (pairs[p->pairs_ext]) jw_kv_str(w, "pairs", pairs[p->pairs_ext]);
            if (p->req_a_dw)   { jw_key(w, "req_a_w");   jw_fixed(w, p->req_a_dw, 1); }
            if (p->req_b_dw)   { jw_key(w, "req_b_w");   jw_fixed(w, p->req_b_dw, 1); }
            if (p->alloc_a_dw) { jw_key(w, "alloc_a_w"); jw_fixed(w, p->alloc_a_dw, 1); }
            if (p->alloc_b_dw) { jw_key(w, "alloc_b_w"); jw_fixed(w, p->alloc_b_dw, 1); }
            if (p->pse_max_dw) { jw_key(w, "max_w");     jw_fixed(w, p->pse_max_dw, 1); }
            if (p->pd_load) jw_kv_bool(w, "pd_load", true);
            jw_obj_end(w);
        }
        jw_obj_end(w);
    }
    if (n->has_upoe) {
        jw_key(w, "upoe"); jw_obj_begin(w);
        jw_kv_bool(w, "supported", n->upoe & 0x01);
        jw_kv_bool(w, "altb_detect", n->upoe & 0x02);
        jw_kv_bool(w, "spare_desired", n->upoe & 0x04);
        jw_kv_bool(w, "spare_on", n->upoe & 0x08);
        jw_obj_end(w);
    }
    if (n->med_power.present) {
        jw_key(w, "med_power"); jw_obj_begin(w);
        jw_kv_str(w, "source", n->med_power.power_type ? "pd" : "pse");
        jw_key(w, "w"); jw_fixed(w, n->med_power.power_dw, 1);
        jw_kv_str(w, "priority", power_priority_str(n->med_power.power_priority));
        jw_obj_end(w);
    }
}

/* ---- neighbor: MAC/PHY, frame size, link aggregation, inventory, MED caps ---- */
static void write_neighbor_phy(jw_t *w, const lldp_neighbor_t *n)
{
    if (n->macphy.present) {
        jw_key(w, "mac_phy"); jw_obj_begin(w);
        jw_kv_bool(w, "autoneg_supported", n->macphy.autoneg_supported);
        jw_kv_bool(w, "autoneg_enabled", n->macphy.autoneg_enabled);
        jw_kv_uint(w, "pmd_adv", n->macphy.pmd_adv);
        jw_kv_uint(w, "mau_type", n->macphy.mau_type);
        jw_obj_end(w);
    }
    if (n->has_max_frame) jw_kv_uint(w, "max_frame", n->max_frame_size);
    if (n->has_eee) {
        jw_key(w, "eee"); jw_obj_begin(w);
        jw_kv_uint(w, "tx_us", n->eee_tx_us);
        jw_kv_uint(w, "rx_us", n->eee_rx_us);
        jw_obj_end(w);
    }
    if (n->has_link_agg) {
        jw_key(w, "link_agg"); jw_obj_begin(w);
        jw_kv_bool(w, "capable", n->link_agg_capable);
        jw_kv_bool(w, "active", n->link_agg_active);
        if (n->link_agg_active) jw_kv_uint(w, "port_id", n->link_agg_port_id);
        jw_obj_end(w);
    }
    if (n->inv_serial[0] || n->inv_model[0] || n->inv_manufacturer[0] || n->inv_hw[0] || n->inv_fw[0] || n->inv_sw[0] || n->inv_asset[0]) {
        jw_key(w, "inventory"); jw_obj_begin(w);
        if (n->inv_serial[0]) jw_kv_str(w, "serial", n->inv_serial);
        if (n->inv_model[0]) jw_kv_str(w, "model", n->inv_model);
        if (n->inv_manufacturer[0]) jw_kv_str(w, "manufacturer", n->inv_manufacturer);
        if (n->inv_hw[0]) jw_kv_str(w, "hw", n->inv_hw);
        if (n->inv_fw[0]) jw_kv_str(w, "fw", n->inv_fw);
        if (n->inv_sw[0]) jw_kv_str(w, "sw", n->inv_sw);
        if (n->inv_asset[0]) jw_kv_str(w, "asset", n->inv_asset);
        jw_obj_end(w);
    }
    if (n->has_med_caps) {
        jw_key(w, "med"); jw_obj_begin(w);
        jw_kv_uint(w, "caps", n->med_caps);
        jw_kv_uint(w, "device_type", n->med_device_type);
        jw_obj_end(w);
    }
    if (n->location_format && n->location[0]) {
        jw_key(w, "location"); jw_obj_begin(w);
        jw_kv_str(w, "format", n->location_format == LLDP_LOC_COORD ? "coordinates" :
                               n->location_format == LLDP_LOC_CIVIC ? "civic" : "elin");
        jw_kv_str(w, "text", n->location);
        if (n->location_format == LLDP_LOC_CIVIC && n->civic_len >= 2) {
            /* every address element as sent (RFC 4776 CAtypes 1-39; language, script and private ones left out) */
            if (n->civic[0] > ' ' && n->civic[1] > ' ') { jw_key(w, "country"); jw_strn(w, (const char *)n->civic, 2); }
            jw_key(w, "civic"); jw_arr_begin(w);
            for (size_t p = 2; p + 2 <= n->civic_len; p += 2 + (size_t)n->civic[p + 1]) {
                uint8_t t = n->civic[p], len = n->civic[p + 1];
                if (t < 1 || t > 39 || !len) continue;
                jw_obj_begin(w);
                jw_kv_uint(w, "type", t);
                jw_key(w, "value"); jw_strn(w, (const char *)&n->civic[p + 2], len);
                jw_obj_end(w);
            }
            jw_arr_end(w);
        }
        jw_obj_end(w);
    }
    if (n->mud_url[0]) jw_kv_str(w, "mud_url", n->mud_url);
}

/* ---- neighbor: CDP-only fields ---- */
static void write_neighbor_cdp(jw_t *w, const lldp_neighbor_t *n)
{
    if (n->proto == NEIGHBOR_PROTO_CDP) {
        jw_kv_uint(w, "cdp_version", n->cdp_version);
        if (!n->checksum_ok) jw_kv_bool(w, "checksum_ok", false);
        jw_key(w, "cdp_caps"); jw_arr_begin(w);
        for (unsigned b = 0; b < 11; b++) if (n->cdp_caps & (1u << b)) jw_str(w, cdp_cap_name(b));
        jw_arr_end(w);
    }
    if (n->has_platform)   jw_kv_str(w, "platform", n->platform);
    if (n->has_vtp_domain) jw_kv_str(w, "vtp_domain", n->vtp_domain);
    if (n->has_duplex)     jw_kv_str(w, "duplex", n->duplex_full ? "full" : "half");
    if (n->has_mtu)        jw_kv_uint(w, "mtu", n->mtu);
    if (n->has_power_mw || n->has_power_req_mw || n->has_power_avail_mw) {
        jw_key(w, "cdp_power"); jw_obj_begin(w);
        if (n->has_power_mw)       { jw_key(w, "consumption_w"); jw_fixed(w, n->power_mw, 3); }
        if (n->has_power_req_mw)   { jw_key(w, "requested_w");   jw_fixed(w, n->power_req_mw, 3); }
        if (n->has_power_avail_mw) { jw_key(w, "available_w");   jw_fixed(w, n->power_avail_mw, 3); }
        jw_obj_end(w);
    }
}

static void write_neighbor(jw_t *w, const neighbor_entry_t *e, uint32_t now_ms)
{
    const lldp_neighbor_t *n = &e->n;
    jw_obj_begin(w);
    write_neighbor_ids(w, n);
    write_neighbor_vlans(w, n);
    write_neighbor_power(w, n);
    write_neighbor_phy(w, n);
    write_neighbor_cdp(w, n);
    if (n->n_unknown) jw_kv_uint(w, "unknown_tlvs", n->n_unknown);
    if (n->truncated) jw_kv_bool(w, "truncated", true);

    jw_kv_uint(w, "ttl", n->ttl);
    jw_kv_uint(w, "ttl_remaining_s", neighbor_entry_ttl_remaining_s(e, now_ms));
    jw_kv_uint(w, "last_seen_s", (now_ms - e->last_seen_ms) / 1000u);
    jw_kv_uint(w, "first_seen_s", (now_ms - e->first_seen_ms) / 1000u);
    jw_obj_end(w);
}

/* ---- eth: link and W5500 driver counters ---- */
static void write_eth(jw_t *w, const status_info_t *si)
{
    jw_key(w, "eth"); jw_obj_begin(w);
    if (si->self_mac) jw_kv_mac(w, "mac", si->self_mac);
    jw_kv_bool(w, "link", si->eth_link);
    if (si->eth_link) {
        jw_kv_uint(w, "speed", si->eth_speed);
        jw_kv_str(w, "duplex", si->eth_full ? "full" : "half");
    }
    jw_kv_bool(w, "chip_ok", si->eth_chip_ok);
    jw_kv_uint(w, "version", si->eth_version);
    jw_kv_uint(w, "probe_w5100s", si->eth_probe_w5100s);
    jw_kv_uint(w, "phycfgr", si->eth_phycfgr);
    jw_kv_uint(w, "sock_sr", si->eth_sn_sr);
    jw_kv_uint(w, "frames", si->eth_frames);
    jw_kv_uint(w, "resyncs", si->eth_resyncs);
    jw_kv_uint(w, "tx_frames", si->eth_tx_frames);
    jw_kv_uint(w, "tx_fail", si->eth_tx_fail);
    if (si->eth_tx_nolink) jw_kv_uint(w, "tx_nolink", si->eth_tx_nolink);
    if (si->eth_cr_timeouts) jw_kv_uint(w, "cr_timeouts", si->eth_cr_timeouts);
    jw_kv_uint(w, "init_attempts", si->eth_init_attempts);
    jw_kv_uint(w, "spi_hz", si->eth_spi_hz);
    jw_kv_uint(w, "polls", si->eth_polls);
    jw_kv_uint(w, "bad_headers", si->eth_bad_headers);
    jw_kv_uint(w, "lldp_frames", si->eth_lldp_frames);
    jw_kv_uint(w, "cdp_frames", si->eth_cdp_frames);
    jw_kv_uint(w, "parse_errors", si->eth_parse_errors);
    jw_kv_uint(w, "last_rsr", si->eth_last_rsr);
    jw_kv_uint(w, "last_hdr", si->eth_last_hdr);
    if (si->eth_last_dst) {
        char m[18];
        mac_str(si->eth_last_dst, m);
        jw_kv_str(w, "last_dst", m);
    }
    jw_obj_end(w);
}

/* ---- port.dhcp: lwIP client state and lease, plus the wire-level detail ---- */
static void write_port_dhcp(jw_t *w, const status_info_t *si, const port_info_t *p)
{
    jw_key(w, "dhcp"); jw_obj_begin(w);
    jw_kv_str(w, "state", p->dhcp_state);
    jw_kv_bool(w, "bound", p->dhcp_bound);
    if (p->dhcp_bound) {
        jw_kv_str(w, "ip", p->ip);
        jw_kv_str(w, "mask", p->mask);
        jw_kv_str(w, "gateway", p->gateway);
        jw_kv_str(w, "server", p->server);
        jw_key(w, "dns"); jw_arr_begin(w);
        for (int i = 0; i < p->n_dns; i++) jw_str(w, p->dns[i]);
        jw_arr_end(w);
        jw_kv_uint(w, "lease_s", p->lease_s);
        jw_kv_uint(w, "bound_for_s", p->bound_for_s);
    }
    if (si->dhcp_detail) write_dhcp_detail(w, si->dhcp_detail);
    jw_obj_end(w);
}

/* ---- port.probe: gateway / internet / DNS checks ---- */
static void write_port_probe(jw_t *w, const port_info_t *p)
{
    char m[18];
    jw_key(w, "probe"); jw_obj_begin(w);
    jw_kv_str(w, "phase", p->probe_phase);
    jw_kv_bool(w, "running", p->probe_running);
    jw_kv_uint(w, "runs", p->probe_runs);
    if (p->probe_valid) {
        jw_kv_uint(w, "age_s", p->probe_age_s);
        jw_key(w, "gateway"); jw_obj_begin(w);
        jw_kv_bool(w, "arp", p->gw_arp_ok);
        if (p->gw_arp_ok && p->gw_mac) { mac_str(p->gw_mac, m); jw_kv_str(w, "mac", m); }
        jw_kv_bool(w, "reachable", p->gw_replies > 0);
        jw_kv_uint(w, "sent", p->gw_sent);
        jw_kv_uint(w, "replies", p->gw_replies);
        if (p->gw_replies) jw_kv_uint(w, "rtt_ms", p->gw_rtt_ms);
        jw_obj_end(w);
        jw_key(w, "internet"); jw_obj_begin(w);
        jw_kv_str(w, "target", p->inet_target);
        jw_kv_bool(w, "reachable", p->inet_replies > 0);
        jw_kv_uint(w, "sent", p->inet_sent);
        jw_kv_uint(w, "replies", p->inet_replies);
        if (p->inet_replies) jw_kv_uint(w, "rtt_ms", p->inet_rtt_ms);
        jw_obj_end(w);
        jw_key(w, "dns"); jw_obj_begin(w);
        jw_kv_str(w, "name", p->dns_name);
        if (p->dns_server && p->dns_server[0]) { jw_kv_str(w, "server", p->dns_server); if (p->dns_custom) jw_kv_bool(w, "custom", true); }
        jw_kv_bool(w, "ok", p->dns_ok);
        if (p->dns_ok) jw_kv_str(w, "addr", p->dns_addr);
        jw_kv_uint(w, "ms", p->dns_ms);
        jw_obj_end(w);
    }
    jw_obj_end(w);
}

static void write_port(jw_t *w, const status_info_t *si)
{
    const port_info_t *p = si->port;
    jw_key(w, "port"); jw_obj_begin(w);
    jw_kv_bool(w, "link", p->link);
    write_port_dhcp(w, si, p);
    write_port_probe(w, p);
    jw_obj_end(w);
}

static void write_tx(jw_t *w, const status_info_t *si)
{
if (si->tx_present) {
    jw_key(w, "tx"); jw_obj_begin(w);
    jw_key(w, "lldp"); jw_obj_begin(w);
    jw_kv_bool(w, "enabled", si->tx_lldp_on);
    jw_kv_uint(w, "frames", si->tx_lldp_frames);
    if (si->tx_lldp_age_s != UINT32_MAX) jw_kv_uint(w, "age_s", si->tx_lldp_age_s);
    jw_obj_end(w);
    jw_key(w, "cdp"); jw_obj_begin(w);
    jw_kv_bool(w, "enabled", si->tx_cdp_on);
    jw_kv_bool(w, "peer", si->tx_cdp_peer);
    jw_kv_uint(w, "frames", si->tx_cdp_frames);
    if (si->tx_cdp_age_s != UINT32_MAX) jw_kv_uint(w, "age_s", si->tx_cdp_age_s);
    jw_obj_end(w);
    jw_obj_end(w);
}
}

void status_json_build(jw_t *w, const status_info_t *si, uint32_t now_ms)
{
    jw_obj_begin(w);
    jw_kv_str(w, "type", "status");
    jw_kv_str(w, "fw", si->fw);
    if (si->board) jw_kv_str(w, "board", si->board);
    if (si->has_probe) jw_kv_uint(w, "probe", si->probe);
    jw_kv_uint(w, "seq", si->seq);
    jw_kv_uint(w, "uptime_s", si->uptime_s);
    jw_kv_uint(w, "wd_resets", si->wd_resets);
    if (si->last_fault_pc) {
        jw_key(w, "last_fault"); jw_obj_begin(w);
        jw_kv_uint(w, "pc", si->last_fault_pc);
        jw_kv_uint(w, "lr", si->last_fault_lr);
        jw_obj_end(w);
    }

    write_eth(w, si);

    jw_key(w, "usb"); jw_obj_begin(w);
    jw_kv_bool(w, "connected", si->usb_connected);
    jw_obj_end(w);

    if (si->port) write_port(w, si);

    if (si->stp) write_stp(w, si->stp, si->link_up_for_s, now_ms);
    if (si->tool) write_tool(w, si->tool, now_ms);
    if (si->dot1x) write_dot1x(w, si->dot1x, now_ms);
    if (si->lacp) write_lacp(w, si->lacp, now_ms);
    if (si->igmp) write_igmp(w, si->igmp, now_ms);
    if (si->ptp1 && si->ptp2) write_ptp(w, si->ptp1, si->ptp2, now_ms);
    if (si->blink) write_blink(w, si->blink, si->blink_remaining_s);
    if (si->peers) write_peers(w, si->peers, si->self_mac, now_ms);
    if (si->path) write_path(w, si->path, now_ms);

    write_tx(w, si);

    /* Neighbors last, and only as many as fit: a message that overflows is
     * dropped whole, so leave out the neighbors that do not fit instead and
     * say how many. 48 bytes stays free for the closing brackets. */
    jw_key(w, "neighbors"); jw_arr_begin(w);
    int count = neighbor_table_count(), omitted = 0;
    for (int i = 0; i < count; i++) {
        const neighbor_entry_t *e = neighbor_table_get(i);
        if (!e) continue;
        jw_t before = *w;
        if (!omitted) write_neighbor(w, e, now_ms);
        if (omitted || w->pos + 48 > w->cap) {
            *w = before;
            if (w->pos < w->cap) w->buf[w->pos] = '\0';
            omitted++;
        }
    }
    jw_arr_end(w);
    if (omitted) jw_kv_uint(w, "neighbors_omitted", (uint32_t)omitted);

    jw_obj_end(w);
}
