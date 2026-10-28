/*
 * Host-side tests for dhcp_info.c: a hand-built ACK with the options a
 * corporate server sends, OFFER/ACK bookkeeping, relay detection, two
 * servers, option 119 decoding, bounds, random input.
 */
#include "dhcp_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)
#define CHECK_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", \
        __FILE__, __LINE__, #a, _a, #b, _b); return 1; } \
} while (0)
#define CHECK_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", \
        __FILE__, __LINE__, #a, (a), (b)); return 1; } \
} while (0)

static const uint8_t US[6] = {0x02, 0x50, 0x4C, 0x38, 0x00, 0x19};
static const uint8_t SRV[6] = {0x94, 0x83, 0xC4, 0x58, 0xAD, 0x95};

typedef struct { uint8_t *f; size_t n; } wr_t;
static void put(wr_t *w, const void *p, size_t n) { memcpy(w->f + w->n, p, n); w->n += n; }
static void put8(wr_t *w, uint8_t v) { w->f[w->n++] = v; }
static void opt(wr_t *w, uint8_t code, const void *v, size_t n) { put8(w, code); put8(w, (uint8_t)n); put(w, v, n); }
static void opt_ip(wr_t *w, uint8_t code, const uint8_t *ips, size_t count) { opt(w, code, ips, count * 4); }
static void opt32(wr_t *w, uint8_t code, uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; opt(w, code, b, 4); }

/* Ethernet + IPv4 + UDP + BOOTP header; returns the writer positioned at the options. */
static wr_t start_reply(uint8_t *buf, bool to_client, const uint8_t *chaddr, const uint8_t *giaddr, const char *sname, const char *file)
{
    wr_t w = { buf, 0 };
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    put(&w, to_client ? chaddr : bcast, 6); put(&w, to_client ? SRV : chaddr, 6);
    put8(&w, 0x08); put8(&w, 0x00);
    /* IPv4 header, total length patched later */
    uint8_t ip[20] = {0x45, 0, 0, 0, 0, 0, 0, 0, 64, 17, 0, 0, 192, 168, 109, 1, 192, 168, 109, 101};
    if (!to_client) { memset(ip + 12, 0, 4); memset(ip + 16, 255, 4); }
    put(&w, ip, 20);
    uint8_t udp[8] = {0, (uint8_t)(to_client ? 67 : 68), 0, (uint8_t)(to_client ? 68 : 67), 0, 0, 0, 0};
    put(&w, udp, 8);
    uint8_t b[240]; memset(b, 0, sizeof b);
    b[0] = to_client ? 2 : 1; b[1] = 1; b[2] = 6;
    b[16] = 192; b[17] = 168; b[18] = 109; b[19] = 101;        /* yiaddr */
    b[20] = 192; b[21] = 168; b[22] = 109; b[23] = 1;          /* siaddr */
    if (giaddr) memcpy(b + 24, giaddr, 4);
    memcpy(b + 28, chaddr, 6);
    if (sname) strncpy((char *)b + 44, sname, 63);
    if (file) strncpy((char *)b + 108, file, 127);
    b[236] = 0x63; b[237] = 0x82; b[238] = 0x53; b[239] = 0x63;
    put(&w, b, sizeof b);
    return w;
}
static size_t finish(wr_t *w)
{
    put8(w, 255);
    size_t iplen = w->n - 14, udplen = w->n - 34;
    w->f[16] = (uint8_t)(iplen >> 8); w->f[17] = (uint8_t)iplen;
    w->f[38] = (uint8_t)(udplen >> 8); w->f[39] = (uint8_t)udplen;
    while (w->n < 60) w->f[w->n++] = 0;
    return w->n;
}

