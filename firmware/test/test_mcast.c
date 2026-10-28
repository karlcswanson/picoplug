/*
 * Host-side tests for mcast.c: hand-built IGMP queries (v1/v2/v3, router
 * alert, group-specific), PTPv2 Announce/Sync over UDP and layer 2, PTPv1
 * Sync, truncation against a guard page, and both trackers.
 */
#include "mcast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

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
static const uint8_t GM[6] = {0x00, 0x1D, 0xC1, 0x12, 0x34, 0x56};

/* Ethernet + optional tag + IPv4 (optional router alert) + payload with the given protocol */
static size_t ipv4_frame(uint8_t *f, bool tagged, bool ralert, const uint8_t *smac, const uint8_t sip[4], const uint8_t dip[4],
                         uint8_t proto, const uint8_t *payload, size_t plen)
{
    size_t n = 0;
    f[n++] = 0x01; f[n++] = 0x00; f[n++] = 0x5E; f[n++] = dip[1] & 0x7F; f[n++] = dip[2]; f[n++] = dip[3];
    memcpy(f + n, smac, 6); n += 6;
    if (tagged) { f[n++] = 0x81; f[n++] = 0x00; f[n++] = 0x00; f[n++] = 0x0A; }
    f[n++] = 0x08; f[n++] = 0x00;
    size_t ihl = ralert ? 24 : 20;
    size_t total = ihl + plen;
    f[n++] = (uint8_t)(0x40 | (ihl / 4)); f[n++] = 0xC0;
    f[n++] = (uint8_t)(total >> 8); f[n++] = (uint8_t)total;
    f[n++] = 0; f[n++] = 0; f[n++] = 0x40; f[n++] = 0;      /* id, flags/frag */
    f[n++] = 1; f[n++] = proto; f[n++] = 0; f[n++] = 0;      /* ttl, proto, checksum (unchecked) */
    memcpy(f + n, sip, 4); n += 4; memcpy(f + n, dip, 4); n += 4;
    if (ralert) { f[n++] = 0x94; f[n++] = 0x04; f[n++] = 0; f[n++] = 0; }
    memcpy(f + n, payload, plen); n += plen;
    return n;
}

static size_t udp_frame(uint8_t *f, const uint8_t sip[4], const uint8_t dip[4], uint16_t dport, const uint8_t *body, size_t blen)
{
    uint8_t u[512];
    size_t ulen = 8 + blen;
    u[0] = 0x01; u[1] = 0x3F; u[2] = (uint8_t)(dport >> 8); u[3] = (uint8_t)dport;
    u[4] = (uint8_t)(ulen >> 8); u[5] = (uint8_t)ulen; u[6] = 0; u[7] = 0;
    memcpy(u + 8, body, blen);
    return ipv4_frame(f, false, false, GM, sip, dip, 17, u, ulen);
}

static const uint8_t ALL_HOSTS[4] = {224, 0, 0, 1};
static const uint8_t PTP_GRP[4] = {224, 0, 1, 129};
static const uint8_t QIP[4] = {192, 168, 109, 1};
static const uint8_t GMIP[4] = {192, 168, 109, 50};

