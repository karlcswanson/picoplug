/*
 * Host-side tests for cdp.c: fixtures, error handling, fuzz with guard pages.
 */
#include "cdp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_FRAME 1600
#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, _a, #b, _b); return 1; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) != 0) { \
    fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, (a), (b)); return 1; } } while (0)

static uint8_t *guard_alloc(size_t len)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t span = (len + page - 1) / page * page;
    if (span == 0) span = page;
    uint8_t *base = mmap(NULL, span + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) abort();
    mprotect(base + span, page, PROT_NONE);
    return base + span - len;
}
static void guard_free(uint8_t *p, size_t len)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t span = (len + page - 1) / page * page;
    if (span == 0) span = page;
    munmap(p + len - span, span + page);
}

static size_t load_hex(const char *name, uint8_t *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.hex", FIXTURE_DIR, name);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = 0; char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#'); if (hash) *hash = '\0';
        for (char *p = line; *p;) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (!*p) break;
            unsigned v; if (sscanf(p, "%2x", &v) != 1) break;
            if (n < cap) buf[n] = (uint8_t)v;
            n++; p += 2;
        }
    }
    fclose(f);
    return n <= cap ? n : 0;
}

static const char *const FIXTURES[] = {"cdp_switch", "cdp_phone", "cdp_router", "cdp_v1"};
#define N_FIXTURES (sizeof FIXTURES / sizeof FIXTURES[0])

static int addr_is(const lldp_mgmt_addr_t *m, const char *want)
{
    char s[LLDP_ADDR_STR_LEN];
    lldp_format_addr(m->afi, m->addr, m->len, s, sizeof s);
    CHECK_STR(s, want);
    return 0;
}

static int test_switch(void)
{
    uint8_t f[MAX_FRAME]; size_t len = load_hex("cdp_switch", f, sizeof f); CHECK(len > 0);
    lldp_neighbor_t n; char s[LLDP_ID_STR_LEN];
    CHECK(cdp_is_cdp_frame(f, len));
    CHECK_EQ(cdp_parse(f, len, &n), CDP_OK);
    CHECK_EQ(n.proto, NEIGHBOR_PROTO_CDP);
    CHECK_EQ(n.cdp_version, 2);
    CHECK_EQ(n.ttl, 180);
    CHECK(n.checksum_ok);                              /* Cisco odd-length quirk */
    lldp_format_chassis_id(&n.chassis_id, s, sizeof s); CHECK_STR(s, "local:sw-access-1.example.com");
    lldp_format_port_id(&n.port_id, s, sizeof s);       CHECK_STR(s, "ifname:GigabitEthernet1/0/12");
    CHECK_STR(n.sys_name, "sw-access-1");               /* System Name TLV wins over Device ID */
    CHECK(strncmp(n.sys_desc, "Cisco IOS Software, C2960X", 26) == 0);
    CHECK(n.has_platform); CHECK_STR(n.platform, "cisco WS-C2960X-48FPD-L");
    CHECK(n.has_vtp_domain); CHECK_STR(n.vtp_domain, "corp");
    CHECK(n.has_caps); CHECK_EQ(n.cdp_caps, 0x28);
    CHECK_EQ(n.caps_enabled, LLDP_CAP_BRIDGE | LLDP_CAP_OTHER);
    CHECK_EQ(n.n_mgmt_addrs, 2);                        /* 10.0.0.2 deduplicated across TLV 2 and 22 */
    if (addr_is(&n.mgmt_addrs[0], "10.0.0.2")) return 1;
    if (addr_is(&n.mgmt_addrs[1], "2001:db8::2")) return 1;
    CHECK(n.has_pvid); CHECK_EQ(n.pvid, 20);
    CHECK(n.has_duplex && n.duplex_full);
    const lldp_med_policy_t *v = lldp_find_med_policy(&n, LLDP_MED_APP_VOICE);
    CHECK(v && v->vlan_id == 30 && v->tagged);
    CHECK(n.has_power_avail_mw); CHECK_EQ(n.power_avail_mw, 15400);
    CHECK(!n.has_power_req_mw);
    /* unknown: protocol hello (8), trust bitmap (18), untrusted CoS (19) */
    CHECK_EQ(n.n_unknown, 3);
    CHECK_EQ(n.unknown[0].type, 8);
    CHECK_EQ(n.unknown[0].len, 31);
    CHECK_EQ(n.n_tlvs, 16);
    CHECK(n.truncated);                                 /* long IOS version string */
    return 0;
}

