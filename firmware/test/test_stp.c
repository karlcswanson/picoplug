/*
 * Host-side tests for stp.c: hand-built BPDUs (STP config, RSTP, MSTP
 * CIST part, TCN, PVST+ tagged and untagged), malformed input, and the
 * tracker's counters.
 */
#include "stp.h"

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

static const uint8_t SW[6] = {0x64, 0x9D, 0x99, 0x32, 0x7E, 0x90};
static const uint8_t ROOT[6] = {0x00, 0x1E, 0x14, 0xAB, 0xCD, 0x80};

/* body: flags root(8) cost(4) bridge(8) port(2) age max hello fwd (2 each) */
static size_t body(uint8_t *b, uint8_t flags, uint16_t rprio, const uint8_t *rmac, uint32_t cost,
                   uint16_t bprio, const uint8_t *bmac, uint16_t port)
{
    size_t n = 0;
    b[n++] = flags;
    b[n++] = (uint8_t)(rprio >> 8); b[n++] = (uint8_t)rprio; memcpy(b + n, rmac, 6); n += 6;
    b[n++] = (uint8_t)(cost >> 24); b[n++] = (uint8_t)(cost >> 16); b[n++] = (uint8_t)(cost >> 8); b[n++] = (uint8_t)cost;
    b[n++] = (uint8_t)(bprio >> 8); b[n++] = (uint8_t)bprio; memcpy(b + n, bmac, 6); n += 6;
    b[n++] = (uint8_t)(port >> 8); b[n++] = (uint8_t)port;
    b[n++] = 0; b[n++] = 0;            /* message age 0 */
    b[n++] = 20; b[n++] = 0;           /* max age 20 s */
    b[n++] = 2; b[n++] = 0;            /* hello 2 s */
    b[n++] = 15; b[n++] = 0;           /* forward delay 15 s */
    return n;
}

/* Build an 802.3/LLC BPDU frame. version 0 = config, 2 = RST (adds v1 len), 3 = MSTP (adds v3 len + 64 bytes) */
static size_t frame_llc(uint8_t *f, uint8_t version, uint8_t type, const uint8_t *bd, size_t bl, bool pad)
{
    size_t n = 0;
    static const uint8_t dst[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};
    memcpy(f, dst, 6); n += 6; memcpy(f + n, SW, 6); n += 6;
    size_t lenpos = n; n += 2;
    f[n++] = 0x42; f[n++] = 0x42; f[n++] = 0x03;
    f[n++] = 0; f[n++] = 0; f[n++] = version; f[n++] = type;
    if (type != STP_TYPE_TCN) { memcpy(f + n, bd, bl); n += bl; }
    if (type == STP_TYPE_RST) f[n++] = 0;
    if (version == 3) {
        /* v3 length 80: CIST section (64) + one MSTI record (16) */
        f[n++] = 0; f[n++] = 80;
        uint8_t *m = f + n; memset(m, 0, 80);
        memcpy(m + 1, "LAB-REGION", 10); m[33] = 0; m[34] = 7;          /* name, revision 7 */
        memset(m + 35, 0xAB, 16);                                     /* digest */
        m[51] = 0; m[52] = 0; m[53] = 0x4E; m[54] = 0x20;              /* internal cost 20000 */
        m[55] = 0x80; m[56] = 0x00; memcpy(m + 57, ROOT, 6); m[62] = 0x11;   /* CIST bridge id: sender */
        m[63] = 19;                                                   /* remaining hops */
        n += 80;
    }
    size_t llen = n - lenpos - 2;
    f[lenpos] = (uint8_t)(llen >> 8); f[lenpos + 1] = (uint8_t)llen;
    if (pad) while (n < 60) f[n++] = 0;
    return n;
}