static int test_igmp(void)
{
    uint8_t f[128];
    igmp_msg_t m;
    /* v2 general query, max resp 10 s, router alert */
    const uint8_t q2[] = {0x11, 100, 0, 0, 0, 0, 0, 0};
    size_t n = ipv4_frame(f, false, true, SW, QIP, ALL_HOSTS, 2, q2, sizeof q2);
    CHECK(igmp_parse(f, n, &m));
    CHECK_EQ(m.type, 0x11); CHECK_EQ(m.version, 2); CHECK_EQ(m.max_resp_ds, 100); CHECK_EQ(m.qqi_s, 0);
    CHECK(memcmp(m.src_ip, QIP, 4) == 0); CHECK(memcmp(m.src_mac, SW, 6) == 0);
    CHECK_EQ(m.group[0], 0);
    /* v1 general query: max resp 0 */
    const uint8_t q1[] = {0x11, 0, 0, 0, 0, 0, 0, 0};
    n = ipv4_frame(f, true, false, SW, QIP, ALL_HOSTS, 2, q1, sizeof q1);
    CHECK(igmp_parse(f, n, &m)); CHECK_EQ(m.version, 1); CHECK_EQ(m.max_resp_ds, 100);
    /* v3 general query: QRV 2, QQIC 125 */
    const uint8_t q3[] = {0x11, 100, 0, 0, 0, 0, 0, 0, 0x02, 125, 0, 0};
    n = ipv4_frame(f, false, true, SW, QIP, ALL_HOSTS, 2, q3, sizeof q3);
    CHECK(igmp_parse(f, n, &m)); CHECK_EQ(m.version, 3); CHECK_EQ(m.qqi_s, 125); CHECK_EQ(m.qrv, 2); CHECK(!m.s_flag);
    const uint8_t q3s[] = {0x11, 100, 0, 0, 0, 0, 0, 0, 0x0A, 125, 0, 0};
    n = ipv4_frame(f, false, true, SW, QIP, ALL_HOSTS, 2, q3s, sizeof q3s);
    CHECK(igmp_parse(f, n, &m)); CHECK(m.s_flag); CHECK_EQ(m.qrv, 2);
    /* a snooping querier without an address queries from 0.0.0.0 */
    n = ipv4_frame(f, false, true, SW, (const uint8_t[]){0, 0, 0, 0}, ALL_HOSTS, 2, q2, sizeof q2);
    CHECK(igmp_parse(f, n, &m)); CHECK_EQ(m.src_ip[0], 0); CHECK_EQ(m.version, 2);
    /* PIMv2 hello: holdtime 105, DR priority 1, generation id */
    const uint8_t hello[] = {0x20, 0, 0, 0,  0, 1, 0, 2, 0, 105,  0, 19, 0, 4, 0, 0, 0, 1,  0, 20, 0, 4, 0xDE, 0xAD, 0xBE, 0xEF};
    pim_msg_t pm;
    n = ipv4_frame(f, false, false, SW, QIP, (const uint8_t[]){224, 0, 0, 13}, 103, hello, sizeof hello);
    CHECK(pim_parse(f, n, &pm)); CHECK_EQ(pm.type, 0); CHECK_EQ(pm.holdtime_s, 105); CHECK_EQ(pm.dr_priority, 1); CHECK(pm.has_dr_priority);
    CHECK(!igmp_parse(f, n, &m));
    n = ipv4_frame(f, false, true, SW, QIP, ALL_HOSTS, 2, q3, sizeof q3);
    CHECK(!pim_parse(f, n, &pm));
    /* v3 with exponent-coded QQIC 0x8A = (0x0A|0x10) << (0+3) = 208 */
    const uint8_t q3e[] = {0x11, 0x8A, 0, 0, 0, 0, 0, 0, 0x02, 0x8A, 0, 0};
    n = ipv4_frame(f, false, false, SW, QIP, ALL_HOSTS, 2, q3e, sizeof q3e);
    CHECK(igmp_parse(f, n, &m)); CHECK_EQ(m.qqi_s, 208); CHECK_EQ(m.max_resp_ds, 208);
    /* group-specific query */
    const uint8_t qg[] = {0x11, 10, 0, 0, 239, 69, 1, 2};
    n = ipv4_frame(f, false, false, SW, QIP, (const uint8_t[]){239, 69, 1, 2}, 2, qg, sizeof qg);
    CHECK(igmp_parse(f, n, &m)); CHECK_EQ(m.group[0], 239); CHECK_EQ(m.group[3], 2);
    /* v2 report */
    const uint8_t rep[] = {0x16, 0, 0, 0, 224, 0, 1, 129};
    n = ipv4_frame(f, false, false, GM, GMIP, PTP_GRP, 2, rep, sizeof rep);
    CHECK(igmp_parse(f, n, &m)); CHECK_EQ(m.type, 0x16); CHECK_EQ(m.version, 0);
    /* not IGMP: UDP */
    n = udp_frame(f, GMIP, PTP_GRP, 5000, rep, sizeof rep);
    CHECK(!igmp_parse(f, n, &m));
    CHECK(!igmp_parse(NULL, 60, &m));
    CHECK(!igmp_parse(f, n, NULL));
    return 0;
}

/* v2 header + optional announce body */
static size_t ptp2(uint8_t *b, uint8_t type, uint8_t domain, uint16_t flags, const uint8_t *cid, int8_t logint, bool announce)
{
    size_t len = announce ? 64 : 44;
    memset(b, 0, len);
    b[0] = type; b[1] = 0x02; b[2] = (uint8_t)(len >> 8); b[3] = (uint8_t)len; b[4] = domain;
    b[6] = (uint8_t)(flags >> 8); b[7] = (uint8_t)flags;
    memcpy(b + 20, cid, 8); b[28] = 0; b[29] = 1; b[30] = 0; b[31] = 7; b[32] = type == PTP_MSG_SYNC ? 0 : 5; b[33] = (uint8_t)logint;
    if (announce) {
        uint8_t *a = b + 34;
        a[10] = 0; a[11] = 37;             /* utc offset 37 */
        a[13] = 128;                       /* priority1 */
        a[14] = 6; a[15] = 0x21;           /* clock class 6 (GPS locked), accuracy 100 ns */
        a[18] = 128;                       /* priority2 */
        memcpy(a + 19, cid, 8);
        a[27] = 0; a[28] = 1;              /* steps removed 1 */
        a[29] = 0x20;                      /* GPS */
    }
    return len;
}
static const uint8_t CID[8] = {0x00, 0x1D, 0xC1, 0xFF, 0xFE, 0x12, 0x34, 0x56};
static const uint8_t BC[8] = {0x00, 0x11, 0x22, 0xFF, 0xFE, 0x33, 0x44, 0x55};