static int test_ack(void)
{
    static uint8_t f[1024];
    wr_t w = start_reply(f, true, US, NULL, NULL, "phone-config.cnf");
    uint8_t mt = 5; opt(&w, 53, &mt, 1);
    uint8_t sid[4] = {192, 168, 109, 1}; opt(&w, 54, sid, 4);
    opt32(&w, 51, 43200); opt32(&w, 58, 21600); opt32(&w, 59, 37800);
    uint8_t mask[4] = {255, 255, 255, 0}; opt(&w, 1, mask, 4);
    uint8_t rt[4] = {192, 168, 109, 1}; opt_ip(&w, 3, rt, 1);
    uint8_t dns[8] = {192, 168, 109, 1, 1, 1, 1, 1}; opt_ip(&w, 6, dns, 2);
    opt(&w, 15, "corp.example.com", 16);
    /* 119: "corp.example.com" then "lab" + pointer to "example.com" (offset 5) */
    uint8_t srch[] = {4, 'c','o','r','p', 7, 'e','x','a','m','p','l','e', 3, 'c','o','m', 0, 3, 'l','a','b', 0xC0, 5};
    opt(&w, 119, srch, sizeof srch);
    uint8_t ntp[4] = {192, 168, 109, 5}; opt_ip(&w, 42, ntp, 1);
    uint8_t wins[4] = {192, 168, 109, 7}; opt_ip(&w, 44, wins, 1);
    opt(&w, 66, "tftp.corp.example.com", 21);
    uint8_t t150[8] = {10, 0, 0, 10, 10, 0, 0, 11}; opt_ip(&w, 150, t150, 2);
    uint8_t bc[4] = {192, 168, 109, 255}; opt(&w, 28, bc, 4);
    opt(&w, 252, "http://wpad.corp.example.com/wpad.dat", 37);
    uint8_t v43[] = {0x01, 0x04, 0xDE, 0xAD, 0xBE, 0xEF}; opt(&w, 43, v43, sizeof v43);
    size_t n = finish(&w);

    dhcp_reply_t r;
    CHECK(dhcp_info_parse_reply(f, n, US, &r));
    CHECK(r.valid); CHECK_EQ(r.msg_type, 5);
    CHECK(memcmp(r.server_mac, SRV, 6) == 0);
    CHECK(memcmp(r.server_id, sid, 4) == 0);
    CHECK(!r.relayed);
    CHECK_EQ(r.lease_s, 43200); CHECK_EQ(r.t1_s, 21600); CHECK_EQ(r.t2_s, 37800);
    CHECK_EQ(r.n_routers, 1); CHECK_EQ(r.n_dns, 2); CHECK_EQ(r.dns[1][0], 1);
    CHECK_STR(r.domain, "corp.example.com");
    CHECK_STR(r.search, "corp.example.com lab.example.com");
    CHECK_EQ(r.n_ntp, 1); CHECK_EQ(r.ntp[0][3], 5);
    CHECK_EQ(r.n_wins, 1);
    CHECK_STR(r.tftp_name, "tftp.corp.example.com");
    CHECK_EQ(r.n_tftp_ips, 2); CHECK_EQ(r.tftp_ips[1][3], 11);
    CHECK(r.has_broadcast); CHECK_EQ(r.broadcast[3], 255);
    CHECK_STR(r.wpad, "http://wpad.corp.example.com/wpad.dat");
    CHECK_EQ(r.vendor_len, 6); CHECK_EQ(r.vendor_total, 6); CHECK_EQ(r.vendor[2], 0xDE);
    CHECK_STR(r.bootfile, "phone-config.cnf");          /* from the BOOTP file field */
    CHECK_EQ(r.n_opts, 17);
    CHECK_EQ(r.opts[0], 53); CHECK_EQ(r.opts[16], 43);
    CHECK(!r.truncated);
    CHECK(!r.raw_truncated); CHECK_EQ(r.raw[0], 53); CHECK_EQ(r.raw[1], 1); CHECK_EQ(r.raw[2], 5);
    CHECK_EQ(r.raw[3], 54); CHECK_EQ(r.raw_len, n - 14 - 20 - 8 - 240 - 1);   /* every option, no end byte */
    /* not ours */
    uint8_t other[6] = {1, 2, 3, 4, 5, 6};
    CHECK(!dhcp_info_parse_reply(f, n, other, &r));
    /* any chaddr when mac is NULL */
    CHECK(dhcp_info_parse_reply(f, n, NULL, &r));
    return 0;
}