static int test_config(void)
{
    uint8_t bd[64], f[128];
    size_t bl = body(bd, 0x00, 0x8000, ROOT, 19, 0x8000, SW, 0x8008);
    size_t n = frame_llc(f, 0, STP_TYPE_CONFIG, bd, bl, true);
    CHECK(stp_is_bpdu(f, n));
    stp_bpdu_t b;
    CHECK(stp_parse(f, n, &b));
    CHECK_EQ(b.version, STP_VERSION_STP); CHECK_EQ(b.type, STP_TYPE_CONFIG);
    CHECK(!b.pvst); CHECK(!b.tagged); CHECK_EQ(b.vlan, 0);
    CHECK(memcmp(b.src_mac, SW, 6) == 0);
    CHECK_EQ(b.root_priority, 0x8000); CHECK(memcmp(b.root_mac, ROOT, 6) == 0);
    CHECK_EQ(b.root_cost, 19);
    CHECK_EQ(b.bridge_priority, 0x8000); CHECK(memcmp(b.bridge_mac, SW, 6) == 0);
    CHECK_EQ(b.port_id, 0x8008);
    CHECK_EQ(b.max_age_256, 20 * 256); CHECK_EQ(b.hello_256, 2 * 256); CHECK_EQ(b.fwd_delay_256, 15 * 256);
    CHECK_STR(stp_version_name(b.version), "stp");
    CHECK_STR(stp_type_name(b.type), "config");
    CHECK_STR(stp_role_name(b.flags), "unknown");       /* 802.1D has no role bits */
    CHECK_STR(stp_state_name(b.flags), "discarding");
    return 0;
}

static int test_rstp(void)
{
    uint8_t bd[64], f[128];
    /* designated (role 3 -> bits 2-3 = 0b11 = 0x0C), learning+forwarding, agreement */
    size_t bl = body(bd, 0x0C | 0x10 | 0x20 | 0x40, 0x8001, SW, 0, 0x8001, SW, 0x8008);
    size_t n = frame_llc(f, 2, STP_TYPE_RST, bd, bl, true);
    stp_bpdu_t b;
    CHECK(stp_parse(f, n, &b));
    CHECK_EQ(b.version, STP_VERSION_RSTP); CHECK_EQ(b.type, STP_TYPE_RST);
    CHECK_STR(stp_role_name(b.flags), "designated");
    CHECK_STR(stp_state_name(b.flags), "forwarding");
    CHECK(memcmp(b.root_mac, b.bridge_mac, 6) == 0);
    CHECK_EQ(b.root_priority, 0x8001);
    /* MSTP: same CIST fields, extra records ignored */
    n = frame_llc(f, 3, STP_TYPE_RST, bd, bl, false);
    CHECK(stp_parse(f, n, &b));
    CHECK_EQ(b.version, STP_VERSION_MSTP);
    CHECK_STR(stp_version_name(b.version), "mstp");
    CHECK_EQ(b.port_id, 0x8008);
    CHECK(b.mst); CHECK_STR(b.mst_region, "LAB-REGION"); CHECK_EQ(b.mst_revision, 7);
    CHECK_EQ(b.cist_internal_cost, 20000); CHECK_EQ(b.cist_hops, 19); CHECK_EQ(b.msti_count, 1);
    CHECK_EQ(b.regional_root_priority, 0x8001); CHECK(memcmp(b.regional_root_mac, SW, 6) == 0);   /* body bridge id */
    CHECK_EQ(b.bridge_priority, 0x8000); CHECK_EQ(b.bridge_mac[5], 0x11);                       /* sender from the CIST section */
    /* RSTP frame has no MST section */
    n = frame_llc(f, 2, STP_TYPE_RST, bd, bl, true); CHECK(stp_parse(f, n, &b)); CHECK(!b.mst);
    return 0;
}

static int test_tcn(void)
{
    uint8_t f[128];
    size_t n = frame_llc(f, 0, STP_TYPE_TCN, NULL, 0, true);
    stp_bpdu_t b;
    CHECK(stp_parse(f, n, &b));
    CHECK_EQ(b.type, STP_TYPE_TCN);
    CHECK_STR(stp_type_name(b.type), "tcn");
    return 0;
}