static int test_phone(void)
{
    uint8_t f[MAX_FRAME]; size_t len = load_hex("cdp_phone", f, sizeof f); CHECK(len > 0);
    lldp_neighbor_t n; char s[LLDP_ID_STR_LEN];
    CHECK_EQ(cdp_parse(f, len, &n), CDP_OK);
    CHECK(n.checksum_ok);                              /* even length, plain checksum */
    lldp_format_chassis_id(&n.chassis_id, s, sizeof s); CHECK_STR(s, "local:SEP001122334455");
    lldp_format_port_id(&n.port_id, s, sizeof s);       CHECK_STR(s, "ifname:Port 1");
    CHECK_STR(n.sys_name, "SEP001122334455");           /* no System Name TLV: device ID */
    CHECK_STR(n.platform, "Cisco IP Phone 8845");
    CHECK_EQ(n.caps_enabled, LLDP_CAP_STATION | LLDP_CAP_TELEPHONE);
    CHECK_EQ(n.n_mgmt_addrs, 1);
    if (addr_is(&n.mgmt_addrs[0], "10.0.30.55")) return 1;
    CHECK(n.has_power_req_mw); CHECK_EQ(n.power_req_mw, 12000);
    CHECK(n.has_power_mw); CHECK_EQ(n.power_mw, 6300);
    CHECK(!n.has_pvid);
    CHECK_EQ(n.n_unknown, 1);                           /* VoIP VLAN query */
    CHECK_EQ(n.unknown[0].type, 15);
    CHECK(!n.truncated);
    return 0;
}

static int test_router(void)
{
    uint8_t f[MAX_FRAME]; size_t len = load_hex("cdp_router", f, sizeof f); CHECK(len > 0);
    lldp_neighbor_t n;
    CHECK_EQ(cdp_parse(f, len, &n), CDP_OK);
    CHECK_STR(n.sys_name, "rtr-edge-1");
    CHECK_EQ(n.caps_enabled, LLDP_CAP_ROUTER | LLDP_CAP_BRIDGE | LLDP_CAP_OTHER);
    CHECK_EQ(n.n_mgmt_addrs, 2);
    if (addr_is(&n.mgmt_addrs[1], "198.51.100.1")) return 1;
    CHECK(n.has_mtu); CHECK_EQ(n.mtu, 1500);
    CHECK_EQ(n.n_unknown, 1); CHECK_EQ(n.unknown[0].type, 7); CHECK_EQ(n.unknown[0].len, 5);
    return 0;
}

static int test_v1(void)
{
    uint8_t f[MAX_FRAME]; size_t len = load_hex("cdp_v1", f, sizeof f); CHECK(len > 0);
    lldp_neighbor_t n;
    CHECK_EQ(cdp_parse(f, len, &n), CDP_OK);
    CHECK_EQ(n.cdp_version, 1);
    CHECK_STR(n.sys_name, "OldSwitch");
    CHECK_STR(n.platform, "cisco WS-C2950-24");
    CHECK_EQ(n.caps_enabled, LLDP_CAP_BRIDGE);
    CHECK(!n.has_pvid && !n.has_duplex);
    CHECK_EQ(n.n_unknown, 0);
    return 0;
}

static int test_errors(void)
{
    uint8_t f[MAX_FRAME], g[MAX_FRAME + 4]; lldp_neighbor_t n;
    size_t len = load_hex("cdp_phone", f, sizeof f); CHECK(len > 0);
    CHECK_EQ(cdp_parse(NULL, len, &n), CDP_ERR_NULL);
    CHECK_EQ(cdp_parse(f, 0, &n), CDP_ERR_TOO_SHORT);
    CHECK_EQ(cdp_parse(f, 20, &n), CDP_ERR_TOO_SHORT);
    /* LLDP frame is not CDP */
    uint8_t l[MAX_FRAME]; size_t ll = load_hex("cisco", l, sizeof l); CHECK(ll > 0);
    CHECK(!cdp_is_cdp_frame(l, ll));
    CHECK_EQ(cdp_parse(l, ll, &n), CDP_ERR_NOT_CDP);
    /* wrong SNAP PID */
    memcpy(g, f, len); g[20] = 0x00; g[21] = 0x01;
    CHECK_EQ(cdp_parse(g, len, &n), CDP_ERR_NOT_CDP);
    /* bad version */
    memcpy(g, f, len); g[22] = 3;
    CHECK_EQ(cdp_parse(g, len, &n), CDP_ERR_BAD_VERSION);
    /* corrupted checksum still parses, flagged */
    memcpy(g, f, len); g[24] ^= 0x55;
    CHECK_EQ(cdp_parse(g, len, &n), CDP_OK); CHECK(!n.checksum_ok);
    /* 802.3 length beyond frame -> truncated */
    memcpy(g, f, len); g[12] = 0x05; g[13] = 0xdc;
    CHECK_EQ(cdp_parse(g, len, &n), CDP_ERR_TRUNCATED);
    /* first TLV length 0 -> truncated */
    memcpy(g, f, len); g[28] = 0; g[29] = 0;
    CHECK_EQ(cdp_parse(g, len, &n), CDP_ERR_TRUNCATED);
    /* TLV length past payload -> truncated */
    memcpy(g, f, len); g[28] = 0x7f; g[29] = 0xff;
    CHECK_EQ(cdp_parse(g, len, &n), CDP_ERR_TRUNCATED);
    /* 802.1Q tagged variant */
    memcpy(g, f, 12); g[12] = 0x81; g[13] = 0x00; g[14] = 0x00; g[15] = 0x2a; memcpy(g + 16, f + 12, len - 12);
    CHECK_EQ(cdp_parse(g, len + 4, &n), CDP_OK); CHECK(n.vlan_tagged); CHECK_EQ(n.tag_vid, 42);
    CHECK_STR(n.platform, "Cisco IP Phone 8845");
    return 0;
}

