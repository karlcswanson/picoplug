/*
 * Host-side tests for lldp_tx.c and cdp_tx.c: every frame we build must
 * parse back with our own parsers and carry the fields we put in.
 *
 *   test_tx <test-name>
 */
#include "lldp_tx.h"
#include "cdp_tx.h"
#include "lldp.h"
#include "cdp.h"

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

static const uint8_t MAC[6] = {0x02, 0x50, 0x4C, 0xAA, 0xBB, 0xCC};

static lldp_tx_cfg_t lldp_cfg(void)
{
    lldp_tx_cfg_t c = {
        .mac = MAC, .port_id = "eth0", .port_desc = "RJ45 tester port",
        .sys_name = "picolldp", .sys_desc = "picolldp 0.1.0 on test",
        .ttl_s = 120, .has_ip = true, .ip = {192, 168, 109, 77},
        .speed = 100, .full = true, .med_device_type = 3,
    };
    return c;
}

static int test_lldp_roundtrip(void)
{
    uint8_t f[LLDP_TX_MAX_FRAME];
    lldp_tx_cfg_t c = lldp_cfg();
    size_t n = lldp_tx_build(f, sizeof f, &c);
    CHECK(n > 14 && n <= sizeof f);
    CHECK_EQ(f[0], 0x01); CHECK_EQ(f[5], 0x0E);
    CHECK(memcmp(f + 6, MAC, 6) == 0);

    static lldp_neighbor_t nb;
    CHECK_EQ(lldp_parse(f, n, &nb), LLDP_OK);
    CHECK(nb.saw_end);
    CHECK_EQ(nb.chassis_id.subtype, LLDP_CHASSIS_MAC);
    CHECK(nb.chassis_id.len == 6 && memcmp(nb.chassis_id.data, MAC, 6) == 0);
    CHECK_EQ(nb.port_id.subtype, LLDP_PORT_IFNAME);
    CHECK(nb.port_id.len == 4 && memcmp(nb.port_id.data, "eth0", 4) == 0);
    CHECK_EQ(nb.ttl, 120);
    CHECK(nb.has_port_desc); CHECK_STR(nb.port_desc, "RJ45 tester port");
    CHECK(nb.has_sys_name); CHECK_STR(nb.sys_name, "picolldp");
    CHECK(nb.has_sys_desc); CHECK_STR(nb.sys_desc, "picolldp 0.1.0 on test");
    CHECK(lldp_neighbor_is_picoplug(&nb));
    CHECK(nb.has_caps); CHECK_EQ(nb.caps_enabled, LLDP_CAP_STATION); CHECK_EQ(nb.caps_supported, LLDP_CAP_STATION);
    CHECK_EQ(nb.n_mgmt_addrs, 1);
    CHECK_EQ(nb.mgmt_addrs[0].afi, LLDP_AFI_IPV4);
    CHECK_EQ(nb.mgmt_addrs[0].len, 4);
    CHECK(memcmp(nb.mgmt_addrs[0].addr, c.ip, 4) == 0);
    CHECK(nb.macphy.present); CHECK(nb.macphy.autoneg_supported); CHECK(nb.macphy.autoneg_enabled);
    CHECK_EQ(nb.macphy.mau_type, 16); CHECK_EQ(nb.macphy.pmd_adv, 0x6C00);
    CHECK(!nb.power_mdi.present);
    /* LLDP-MED */
    CHECK(nb.has_med_caps);
    CHECK_EQ(nb.med_caps, 0x0003);
    CHECK_EQ(nb.med_device_type, 3);
    CHECK(!nb.med_power.present);
    CHECK_EQ(nb.n_unknown, 0);
    CHECK(!nb.truncated);
    return 0;
}

static int test_lldp_shutdown(void)
{
    uint8_t f[LLDP_TX_MAX_FRAME];
    lldp_tx_cfg_t c = lldp_cfg();
    c.ttl_s = 0;
    size_t n = lldp_tx_build(f, sizeof f, &c);
    CHECK(n > 0);
    static lldp_neighbor_t nb;
    CHECK_EQ(lldp_parse(f, n, &nb), LLDP_OK);
    CHECK_EQ(nb.ttl, 0);
    CHECK_EQ(nb.n_tlvs, 3);          /* chassis, port, TTL only */
    CHECK(nb.saw_end);
    return 0;
}