static size_t frame_pvst(uint8_t *f, bool tagged, uint16_t vid, const uint8_t *bd, size_t bl, bool trailer)
{
    size_t n = 0;
    static const uint8_t dst[6] = {0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCD};
    static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x0C, 0x01, 0x0B};
    memcpy(f, dst, 6); n += 6; memcpy(f + n, SW, 6); n += 6;
    if (tagged) { f[n++] = 0x81; f[n++] = 0x00; f[n++] = (uint8_t)(vid >> 8); f[n++] = (uint8_t)vid; }
    size_t lenpos = n; n += 2;
    memcpy(f + n, snap, 8); n += 8;
    f[n++] = 0; f[n++] = 0; f[n++] = 2; f[n++] = STP_TYPE_RST;
    memcpy(f + n, bd, bl); n += bl;
    f[n++] = 0;                                   /* v1 len */
    if (trailer) { f[n++] = 0; f[n++] = 0; f[n++] = 0; f[n++] = 2; f[n++] = (uint8_t)(vid >> 8); f[n++] = (uint8_t)vid; }
    size_t llen = n - lenpos - 2;
    f[lenpos] = (uint8_t)(llen >> 8); f[lenpos + 1] = (uint8_t)llen;
    while (n < 64) f[n++] = 0;
    return n;
}

static int test_pvst(void)
{
    uint8_t bd[64], f[160];
    size_t bl = body(bd, 0x0C | 0x20, 0x8000 | 30, ROOT, 4, 0x8000 | 30, SW, 0x8003);
    size_t n = frame_pvst(f, true, 30, bd, bl, true);
    stp_bpdu_t b;
    CHECK(stp_is_bpdu(f, n));
    CHECK(stp_parse(f, n, &b));
    CHECK(b.pvst); CHECK(b.tagged); CHECK_EQ(b.vlan, 30);
    CHECK_EQ(b.root_priority, 0x8000 + 30);
    CHECK_EQ(b.root_cost, 4);
    /* untagged (native VLAN) with trailer only */
    n = frame_pvst(f, false, 1, bd, bl, true);
    CHECK(stp_parse(f, n, &b));
    CHECK(b.pvst); CHECK(!b.tagged); CHECK_EQ(b.vlan, 1);
    /* no trailer, no tag: vlan unknown */
    n = frame_pvst(f, false, 1, bd, bl, false);
    CHECK(stp_parse(f, n, &b));
    CHECK_EQ(b.vlan, 0);
    return 0;
}

static int test_errors(void)
{
    uint8_t bd[64], f[128];
    size_t bl = body(bd, 0x3C, 0x8000, ROOT, 19, 0x8000, SW, 0x8008);
    size_t n = frame_llc(f, 2, STP_TYPE_RST, bd, bl, false);
    stp_bpdu_t b;
    CHECK(!stp_parse(NULL, n, &b)); CHECK(!stp_parse(f, n, NULL));
    CHECK(!stp_is_bpdu(f, 10));
    /* wrong destination */
    f[5] = 0x0E; CHECK(!stp_is_bpdu(f, n)); f[5] = 0x00;
    /* wrong LLC */
    f[14] = 0x43; CHECK(!stp_is_bpdu(f, n)); f[14] = 0x42;
    /* EtherType instead of length */
    f[12] = 0x88; f[13] = 0xCC; CHECK(!stp_is_bpdu(f, n)); f[12] = 0x00; f[13] = (uint8_t)(n - 14);
    /* protocol id not zero */
    f[17] = 1; CHECK(stp_is_bpdu(f, n)); CHECK(!stp_parse(f, n, &b)); f[17] = 0;
    /* unknown type */
    f[20] = 0x05; CHECK(!stp_parse(f, n, &b)); f[20] = STP_TYPE_RST;
    /* length field claims more than the frame has */
    f[13] = (uint8_t)(n - 14 + 10); CHECK(!stp_is_bpdu(f, n)); f[13] = (uint8_t)(n - 14);
    /* truncated body: every shorter length field must be rejected, never read past */
    for (size_t l = 0; l < n - 14; l++) {
        f[13] = (uint8_t)l;
        if (stp_is_bpdu(f, 14 + l)) {
            stp_bpdu_t t;
            bool ok = stp_parse(f, 14 + l, &t);
            CHECK(!ok || l >= 3 + 4 + 31);
        }
    }
    f[13] = (uint8_t)(n - 14);
    /* random junk never crashes */
    srand(7);
    for (int i = 0; i < 20000; i++) {
        uint8_t g[128]; size_t gl = (size_t)(rand() % 128);
        for (size_t k = 0; k < gl; k++) g[k] = (uint8_t)rand();
        if (i & 1) memcpy(g, f, gl < 24 ? gl : 24);        /* keep the header valid half the time */
        stp_bpdu_t t; (void)stp_parse(g, gl, &t);
    }
    return 0;
}

