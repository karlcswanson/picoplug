/*
 * test_peers.c - the mDNS-fed table of other testers (peers.c).
 */
#include "peers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)
#define CHECK_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { fprintf(stderr, "%s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b); return 1; } \
} while (0)

/* "a.b.c" -> DNS label form, returns its length (with the closing 0) */
static size_t dn(uint8_t *out, const char *s)
{
    size_t o = 0;
    while (*s) {
        const char *d = strchr(s, '.');
        size_t l = d ? (size_t)(d - s) : strlen(s);
        out[o++] = (uint8_t)l;
        memcpy(out + o, s, l);
        o += l;
        s += l + (d ? 1 : 0);
    }
    out[o++] = 0;
    return o;
}

static bool rec(peers_table_t *t, uint16_t type, const char *owner, const uint8_t *rd, size_t rdlen, uint32_t ttl, uint32_t now)
{
    uint8_t d[256];
    size_t dl = dn(d, owner);
    return peers_record(t, "picoplug-1", type, d, dl, rd, rdlen, ttl, now);
}

static bool ptr(peers_table_t *t, const char *inst, uint32_t ttl, uint32_t now)
{
    uint8_t r[256];
    char full[128];
    snprintf(full, sizeof full, "%s._picoplug._tcp.local", inst);
    return rec(t, PEERS_RR_PTR, "_picoplug._tcp.local", r, dn(r, full), ttl, now);
}

static size_t txt(uint8_t *out, const char *const *items)
{
    size_t o = 0;
    for (; *items; items++) {
        size_t l = strlen(*items);
        out[o++] = (uint8_t)l;
        memcpy(out + o, *items, l);
        o += l;
    }
    return o;
}

