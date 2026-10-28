/*
 * dhcp_info.c - see dhcp_info.h.
 */
#include "dhcp_info.h"

#include <string.h>

static uint8_t our_mac[6];
static dhcp_info_state_t st;

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool ip_zero(const uint8_t ip[4]) { return (ip[0] | ip[1] | ip[2] | ip[3]) == 0; }

/* Locate the BOOTP header of a DHCP packet; returns NULL if not DHCP. */
static const uint8_t *bootp_of(const uint8_t *f, size_t len, const uint8_t **end_out, bool *to_client)
{
    if (len < 14 + 20 + 8 + 240 || f[12] != 0x08 || f[13] != 0x00) return NULL;
    const uint8_t *ip = f + 14;
    if ((ip[0] >> 4) != 4 || ip[9] != 17) return NULL;
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4;
    if (ihl < 20 || 14 + ihl + 8 > len) return NULL;
    size_t iplen = (size_t)((ip[2] << 8) | ip[3]);
    const uint8_t *end = f + len;
    if (iplen >= ihl + 8 && 14 + iplen <= len) end = f + 14 + iplen;   /* ignore Ethernet padding */
    const uint8_t *udp = ip + ihl;
    uint16_t dport = (uint16_t)((udp[2] << 8) | udp[3]);
    if (dport != 67 && dport != 68) return NULL;
    const uint8_t *bootp = udp + 8;
    if (bootp + 240 > end) return NULL;
    if (bootp[236] != 0x63 || bootp[237] != 0x82 || bootp[238] != 0x53 || bootp[239] != 0x63) return NULL;  /* magic */
    *end_out = end;
    *to_client = dport == 68;
    return bootp;
}

static void copy_str(char *dst, size_t cap, const uint8_t *src, size_t n, bool *trunc)
{
    size_t i = 0;
    for (; i < n && i < cap && src[i]; i++) dst[i] = (char)((src[i] >= 0x20 && src[i] < 0x7F) ? src[i] : '?');
    dst[i] = '\0';
    if (i == cap && i < n && src[i]) *trunc = true;     /* more text followed */
}

static void add_ips(uint8_t (*dst)[4], uint8_t *n, uint8_t max, const uint8_t *v, size_t vlen, bool *trunc)
{
    for (size_t i = 0; i + 4 <= vlen; i += 4) {
        if (*n >= max) { *trunc = true; return; }
        memcpy(dst[(*n)++], v + i, 4);
    }
}

/* Option 119: DNS labels with RFC 1035 compression, pointers relative to
 * the option data. Names are appended space separated. */
static void decode_search(char *dst, size_t cap, const uint8_t *v, size_t vlen, bool *trunc)
{
    size_t out = strlen(dst);
    size_t p = 0;
    int guard = 0;
    bool first_label = true;
    while (p < vlen && guard++ < 64) {
        uint8_t l = v[p];
        if (l == 0) { p++; first_label = true; continue; }
        if ((l & 0xC0) == 0xC0) {
            if (p + 1 >= vlen) break;
            size_t ptr = (size_t)(((l & 0x3F) << 8) | v[p + 1]);
            p += 2;
            /* follow the pointer once, without recursion into further pointers */
            size_t q = ptr;
            int g2 = 0;
            while (q < vlen && v[q] && g2++ < 16) {
                uint8_t m = v[q];
                if ((m & 0xC0) == 0xC0) break;
                if (q + 1 + m > vlen) { *trunc = true; return; }
                if (out + m + 2 >= cap) { *trunc = true; dst[out] = '\0'; return; }
                if (!first_label) dst[out++] = '.';
                memcpy(dst + out, v + q + 1, m); out += m; first_label = false;
                q += 1 + m;
            }
            dst[out] = '\0';
            if (!first_label) { if (out + 1 < cap) dst[out++] = ' '; dst[out] = '\0'; }
            first_label = true;
            continue;
        }
        if (p + 1 + l > vlen) { *trunc = true; break; }
        if (out + l + 2 >= cap) { *trunc = true; break; }
        if (first_label && out && dst[out - 1] != ' ') dst[out++] = ' ';
        if (!first_label) dst[out++] = '.';
        memcpy(dst + out, v + p + 1, l); out += l;
        first_label = false;
        p += 1 + l;
    }
    dst[out] = '\0';
    /* trim a trailing space */
    while (out && dst[out - 1] == ' ') dst[--out] = '\0';
}