static int test_lldp_minimal(void)
{
    /* No IP, no link speed, no MED: still a valid LLDPDU. */
    uint8_t f[LLDP_TX_MAX_FRAME];
    lldp_tx_cfg_t c = { .mac = MAC, .port_id = "eth0", .sys_name = "picolldp", .ttl_s = 120 };
    size_t n = lldp_tx_build(f, sizeof f, &c);
    CHECK(n > 0);
    static lldp_neighbor_t nb;
    CHECK_EQ(lldp_parse(f, n, &nb), LLDP_OK);
    CHECK(!nb.has_port_desc); CHECK(!nb.has_sys_desc);
    CHECK_EQ(nb.n_mgmt_addrs, 0);
    CHECK(!nb.macphy.present); CHECK(!nb.power_mdi.present);
    CHECK(!nb.has_med_caps); CHECK(!nb.med_power.present);
    CHECK(nb.has_caps);
    c.med_device_type = 1;
    n = lldp_tx_build(f, sizeof f, &c);
    CHECK_EQ(lldp_parse(f, n, &nb), LLDP_OK);
    CHECK(nb.has_med_caps); CHECK_EQ(nb.med_caps, 0x0003); CHECK_EQ(nb.med_device_type, 1);
    CHECK(!nb.med_power.present);
    return 0;
}

static int test_lldp_bounds(void)
{
    uint8_t f[LLDP_TX_MAX_FRAME];
    lldp_tx_cfg_t c = lldp_cfg();
    CHECK_EQ(lldp_tx_build(NULL, sizeof f, &c), 0);
    CHECK_EQ(lldp_tx_build(f, sizeof f, NULL), 0);
    c.mac = NULL; CHECK_EQ(lldp_tx_build(f, sizeof f, &c), 0);
    c = lldp_cfg();
    size_t full = lldp_tx_build(f, sizeof f, &c);
    CHECK(full > 0);
    for (size_t cap = 0; cap < full; cap++) CHECK_EQ(lldp_tx_build(f, cap, &c), 0);   /* too small -> 0, never overruns */
    CHECK_EQ(lldp_tx_build(f, full, &c), full);
    /* Over-long strings are cut to the parser's limits, not rejected. */
    static char big[600]; memset(big, 'x', sizeof big - 1);
    c.sys_desc = big; c.port_desc = big; c.sys_name = big; c.port_id = big;
    size_t n = lldp_tx_build(f, sizeof f, &c);
    CHECK(n > 0 && n <= sizeof f);
    static lldp_neighbor_t nb;
    CHECK_EQ(lldp_parse(f, n, &nb), LLDP_OK);
    CHECK_EQ(strlen(nb.sys_desc), LLDP_MAX_SYS_DESC);
    CHECK_EQ(nb.port_id.len, LLDP_MAX_ID_LEN);
    return 0;
}

static cdp_tx_cfg_t cdp_cfg(void)
{
    cdp_tx_cfg_t c = {
        .mac = MAC, .device_id = "picolldp", .port_id = "eth0",
        .sw_version = "picolldp 0.1.0 on test", .platform = "picolldp",
        .ttl_s = 180, .has_ip = true, .ip = {10, 1, 2, 3}, .full_duplex = true,
    };
    return c;
}

static int test_cdp_roundtrip(void)
{
    uint8_t f[CDP_TX_MAX_FRAME];
    cdp_tx_cfg_t c = cdp_cfg();
    size_t n = cdp_tx_build(f, sizeof f, &c);
    CHECK(n > 26);
    CHECK(cdp_is_cdp_frame(f, n));
    /* 802.3 length covers LLC/SNAP + CDP exactly */
    CHECK_EQ(((size_t)f[12] << 8 | f[13]) + 14, n);
    CHECK_EQ((n - 22) & 1, 0);                      /* payload padded to even length */
    static lldp_neighbor_t nb;
    CHECK_EQ(cdp_parse(f, n, &nb), CDP_OK);
    CHECK(nb.checksum_ok);
    CHECK_EQ(nb.proto, NEIGHBOR_PROTO_CDP);
    CHECK_EQ(nb.cdp_version, 2);
    CHECK_EQ(nb.ttl, 180);
    CHECK(nb.has_sys_name); CHECK_STR(nb.sys_name, "picolldp");
    CHECK(nb.port_id.len == 4 && memcmp(nb.port_id.data, "eth0", 4) == 0);
    CHECK_EQ(nb.n_mgmt_addrs, 1);
    CHECK_EQ(nb.mgmt_addrs[0].afi, LLDP_AFI_IPV4);
    CHECK(memcmp(nb.mgmt_addrs[0].addr, c.ip, 4) == 0);
    CHECK(nb.has_caps); CHECK_EQ(nb.cdp_caps, CDP_CAP_HOST);
    CHECK(nb.has_sys_desc); CHECK(strncmp(nb.sys_desc, "picolldp 0.1.0 on test", 22) == 0);
    CHECK(strlen(nb.sys_desc) == 22 || (strlen(nb.sys_desc) == 23 && nb.sys_desc[22] == '\n'));
    CHECK(nb.has_platform); CHECK_STR(nb.platform, "picolldp");
    CHECK(lldp_neighbor_is_picoplug(&nb));
    CHECK(nb.has_duplex); CHECK(nb.duplex_full);
    CHECK(!nb.has_power_mw); CHECK(!nb.has_power_req_mw);
    CHECK_EQ(nb.n_unknown, 0);
    return 0;
}