static int test_records(void)
{
    peers_table_t t;
    memset(&t, 0, sizeof t);
    peers_reset(&t);
    uint32_t g0 = t.generation;

    CHECK(ptr(&t, "picoplug-3", 120, 1000));
    CHECK_EQ(peers_count(&t), 1);
    CHECK(t.generation > g0);
    const peers_entry_t *e = peers_find(&t, "PICOPLUG-3");          /* names compare without case */
    CHECK(e != NULL);
    CHECK_EQ(e->probe, PEERS_PROBE_NONE);

    static const char *const items[] = {"probe=3", "ch=mac:68:99:cd:d9:13:bd", "sw=clockclub-sw01", "port=Gi1/0/7",
                                        "pvid=4000", "fw=0.4.0", "junk", "pvid2=9", NULL};
    uint8_t r[256];
    size_t rl = txt(r, items);
    CHECK(rec(&t, PEERS_RR_TXT, "picoplug-3._picoplug._tcp.local", r, rl, 4500, 1100));
    CHECK_EQ(e->probe, 3);
    CHECK(!strcmp(e->chassis, "mac:68:99:cd:d9:13:bd"));
    CHECK(!strcmp(e->sys_name, "clockclub-sw01"));
    CHECK(!strcmp(e->port, "Gi1/0/7"));
    CHECK(e->has_pvid); CHECK_EQ(e->pvid, 4000);
    CHECK(!strcmp(e->fw, "0.4.0"));
    uint32_t g = t.generation;
    CHECK(!rec(&t, PEERS_RR_TXT, "picoplug-3._picoplug._tcp.local", r, rl, 4500, 1200));   /* same: no bump */
    CHECK_EQ(t.generation, g);
    CHECK_EQ(e->last_ms, 1200);

    /* SRV carries the port */
    uint8_t srv[64] = {0, 0, 0, 0, 0x1f, 0x90};
    size_t sl = 6 + dn(srv + 6, "picoplug-3.local");
    CHECK(rec(&t, PEERS_RR_SRV, "picoplug-3._picoplug._tcp.local", srv, sl, 120, 1300));
    CHECK_EQ(e->http_port, 8080);

    /* addresses: only for a known tester; link-local wins over a global */
    static const uint8_t ll[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x00, 0x50, 0x4c, 0xff, 0xfe, 0x50, 0x34, 0xa5};
    static const uint8_t gl[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t v4[4] = {192, 168, 109, 209};
    CHECK(!rec(&t, PEERS_RR_AAAA, "macbook.local", ll, 16, 120, 1400));
    CHECK(rec(&t, PEERS_RR_AAAA, "picoplug-3.local", ll, 16, 120, 1400));
    CHECK(rec(&t, PEERS_RR_AAAA, "picoplug-3.local", gl, 16, 120, 1400) == false);
    CHECK(!memcmp(e->ip6, ll, 16));
    CHECK(!rec(&t, PEERS_RR_AAAA, "picoplug-3.local", ll, 4, 120, 1400));     /* wrong length */
    CHECK(rec(&t, PEERS_RR_A, "picoplug-3.local", v4, 4, 120, 1500));
    CHECK(e->has_ip4); CHECK(!memcmp(e->ip4, v4, 4));

    /* not ours: other services, our own name, odd names */
    CHECK(!rec(&t, PEERS_RR_TXT, "printer._ipp._tcp.local", r, rl, 4500, 1600));
    CHECK(!ptr(&t, "picoplug-1", 120, 1600));
    uint8_t other[64];
    CHECK(!rec(&t, PEERS_RR_PTR, "_picoplug._tcp.local", other, dn(other, "x._http._tcp.local"), 120, 1600));
    CHECK(!rec(&t, PEERS_RR_PTR, "_http._tcp.local", other, dn(other, "y._picoplug._tcp.local"), 120, 1600));
    CHECK_EQ(peers_count(&t), 1);

    /* goodbye */
    CHECK(ptr(&t, "picoplug-3", 0, 1700));
    CHECK_EQ(peers_count(&t), 0);
    CHECK(!ptr(&t, "picoplug-3", 0, 1700));
    return 0;
}

static int test_expire(void)
{
    peers_table_t t;
    memset(&t, 0, sizeof t);
    peers_reset(&t);
    CHECK(ptr(&t, "picoplug", 120, 1000));
    CHECK(ptr(&t, "picoplug-4", 120, 50000));
    CHECK(!peers_expire(&t, 100000, 150000));
    CHECK(peers_expire(&t, 160000, 150000));
    CHECK_EQ(peers_count(&t), 1);
    CHECK(peers_find(&t, "picoplug-4") != NULL);
    /* a full table refuses the next one rather than overwriting */
    for (int i = 0; i < PEERS_MAX + 2; i++) {
        char n[16];
        snprintf(n, sizeof n, "picoplug-x%d", i);
        ptr(&t, n, 120, 60000);
    }
    CHECK_EQ(peers_count(&t), PEERS_MAX);
    return 0;
}

static int test_bounds(void)
{
    peers_table_t t;
    memset(&t, 0, sizeof t);
    peers_reset(&t);
    uint8_t d[300];
    /* a label length running past the end, an over-long label, a 40-char instance (cut to 32) */
    uint8_t bad[] = {9, '_', 'p', 'i', 'c', 'o', 'p', 'l', 'u', 'g', 4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0};
    uint8_t tr[] = {200, 'a', 'b', 0};
    CHECK(!peers_record(&t, NULL, PEERS_RR_PTR, bad, sizeof bad, tr, sizeof tr, 120, 1));
    size_t l = dn(d, "abcdefghijabcdefghijabcdefghijabcdefghij._picoplug._tcp.local");
    CHECK(!peers_record(&t, NULL, PEERS_RR_SRV, d, l, (const uint8_t *)"", 0, 120, 1));     /* too long for a name */
    /* TXT with a length past the end */
    l = dn(d, "p._picoplug._tcp.local");
    uint8_t txt_bad[] = {7, 'p', 'r', 'o', 'b', 'e', '=', '5', 40, 'x'};
    CHECK(peers_record(&t, NULL, PEERS_RR_TXT, d, l, txt_bad, sizeof txt_bad, 120, 1));
    CHECK_EQ(peers_find(&t, "p")->probe, 5);
    char lab[8];
    CHECK(!peers_label(d, l, 9, lab, sizeof lab));
    CHECK(!peers_label(d, l, 1, lab, 4));      /* "_picoplug" does not fit in 4 */
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s records|expire|bounds\n", argv[0]); return 2; }
    if (!strcmp(argv[1], "records")) return test_records();
    if (!strcmp(argv[1], "expire")) return test_expire();
    if (!strcmp(argv[1], "bounds")) return test_bounds();
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