static int test_ptp(void)
{
    uint8_t b[128], f[256];
    ptp_msg_t m;
    size_t bl = ptp2(b, PTP_MSG_ANNOUNCE, 0, 0x0008, CID, 1, true);
    size_t n = udp_frame(f, GMIP, PTP_GRP, 320, b, bl);
    CHECK(ptp_parse(f, n, &m));
    CHECK_EQ(m.version, 2); CHECK(!m.l2); CHECK_EQ(m.msg_type, PTP_MSG_ANNOUNCE); CHECK(m.announce);
    CHECK_EQ(m.domain, 0); CHECK_EQ(m.priority1, 128); CHECK_EQ(m.clock_class, 6); CHECK_EQ(m.steps_removed, 1);
    CHECK_EQ(m.utc_offset, 37); CHECK_EQ(m.time_source, 0x20); CHECK_STR(ptp_time_source_name(m.time_source), "gps");
    CHECK(memcmp(m.gm_id, CID, 8) == 0); CHECK(memcmp(m.src_ip, GMIP, 4) == 0); CHECK_EQ(m.log_interval, 1);
    CHECK_STR(ptp_msg_name(2, m.msg_type), "announce");
    /* Sync, two-step, domain 127, log interval -3 (8/s) */
    bl = ptp2(b, PTP_MSG_SYNC, 127, 0x0200, CID, -3, false);
    n = udp_frame(f, GMIP, PTP_GRP, 319, b, bl);
    CHECK(ptp_parse(f, n, &m));
    CHECK_EQ(m.msg_type, PTP_MSG_SYNC); CHECK(!m.announce); CHECK_EQ(m.domain, 127); CHECK_EQ(m.log_interval, -3);
    CHECK(m.flags & 0x0200);
    /* layer 2 */
    memcpy(f, (const uint8_t[]){0x01, 0x1B, 0x19, 0, 0, 0}, 6); memcpy(f + 6, GM, 6); f[12] = 0x88; f[13] = 0xF7;
    memcpy(f + 14, b, bl); n = 14 + bl;
    CHECK(ptp_parse(f, n, &m)); CHECK(m.l2); CHECK_EQ(m.src_ip[0], 0); CHECK_EQ(m.domain, 127);
    /* v1 Sync (Dante): versionPTP 1, subdomain "_DFLT", control 0 */
    uint8_t v1[124]; memset(v1, 0, sizeof v1);
    v1[1] = 1; v1[3] = 1; memcpy(v1 + 4, "_DFLT", 5); memcpy(v1 + 22, GM, 6); v1[29] = 1; v1[31] = 9; v1[32] = 0;
    n = udp_frame(f, GMIP, PTP_GRP, 319, v1, sizeof v1);
    CHECK(ptp_parse(f, n, &m));
    CHECK_EQ(m.version, 1); CHECK_EQ(m.msg_type, 0); CHECK_STR(m.subdomain, "_DFLT"); CHECK(memcmp(m.clock_id, GM, 6) == 0);
    CHECK_STR(ptp_msg_name(1, 0), "sync");
    /* wrong port, wrong version nibble */
    n = udp_frame(f, GMIP, PTP_GRP, 5004, b, bl);
    CHECK(!ptp_parse(f, n, &m));
    b[1] = 0x03; n = udp_frame(f, GMIP, PTP_GRP, 319, b, bl);
    CHECK(!ptp_parse(f, n, &m));
    return 0;
}