static int test_flow(void)
{
    static uint8_t f[600];
    dhcp_info_init(US);
    const dhcp_info_state_t *s = dhcp_info_get();
    CHECK(!s->ack.valid);
    /* DISCOVER (tx) */
    wr_t w = start_reply(f, false, US, NULL, NULL, NULL);
    uint8_t mt = 1; opt(&w, 53, &mt, 1);
    size_t n = finish(&w);
    dhcp_info_observe(f, n, true, 1000);
    CHECK_EQ(s->discovers, 1); CHECK_EQ(s->discover_ms, 1000);
    /* OFFER from server A, relayed */
    uint8_t gi[4] = {10, 0, 0, 1};
    w = start_reply(f, true, US, gi, NULL, NULL);
    mt = 2; opt(&w, 53, &mt, 1);
    uint8_t sa[4] = {192, 168, 109, 1}; opt(&w, 54, sa, 4);
    n = finish(&w);
    dhcp_info_observe(f, n, false, 1200);
    CHECK_EQ(s->offers, 1); CHECK_EQ(s->n_servers, 1);
    CHECK(s->ack.valid); CHECK_EQ(s->ack.msg_type, 2); CHECK(s->ack.relayed);
    /* OFFER from server B (rogue) */
    w = start_reply(f, true, US, NULL, NULL, NULL);
    mt = 2; opt(&w, 53, &mt, 1);
    uint8_t sb[4] = {192, 168, 109, 200}; opt(&w, 54, sb, 4);
    n = finish(&w);
    dhcp_info_observe(f, n, false, 1300);
    CHECK_EQ(s->offers, 2); CHECK_EQ(s->n_servers, 2);
    CHECK(memcmp(s->ack.server_id, sa, 4) == 0);       /* first offer stays on display */
    /* ACK from A */
    w = start_reply(f, true, US, gi, NULL, NULL);
    mt = 5; opt(&w, 53, &mt, 1); opt(&w, 54, sa, 4); opt32(&w, 51, 3600);
    n = finish(&w);
    dhcp_info_observe(f, n, false, 2500);
    CHECK_EQ(s->acks, 1); CHECK_EQ(s->ack.msg_type, 5); CHECK_EQ(s->ack.lease_s, 3600);
    CHECK_EQ(s->acquire_ms, 1500); CHECK_EQ(s->discover_ms, 0);
    /* a request-side frame does not count as a reply; a NAK counts */
    w = start_reply(f, true, US, NULL, NULL, NULL);
    mt = 6; opt(&w, 53, &mt, 1); opt(&w, 54, sa, 4);
    n = finish(&w);
    dhcp_info_observe(f, n, false, 3000);
    CHECK_EQ(s->naks, 1); CHECK_EQ(s->ack.msg_type, 5);   /* NAK does not replace the ACK on display */
    uint32_t g = s->generation;
    dhcp_info_reset();
    CHECK(!s->ack.valid); CHECK_EQ(s->offers, 0); CHECK_EQ(s->generation, g + 1);
    return 0;
}

static int test_bounds(void)
{
    static uint8_t f[1024];
    /* long strings are cut, not overflowed */
    wr_t w = start_reply(f, true, US, NULL, NULL, NULL);
    uint8_t mt = 5; opt(&w, 53, &mt, 1);
    char big[200]; memset(big, 'x', sizeof big);
    opt(&w, 15, big, 120); opt(&w, 66, big, 100); opt(&w, 252, big, 200);
    uint8_t ips[40]; memset(ips, 9, sizeof ips);
    opt(&w, 6, ips, 40); opt(&w, 42, ips, 40);
    uint8_t v[60]; memset(v, 0xAB, sizeof v); opt(&w, 43, v, 60);
    size_t n = finish(&w);
    dhcp_reply_t r;
    CHECK(dhcp_info_parse_reply(f, n, US, &r));
    CHECK(r.truncated);
    CHECK_EQ(strlen(r.domain), DHCP_INFO_STR);
    CHECK_EQ(r.n_dns, DHCP_INFO_MAX_IPS);
    CHECK_EQ(r.vendor_len, DHCP_INFO_VENDOR); CHECK_EQ(r.vendor_total, 60);
    CHECK(r.raw_truncated); CHECK(r.raw_len <= DHCP_INFO_RAW); CHECK_EQ(r.raw[0], 53);
    /* option length running past the packet: rejected cleanly or truncated, never read past */
    f[n - 1] = 0;                               /* lose the end marker */
    (void)dhcp_info_parse_reply(f, n, US, &r);
    /* no message type: not a reply */
    w = start_reply(f, true, US, NULL, NULL, NULL);
    n = finish(&w);
    CHECK(!dhcp_info_parse_reply(f, n, US, &r));
    /* too short / not UDP / wrong port */
    CHECK(!dhcp_info_parse_reply(f, 100, US, &r));
    f[23] = 6; CHECK(!dhcp_info_parse_reply(f, n, US, &r)); f[23] = 17;
    f[37] = 80; CHECK(!dhcp_info_parse_reply(f, n, US, &r));
    /* random input never crashes */
    srand(3);
    for (int i = 0; i < 20000; i++) {
        static uint8_t g[400]; size_t gl = (size_t)(rand() % 400);
        for (size_t k = 0; k < gl; k++) g[k] = (uint8_t)rand();
        if (i & 1) { size_t keep = gl < 282 ? gl : 282; memcpy(g, f, keep); }
        (void)dhcp_info_parse_reply(g, gl, US, &r);
        dhcp_info_observe(g, gl, i & 2, 100);
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const struct { const char *name; int (*fn)(void); } TESTS[] = {
        {"ack", test_ack}, {"flow", test_flow}, {"bounds", test_bounds},
    };
    if (argc < 2) { fprintf(stderr, "usage: test_dhcp_info <name>\n"); return 2; }
    for (size_t i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++)
        if (strcmp(argv[1], TESTS[i].name) == 0) { int rc = TESTS[i].fn(); fprintf(stderr, "dhcp_info %s: %s\n", TESTS[i].name, rc ? "FAIL" : "ok"); return rc; }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