bool dhcp_info_parse_reply(const uint8_t *frame, size_t len, const uint8_t mac[6], dhcp_reply_t *out)
{
    if (!frame || !out) return false;
    const uint8_t *end; bool to_client;
    const uint8_t *b = bootp_of(frame, len, &end, &to_client);
    if (!b || !to_client || b[0] != 2) return false;           /* BOOTREPLY */
    if (mac && memcmp(b + 28, mac, 6) != 0) return false;      /* chaddr */
    memset(out, 0, sizeof *out);
    memcpy(out->server_mac, frame + 6, 6);
    memcpy(out->yiaddr, b + 16, 4);
    memcpy(out->siaddr, b + 20, 4);
    memcpy(out->giaddr, b + 24, 4);
    out->relayed = !ip_zero(out->giaddr);
    bool trunc = false;
    const uint8_t *o = b + 240;
    while (o < end && *o != 255) {
        if (*o == 0) { o++; continue; }
        if (o + 2 > end) { trunc = true; break; }
        uint8_t code = o[0], l = o[1];
        const uint8_t *v = o + 2;
        if (v + l > end) { trunc = true; break; }
        if (out->n_opts < DHCP_INFO_MAX_OPTS) out->opts[out->n_opts++] = code; else trunc = true;
        if (!out->raw_truncated && out->raw_len + 2u + l <= DHCP_INFO_RAW) {
            out->raw[out->raw_len++] = code; out->raw[out->raw_len++] = l;
            memcpy(out->raw + out->raw_len, v, l); out->raw_len = (uint16_t)(out->raw_len + l);
        } else out->raw_truncated = true;
        switch (code) {
        case 53: if (l >= 1) out->msg_type = v[0]; break;
        case 54: if (l >= 4) memcpy(out->server_id, v, 4); break;
        case 51: if (l >= 4) out->lease_s = rd32(v); break;
        case 58: if (l >= 4) out->t1_s = rd32(v); break;
        case 59: if (l >= 4) out->t2_s = rd32(v); break;
        case 3:  add_ips(out->routers, &out->n_routers, DHCP_INFO_MAX_IPS, v, l, &trunc); break;
        case 6:  add_ips(out->dns, &out->n_dns, DHCP_INFO_MAX_IPS, v, l, &trunc); break;
        case 42: add_ips(out->ntp, &out->n_ntp, DHCP_INFO_MAX_IPS, v, l, &trunc); break;
        case 44: add_ips(out->wins, &out->n_wins, 2, v, l, &trunc); break;
        case 150: add_ips(out->tftp_ips, &out->n_tftp_ips, 2, v, l, &trunc); break;
        case 28: if (l >= 4) { out->has_broadcast = true; memcpy(out->broadcast, v, 4); } break;
        case 15: copy_str(out->domain, DHCP_INFO_STR, v, l, &trunc); break;
        case 119: decode_search(out->search, sizeof out->search, v, l, &trunc); break;
        case 66: copy_str(out->tftp_name, DHCP_INFO_STR, v, l, &trunc); break;
        case 67: copy_str(out->bootfile, DHCP_INFO_STR, v, l, &trunc); break;
        case 252: copy_str(out->wpad, sizeof out->wpad - 1, v, l, &trunc); break;
        case 43: {
            out->vendor_total = (uint16_t)(out->vendor_total + l);
            size_t room = DHCP_INFO_VENDOR - out->vendor_len;
            size_t n = l < room ? l : room;
            memcpy(out->vendor + out->vendor_len, v, n);
            out->vendor_len = (uint8_t)(out->vendor_len + n);
            if (n < l) trunc = true;
            break;
        }
        default: break;
        }
        o = v + l;
    }
    if (out->msg_type != 2 && out->msg_type != 5 && out->msg_type != 6) return false;
    /* BOOTP sname/file fields when the options did not carry them */
    if (!out->tftp_name[0] && b[44]) copy_str(out->tftp_name, DHCP_INFO_STR, b + 44, 64, &trunc);
    if (!out->bootfile[0] && b[108]) copy_str(out->bootfile, DHCP_INFO_STR, b + 108, 128, &trunc);
    out->truncated = trunc;
    out->valid = true;
    return true;
}

void dhcp_info_init(const uint8_t mac[6]) { memcpy(our_mac, mac, 6); dhcp_info_reset(); }

void dhcp_info_reset(void)
{
    uint32_t g = st.generation + 1;
    memset(&st, 0, sizeof st);
    st.generation = g;
}

static void note_server(const uint8_t id[4])
{
    if (ip_zero(id)) return;
    for (int i = 0; i < st.n_servers; i++) if (memcmp(st.servers[i], id, 4) == 0) return;
    if (st.n_servers < 4) { memcpy(st.servers[st.n_servers++], id, 4); st.generation++; }
}

void dhcp_info_observe(const uint8_t *frame, size_t len, bool tx, uint32_t now_ms)
{
    if (tx) {
        const uint8_t *end; bool to_client;
        const uint8_t *b = bootp_of(frame, len, &end, &to_client);
        if (!b || to_client) return;
        const uint8_t *o = b + 240;
        uint8_t mt = 0;
        while (o + 2 < end && *o != 255) { if (*o == 0) { o++; continue; } if (*o == 53 && o[1] >= 1 && o + 2 < end) { mt = o[2]; break; } o += 2 + o[1]; }
        if (mt == 1) { st.discovers++; if (!st.discover_ms) st.discover_ms = now_ms ? now_ms : 1; }
        return;
    }
    static dhcp_reply_t r;      /* ~500 bytes, keep off the stack */
    if (!dhcp_info_parse_reply(frame, len, our_mac, &r)) return;
    note_server(r.server_id);
    if (r.msg_type == 2) {
        st.offers++;
        if (!st.ack.valid) st.ack = r;          /* show the first offer until the ACK lands */
    } else if (r.msg_type == 5) {
        st.acks++;
        if (st.discover_ms) { st.acquire_ms = now_ms - st.discover_ms; if (!st.acquire_ms) st.acquire_ms = 1; st.discover_ms = 0; }
        st.ack = r;
        st.generation++;
    } else {
        st.naks++;
        st.generation++;
    }
}

const dhcp_info_state_t *dhcp_info_get(void) { return &st; }