static int test_truncation(void)
{
    uint8_t f[MAX_FRAME]; lldp_neighbor_t n;
    for (size_t i = 0; i < N_FIXTURES; i++) {
        size_t len = load_hex(FIXTURES[i], f, sizeof f); CHECK(len > 0);
        for (size_t l = 0; l <= len; l++) {
            uint8_t *h = guard_alloc(l); memcpy(h, f, l);
            cdp_result_t r = cdp_parse(h, l, &n);
            guard_free(h, l);
            if (l < len) CHECK(r != CDP_OK || l >= 26);
        }
    }
    return 0;
}

static uint32_t rs = 0x1234567u;
static uint32_t rnd(void) { uint32_t x = rs; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rs = x; }

static int test_mutation(void)
{
    uint8_t f[MAX_FRAME]; lldp_neighbor_t n;
    for (size_t i = 0; i < N_FIXTURES; i++) {
        size_t len = load_hex(FIXTURES[i], f, sizeof f); CHECK(len > 0);
        uint8_t *g = guard_alloc(len);
        for (size_t b = 12; b < len; b++) {
            static const uint8_t vals[] = {0x00, 0xff, 0x80, 0x7f, 0x01, 0x04};
            for (size_t v = 0; v < sizeof vals; v++) { memcpy(g, f, len); g[b] = vals[v]; (void)cdp_parse(g, len, &n); }
            for (int v = 0; v < 4; v++) { memcpy(g, f, len); g[b] = (uint8_t)rnd(); g[b + (size_t)(rnd() % (len - b))] = (uint8_t)rnd(); (void)cdp_parse(g, len, &n); }
        }
        guard_free(g, len);
    }
    return 0;
}

static int test_random(void)
{
    static const uint8_t prefix[] = {0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                     0x01, 0x00, 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x0c, 0x20, 0x00, 0x02, 0xb4, 0x00, 0x00};
    lldp_neighbor_t n; int oks = 0;
    for (int it = 0; it < 150000; it++) {
        size_t len = (size_t)(rnd() % 600);
        uint8_t *g = guard_alloc(len);
        for (size_t b = 0; b < len; b++) g[b] = (uint8_t)rnd();
        if (it % 2 && len >= sizeof prefix) {
            memcpy(g, prefix, sizeof prefix);
            size_t payload = len - 14; g[12] = (uint8_t)(payload >> 8); g[13] = (uint8_t)payload;
            /* plausible TLV chain */
            size_t p = sizeof prefix;
            while (p + 4 < len) {
                uint16_t type = (uint16_t)(rnd() % 28); size_t tl = 4 + rnd() % 40;
                g[p] = (uint8_t)(type >> 8); g[p + 1] = (uint8_t)type; g[p + 2] = (uint8_t)(tl >> 8); g[p + 3] = (uint8_t)tl;
                p += tl;
            }
        }
        if (cdp_parse(g, len, &n) == CDP_OK) oks++;
        guard_free(g, len);
    }
    fprintf(stderr, "random: %d of 150000 parsed OK\n", oks);
    CHECK(oks > 100);
    return 0;
}

typedef struct { const char *name; int (*fn)(void); } test_t;
static const test_t TESTS[] = {
    {"switch", test_switch}, {"phone", test_phone}, {"router", test_router}, {"v1", test_v1},
    {"errors", test_errors}, {"truncation", test_truncation}, {"mutation", test_mutation}, {"random", test_random},
};

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    for (size_t i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++)
        if (strcmp(argv[1], TESTS[i].name) == 0) { int rc = TESTS[i].fn(); fprintf(stderr, "cdp %s: %s\n", TESTS[i].name, rc ? "FAIL" : "ok"); return rc; }
    return 2;
}