static int test_cdp_minimal(void)
{
    uint8_t f[CDP_TX_MAX_FRAME];
    cdp_tx_cfg_t c = cdp_cfg();
    c.has_ip = false; c.sw_version = NULL; c.platform = NULL; c.ttl_s = 0;
    size_t n = cdp_tx_build(f, sizeof f, &c);
    CHECK(n > 0);
    static lldp_neighbor_t nb;
    CHECK_EQ(cdp_parse(f, n, &nb), CDP_OK);
    CHECK(nb.checksum_ok);
    CHECK_EQ(nb.n_mgmt_addrs, 0);
    CHECK(!nb.has_sys_desc); CHECK(!nb.has_platform);
    return 0;
}

static int test_cdp_checksum_quirk(void)
{
    /* Odd payload lengths, last byte below and above 0x80, must verify
     * with the Cisco rule, and the whole frame must still verify. */
    uint8_t f[CDP_TX_MAX_FRAME];
    static lldp_neighbor_t nb;
    const char *ids[] = {"a", "ab", "abc", "\x7f", "\xc3\xa9", "picolldp-1"};
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        cdp_tx_cfg_t c = cdp_cfg();
        c.has_ip = false; c.sw_version = NULL; c.platform = ids[i];
        c.device_id = "x";
        size_t n = cdp_tx_build(f, sizeof f, &c);
        CHECK(n > 0);
        CHECK_EQ(cdp_parse(f, n, &nb), CDP_OK);
        CHECK(nb.checksum_ok);
        /* flipping one payload byte breaks it */
        f[n - 1] ^= 0x01;
        CHECK_EQ(cdp_parse(f, n, &nb), CDP_OK);
        CHECK(!nb.checksum_ok);
    }
    /* Direct check of the rule: {0x00,0x00,0x90} -> word 0xFF8F */
    uint8_t d[3] = {0, 0, 0x90};
    CHECK_EQ(cdp_checksum(d, 3), (uint16_t)~0xFF8F);
    uint8_t e[3] = {0, 0, 0x10};
    CHECK_EQ(cdp_checksum(e, 3), (uint16_t)~0x0010);
    return 0;
}

static int test_cdp_bounds(void)
{
    uint8_t f[CDP_TX_MAX_FRAME];
    cdp_tx_cfg_t c = cdp_cfg();
    CHECK_EQ(cdp_tx_build(NULL, sizeof f, &c), 0);
    CHECK_EQ(cdp_tx_build(f, sizeof f, NULL), 0);
    size_t full = cdp_tx_build(f, sizeof f, &c);
    CHECK(full > 0);
    for (size_t cap = 0; cap < full; cap++) CHECK_EQ(cdp_tx_build(f, cap, &c), 0);
    CHECK_EQ(cdp_tx_build(f, full, &c), full);
    return 0;
}

int main(int argc, char **argv)
{
    static const struct { const char *name; int (*fn)(void); } TESTS[] = {
        {"lldp_roundtrip", test_lldp_roundtrip}, {"lldp_shutdown", test_lldp_shutdown},
        {"lldp_minimal", test_lldp_minimal}, {"lldp_bounds", test_lldp_bounds},
        {"cdp_roundtrip", test_cdp_roundtrip}, {"cdp_minimal", test_cdp_minimal},
        {"cdp_checksum_quirk", test_cdp_checksum_quirk}, {"cdp_bounds", test_cdp_bounds},
    };
    if (argc < 2) { fprintf(stderr, "usage: test_tx <name>\n"); return 2; }
    for (size_t i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++)
        if (strcmp(argv[1], TESTS[i].name) == 0) { int rc = TESTS[i].fn(); fprintf(stderr, "tx %s: %s\n", TESTS[i].name, rc ? "FAIL" : "ok"); return rc; }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