static int test_tracker(void)
{
    uint8_t bd[64], f[128];
    stp_state_t s; stp_state_reset(&s);
    CHECK(!s.seen);
    size_t bl = body(bd, 0x0C | 0x20, 0x8000, ROOT, 19, 0x8000, SW, 0x8008);
    size_t n = frame_llc(f, 2, STP_TYPE_RST, bd, bl, true);
    stp_bpdu_t b;
    CHECK(stp_parse(f, n, &b));
    stp_state_update(&s, &b, 1000);
    CHECK(s.seen); CHECK_EQ(s.count, 1); CHECK_EQ(s.first_ms, 1000); CHECK_EQ(s.generation, 1);
    stp_state_update(&s, &b, 3000);
    CHECK_EQ(s.count, 2); CHECK_EQ(s.interval_ms, 2000); CHECK_EQ(s.root_changes, 0); CHECK_EQ(s.generation, 1);
    /* topology change flag */
    bd[0] |= STP_FLAG_TC; n = frame_llc(f, 2, STP_TYPE_RST, bd, bl, true);
    CHECK(stp_parse(f, n, &b)); stp_state_update(&s, &b, 5000);
    CHECK_EQ(s.tc_count, 1); CHECK_EQ(s.generation, 2);
    /* new root */
    bl = body(bd, 0x0C | 0x20, 0x1000, SW, 0, 0x1000, SW, 0x8008);
    n = frame_llc(f, 2, STP_TYPE_RST, bd, bl, true);
    CHECK(stp_parse(f, n, &b)); stp_state_update(&s, &b, 7000);
    CHECK_EQ(s.root_changes, 1); CHECK_EQ(s.last.root_priority, 0x1000);
    /* a TCN counts but keeps the last config BPDU's fields */
    n = frame_llc(f, 0, STP_TYPE_TCN, NULL, 0, true);
    CHECK(stp_parse(f, n, &b)); stp_state_update(&s, &b, 8000);
    CHECK_EQ(s.count, 5); CHECK_EQ(s.tc_count, 2);
    CHECK_EQ(s.last.root_priority, 0x1000); CHECK_EQ(s.last.type, STP_TYPE_RST);
    return 0;
}

int main(int argc, char **argv)
{
    static const struct { const char *name; int (*fn)(void); } TESTS[] = {
        {"config", test_config}, {"rstp", test_rstp}, {"tcn", test_tcn}, {"pvst", test_pvst},
        {"errors", test_errors}, {"tracker", test_tracker},
    };
    if (argc < 2) { fprintf(stderr, "usage: test_stp <name>\n"); return 2; }
    for (size_t i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++)
        if (strcmp(argv[1], TESTS[i].name) == 0) { int rc = TESTS[i].fn(); fprintf(stderr, "stp %s: %s\n", TESTS[i].name, rc ? "FAIL" : "ok"); return rc; }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