static int test_bounds(void)
{
    uint8_t b[128], full[256];
    size_t bl = ptp2(b, PTP_MSG_ANNOUNCE, 0, 0, CID, 1, true);
    size_t n = udp_frame(full, GMIP, PTP_GRP, 320, b, bl);
    long pg = sysconf(_SC_PAGESIZE);
    uint8_t *region = mmap(NULL, (size_t)pg * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(region != MAP_FAILED);
    CHECK(mprotect(region + pg, (size_t)pg, PROT_NONE) == 0);
    ptp_msg_t pm; igmp_msg_t im;
    int ok = 0;
    for (size_t len = 0; len <= n; len++) {
        uint8_t *f = region + pg - len;
        memcpy(f, full, len);
        if (ptp_parse(f, len, &pm)) ok++;
        igmp_parse(f, len, &im);
        pim_msg_t pi; pim_parse(f, len, &pi);
    }
    CHECK_EQ(ok, 1);                                     /* the IP total length pins the exact size */
    const uint8_t q3[] = {0x11, 100, 0, 0, 0, 0, 0, 0, 0x02, 125, 0, 0};
    n = ipv4_frame(full, true, true, SW, QIP, ALL_HOSTS, 2, q3, sizeof q3);
    ok = 0;
    for (size_t len = 0; len <= n; len++) {
        uint8_t *f = region + pg - len;
        memcpy(f, full, len);
        if (igmp_parse(f, len, &im)) ok++;
        ptp_parse(f, len, &pm);
    }
    CHECK_EQ(ok, 1);
    munmap(region, (size_t)pg * 2);
    return 0;
}

static int test_tracker(void)
{
    uint8_t b[128], f[256];
    /* IGMP: two general queries, a group query, a report, a second querier */
    igmp_state_t g; igmp_state_reset(&g);
    igmp_msg_t q;
    const uint8_t q3[] = {0x11, 100, 0, 0, 0, 0, 0, 0, 0x02, 125, 0, 0};
    size_t n = ipv4_frame(f, false, true, SW, QIP, ALL_HOSTS, 2, q3, sizeof q3);
    CHECK(igmp_parse(f, n, &q)); igmp_state_update(&g, &q, 1000);
    CHECK(g.seen); CHECK_EQ(g.queries, 1); CHECK_EQ(g.version, 3); CHECK_EQ(g.qqi_s, 125); CHECK_EQ(g.generation, 1);
    igmp_state_update(&g, &q, 126000);
    CHECK_EQ(g.queries, 2); CHECK_EQ(g.interval_ms, 125000); CHECK_EQ(g.querier_changes, 0);
    const uint8_t qg[] = {0x11, 10, 0, 0, 239, 69, 1, 2};
    n = ipv4_frame(f, false, false, SW, QIP, (const uint8_t[]){239, 69, 1, 2}, 2, qg, sizeof qg);
    CHECK(igmp_parse(f, n, &q)); igmp_state_update(&g, &q, 127000);
    CHECK_EQ(g.queries, 2); CHECK_EQ(g.group_queries, 1); CHECK_EQ(g.last_ms, 126000);
    const uint8_t rep[] = {0x16, 0, 0, 0, 224, 0, 1, 129};
    n = ipv4_frame(f, false, false, GM, GMIP, PTP_GRP, 2, rep, sizeof rep);
    CHECK(igmp_parse(f, n, &q)); igmp_state_update(&g, &q, 128000);
    CHECK_EQ(g.reports, 1); CHECK_EQ(g.generation, 2);
    n = ipv4_frame(f, false, true, GM, GMIP, ALL_HOSTS, 2, q3, sizeof q3);
    CHECK(igmp_parse(f, n, &q)); igmp_state_update(&g, &q, 130000);
    CHECK_EQ(g.querier_changes, 1); CHECK(memcmp(g.querier_ip, GMIP, 4) == 0); CHECK(memcmp(g.prev_querier_ip, QIP, 4) == 0); CHECK_EQ(g.queries, 3);
    const uint8_t hello[] = {0x20, 0, 0, 0,  0, 1, 0, 2, 0, 105,  0, 19, 0, 4, 0, 0, 0, 1};
    pim_msg_t pm;
    n = ipv4_frame(f, false, false, SW, QIP, (const uint8_t[]){224, 0, 0, 13}, 103, hello, sizeof hello);
    CHECK(pim_parse(f, n, &pm));
    uint32_t gg = g.generation;
    igmp_state_pim(&g, &pm, 131000); igmp_state_pim(&g, &pm, 161000);
    CHECK(g.pim_seen); CHECK_EQ(g.pim_hellos, 2); CHECK_EQ(g.pim_holdtime_s, 105); CHECK_EQ(g.generation, gg + 1); CHECK_EQ(g.pim_last_ms, 161000);

    /* PTP: announce then syncs; a boundary clock sends sync, GM identity differs */
    ptp_state_t p; ptp_state_reset(&p, 2);
    ptp_msg_t m;
    size_t bl = ptp2(b, PTP_MSG_ANNOUNCE, 0, 0x0008, CID, 1, true);
    n = udp_frame(f, GMIP, PTP_GRP, 320, b, bl);
    CHECK(ptp_parse(f, n, &m)); ptp_state_update(&p, &m, 1000);
    CHECK(p.seen); CHECK(p.have_gm); CHECK_EQ(p.announces, 1); CHECK_EQ(p.domain, 0); CHECK_EQ(p.clock_class, 6);
    CHECK(memcmp(p.gm_id, CID, 8) == 0); CHECK_EQ(p.generation, 2); CHECK_EQ(p.syncs, 0);
    bl = ptp2(b, PTP_MSG_SYNC, 0, 0x0200, BC, -3, false);
    n = udp_frame(f, GMIP, PTP_GRP, 319, b, bl);
    CHECK(ptp_parse(f, n, &m)); ptp_state_update(&p, &m, 1125);
    ptp_state_update(&p, &m, 1250);
    CHECK_EQ(p.syncs, 2); CHECK_EQ(p.sync_interval_ms, 125); CHECK(p.two_step); CHECK_EQ(p.log_sync_interval, -3);
    CHECK(memcmp(p.master_id, BC, 8) == 0); CHECK(memcmp(p.gm_id, CID, 8) == 0); CHECK_EQ(p.generation, 3);
    bl = ptp2(b, PTP_MSG_FOLLOW_UP, 0, 0x0200, BC, -3, false);
    n = udp_frame(f, GMIP, PTP_GRP, 320, b, bl);
    CHECK(ptp_parse(f, n, &m)); ptp_state_update(&p, &m, 1251);
    CHECK_EQ(p.follow_ups, 1);
    /* other domain: counted, not adopted */
    bl = ptp2(b, PTP_MSG_SYNC, 127, 0, BC, -3, false);
    n = udp_frame(f, GMIP, PTP_GRP, 319, b, bl);
    CHECK(ptp_parse(f, n, &m)); ptp_state_update(&p, &m, 1300);
    CHECK_EQ(p.other_domains, 1); CHECK_EQ(p.domain, 0); CHECK_EQ(p.syncs, 2);
    /* delay_req from a slave */
    bl = ptp2(b, PTP_MSG_DELAY_REQ, 0, 0, BC, 0, false);
    n = udp_frame(f, GMIP, PTP_GRP, 319, b, bl);
    CHECK(ptp_parse(f, n, &m)); ptp_state_update(&p, &m, 1400);
    CHECK_EQ(p.delay_reqs, 1); CHECK_EQ(p.generation, 3);
    /* GM change bumps */
    bl = ptp2(b, PTP_MSG_ANNOUNCE, 0, 0x0008, BC, 1, true);
    n = udp_frame(f, GMIP, PTP_GRP, 320, b, bl);
    CHECK(ptp_parse(f, n, &m)); ptp_state_update(&p, &m, 3000);
    CHECK_EQ(p.gm_changes, 1); CHECK(memcmp(p.gm_id, BC, 8) == 0); CHECK_EQ(p.generation, 4);
    /* v1 goes to its own tracker; the v2 tracker ignores it */
    uint8_t v1[124]; memset(v1, 0, sizeof v1);
    v1[1] = 1; v1[3] = 1; memcpy(v1 + 4, "_DFLT", 5); memcpy(v1 + 22, GM, 6); v1[29] = 1;
    n = udp_frame(f, GMIP, PTP_GRP, 319, v1, sizeof v1);
    CHECK(ptp_parse(f, n, &m)); ptp_state_update(&p, &m, 5000);
    CHECK_EQ(p.syncs, 2); CHECK_EQ(p.generation, 4);
    ptp_state_reset(&p, 1);
    ptp_state_update(&p, &m, 1000); ptp_state_update(&p, &m, 2000);
    CHECK_EQ(p.version, 1); CHECK_STR(p.subdomain, "_DFLT"); CHECK_EQ(p.syncs, 2); CHECK_EQ(p.sync_interval_ms, 1000);
    CHECK(memcmp(p.master_id, GM, 6) == 0); CHECK(!p.have_gm);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s igmp|ptp|bounds|tracker\n", argv[0]); return 2; }
    if (!strcmp(argv[1], "igmp")) return test_igmp();
    if (!strcmp(argv[1], "ptp")) return test_ptp();
    if (!strcmp(argv[1], "bounds")) return test_bounds();
    if (!strcmp(argv[1], "tracker")) return test_tracker();
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
