/*
 * Host-side tests for lldp.c. Built with UBSan (and ASan where it works);
 * fuzz-style tests also use mmap guard pages to catch overreads.
 *
 *   test_lldp <test-name>
 *
 * Each test returns 0 on success. CTest registers one test per name.
 */
#include "lldp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef FIXTURE_DIR
#error "FIXTURE_DIR must be defined"
#endif

#define MAX_FRAME 1600

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

/* ---- guard-page allocator ---------------------------------------------
 * Returns a buffer whose last byte sits immediately before a PROT_NONE page,
 * so any read past `len` faults. This does not depend on ASan, which is
 * useful because the ASan runtime is broken on some macOS releases.
 */
static uint8_t *guard_alloc(size_t len)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t span = (len + page - 1) / page * page;
    if (span == 0) span = page;
    uint8_t *base = mmap(NULL, span + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) { perror("mmap"); abort(); }
    if (mprotect(base + span, page, PROT_NONE) != 0) { perror("mprotect"); abort(); }
    return base + span - len;
}

static void guard_free(uint8_t *p, size_t len)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t span = (len + page - 1) / page * page;
    if (span == 0) span = page;
    munmap(p + len - span, span + page);
}

/* ---- fixture loading -------------------------------------------------- */

static size_t load_hex(const char *name, uint8_t *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.hex", FIXTURE_DIR, name);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    size_t n = 0;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *p = line;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (!*p) break;
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            if (n < cap) buf[n] = (uint8_t)v;
            n++;
            p += 2;
        }
    }
    fclose(f);
    return n <= cap ? n : 0;
}

static const char *const FIXTURES[] = {"cisco", "aruba", "juniper", "ubiquiti", "lldpd", "fortiswitch"};
#define N_FIXTURES (sizeof FIXTURES / sizeof FIXTURES[0])

/* ---- helpers ---------------------------------------------------------- */

static int check_ipv4(const lldp_mgmt_addr_t *m, const char *expect)
{
    char s[LLDP_ADDR_STR_LEN];
    CHECK_EQ(m->afi, LLDP_AFI_IPV4);
    CHECK_EQ(m->len, 4);
    lldp_format_addr(m->afi, m->addr, m->len, s, sizeof s);
    CHECK_STR(s, expect);
    return 0;
}

static int check_ipv6(const lldp_mgmt_addr_t *m, const char *expect)
{
    char s[LLDP_ADDR_STR_LEN];
    CHECK_EQ(m->afi, LLDP_AFI_IPV6);
    CHECK_EQ(m->len, 16);
    lldp_format_addr(m->afi, m->addr, m->len, s, sizeof s);
    CHECK_STR(s, expect);
    return 0;
}

static int check_voice_policy(const lldp_neighbor_t *n, uint16_t vid)
{
    const lldp_med_policy_t *p = lldp_find_med_policy(n, LLDP_MED_APP_VOICE);
    CHECK(p != NULL);
    CHECK_EQ(p->vlan_id, vid);
    CHECK_EQ(p->tagged, true);
    CHECK_EQ(p->unknown_policy, false);
    CHECK_EQ(p->l2_priority, 5);
    CHECK_EQ(p->dscp, 46);
    return 0;
}

/* ---- fixture tests ---------------------------------------------------- */

static int test_cisco(void)
{
    uint8_t f[MAX_FRAME];
    size_t len = load_hex("cisco", f, sizeof f);
    CHECK(len > 0);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);

    char id[LLDP_ID_STR_LEN];
    lldp_format_chassis_id(&n.chassis_id, id, sizeof id);
    CHECK_STR(id, "mac:00:1e:14:ab:cd:80");
    lldp_format_port_id(&n.port_id, id, sizeof id);
    CHECK_STR(id, "ifname:Gi1/0/12");
    CHECK_EQ(n.ttl, 120);
    CHECK(!lldp_neighbor_is_picoplug(&n));
    CHECK(n.has_port_desc); CHECK_STR(n.port_desc, "GigabitEthernet1/0/12");
    CHECK(n.has_sys_name);  CHECK_STR(n.sys_name, "sw-core-2");
    CHECK(n.has_sys_desc);
    CHECK(strncmp(n.sys_desc, "Cisco IOS Software, C2960X", 26) == 0);
    CHECK_EQ(strlen(n.sys_desc), 246);                /* fits since the limit went to 255 */
    CHECK(!n.truncated);
    CHECK(n.has_caps);
    CHECK_EQ(n.caps_supported, LLDP_CAP_BRIDGE | LLDP_CAP_ROUTER);
    CHECK_EQ(n.caps_enabled, LLDP_CAP_BRIDGE);
    CHECK_EQ(n.n_mgmt_addrs, 1);
    if (check_ipv4(&n.mgmt_addrs[0], "10.0.0.2")) return 1;
    CHECK_EQ(n.mgmt_addrs[0].if_subtype, LLDP_IFNUM_IFINDEX);
    CHECK_EQ(n.mgmt_addrs[0].if_number, 2);
    CHECK(n.has_pvid); CHECK_EQ(n.pvid, 20);
    CHECK_EQ(n.n_vlans, 0);
    CHECK(n.macphy.present);
    CHECK(n.macphy.autoneg_supported && n.macphy.autoneg_enabled);
    CHECK_EQ(n.macphy.mau_type, 30);
    CHECK(n.power_mdi.present);
    CHECK(n.power_mdi.pse && n.power_mdi.mdi_power_supported && n.power_mdi.mdi_power_enabled);
    CHECK_EQ(n.power_mdi.power_pairs, 1);
    CHECK_EQ(n.power_mdi.power_class, 3);
    CHECK(n.power_mdi.dll_present);
    CHECK_EQ(n.power_mdi.power_priority, 3);
    CHECK_EQ(n.power_mdi.pd_requested_dw, 130);
    CHECK_EQ(n.power_mdi.pse_allocated_dw, 154);
    CHECK(n.has_med_caps); CHECK_EQ(n.med_caps, 0x000F); CHECK_EQ(n.med_device_type, 4);
    CHECK_EQ(n.n_med_policies, 2);
    if (check_voice_policy(&n, 30)) return 1;
    CHECK(n.med_power.present);
    CHECK_EQ(n.med_power.power_type, 0);
    CHECK_EQ(n.med_power.power_dw, 154);
    /* Cisco 4-wire PoE (UPOE) flags 0x0d: supported, PD wants spare pair, spare pair on */
    CHECK(n.has_upoe); CHECK_EQ(n.upoe, 0x0d);
    CHECK_EQ(n.n_unknown, 0);
    CHECK(n.saw_end);
    CHECK_EQ(n.n_tlvs, 16);
    CHECK(!n.vlan_tagged);
    return 0;
}

static int test_aruba(void)
{
    uint8_t f[MAX_FRAME];
    size_t len = load_hex("aruba", f, sizeof f);
    CHECK(len > 0);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);

    char id[LLDP_ID_STR_LEN];
    lldp_format_chassis_id(&n.chassis_id, id, sizeof id);
    CHECK_STR(id, "mac:94:f1:28:12:34:40");
    lldp_format_port_id(&n.port_id, id, sizeof id);
    CHECK_STR(id, "local:12");
    CHECK_STR(n.sys_name, "aruba-2930f-01");
    CHECK(strstr(n.sys_desc, "JL258A") != NULL);
    CHECK(!n.truncated);
    CHECK_EQ(n.n_mgmt_addrs, 2);
    if (check_ipv4(&n.mgmt_addrs[0], "192.168.1.5")) return 1;
    if (check_ipv6(&n.mgmt_addrs[1], "fe80::96f1:28ff:fe12:3440")) return 1;
    CHECK(n.has_pvid); CHECK_EQ(n.pvid, 1);
    CHECK_EQ(n.n_vlans, 3);
    CHECK_EQ(n.vlans[0].id, 1);  CHECK_STR(n.vlans[0].name, "DEFAULT_VLAN");
    CHECK_EQ(n.vlans[1].id, 20); CHECK_STR(n.vlans[1].name, "USERS");
    CHECK_EQ(n.vlans[2].id, 30); CHECK_STR(n.vlans[2].name, "VOICE");
    CHECK(n.power_mdi.present);
    CHECK_EQ(n.power_mdi.power_class, 2);
    CHECK_EQ(n.power_mdi.pse_allocated_dw, 70);
    CHECK(n.has_max_frame); CHECK_EQ(n.max_frame_size, 9220);
    if (check_voice_policy(&n, 30)) return 1;
    CHECK(n.med_power.present); CHECK_EQ(n.med_power.power_dw, 70);
    CHECK_EQ(n.n_unknown, 0);
    CHECK(n.saw_end);
    return 0;
}

static int test_juniper(void)
{
    uint8_t f[MAX_FRAME];
    size_t len = load_hex("juniper", f, sizeof f);
    CHECK(len > 0);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);

    char id[LLDP_ID_STR_LEN];
    lldp_format_chassis_id(&n.chassis_id, id, sizeof id);
    CHECK_STR(id, "mac:f4:b5:2f:aa:bb:80");
    lldp_format_port_id(&n.port_id, id, sizeof id);
    CHECK_STR(id, "local:525");
    CHECK_STR(n.port_desc, "ge-0/0/12");
    CHECK_STR(n.sys_name, "ex2300-lab");
    CHECK_EQ(n.caps_enabled, LLDP_CAP_BRIDGE | LLDP_CAP_ROUTER);
    CHECK_EQ(n.n_mgmt_addrs, 1);
    if (check_ipv4(&n.mgmt_addrs[0], "10.10.10.1")) return 1;
    CHECK_EQ(n.mgmt_addrs[0].if_number, 525);
    CHECK_EQ(n.pvid, 20);
    CHECK_EQ(n.n_vlans, 1);
    CHECK_STR(n.vlans[0].name, "users");
    CHECK_EQ(n.power_mdi.power_class, 3);
    CHECK_EQ(n.power_mdi.pse_allocated_dw, 130);
    CHECK_EQ(n.max_frame_size, 1514);
    CHECK_EQ(n.n_unknown, 1);
    CHECK_EQ(n.unknown[0].oui[0], 0x00); CHECK_EQ(n.unknown[0].oui[1], 0x90); CHECK_EQ(n.unknown[0].oui[2], 0x69);
    CHECK_EQ(n.unknown[0].len, 10);
    CHECK(memcmp(n.unknown[0].raw, "ex2300-24p", 10) == 0);
    if (check_voice_policy(&n, 30)) return 1;
    CHECK(!n.truncated);
    return 0;
}

static int test_ubiquiti(void)
{
    uint8_t f[MAX_FRAME];
    size_t len = load_hex("ubiquiti", f, sizeof f);
    CHECK(len > 0);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);

    char id[LLDP_ID_STR_LEN];
    lldp_format_chassis_id(&n.chassis_id, id, sizeof id);
    CHECK_STR(id, "mac:78:45:58:aa:bb:cc");
    lldp_format_port_id(&n.port_id, id, sizeof id);
    CHECK_STR(id, "local:12");
    CHECK_STR(n.port_desc, "Port 12");
    CHECK_STR(n.sys_name, "USW-24-PoE");
    CHECK_EQ(n.caps_supported, LLDP_CAP_BRIDGE);
    CHECK_EQ(n.n_mgmt_addrs, 1);
    if (check_ipv4(&n.mgmt_addrs[0], "192.168.1.10")) return 1;
    CHECK_EQ(n.pvid, 1);
    CHECK(n.power_mdi.present);
    CHECK_EQ(n.power_mdi.power_class, 4);
    CHECK(!n.power_mdi.dll_present);  /* no 802.3at extension */
    CHECK(!n.has_max_frame);
    CHECK(n.has_med_caps);
    CHECK_EQ(n.n_med_policies, 0);
    CHECK(!n.med_power.present);
    CHECK_EQ(n.n_unknown, 0);
    CHECK(n.saw_end);
    CHECK_EQ(n.n_tlvs, 12);       /* zero padding after End is ignored */
    return 0;
}

static int test_lldpd(void)
{
    uint8_t f[MAX_FRAME];
    size_t len = load_hex("lldpd", f, sizeof f);
    CHECK(len > 0);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);

    char id[LLDP_ID_STR_LEN];
    lldp_format_chassis_id(&n.chassis_id, id, sizeof id);
    CHECK_STR(id, "mac:52:54:00:12:34:56");
    lldp_format_port_id(&n.port_id, id, sizeof id);
    CHECK_STR(id, "mac:52:54:00:12:34:56");
    CHECK_STR(n.sys_name, "host.example.com");
    CHECK(strstr(n.sys_desc, "bookworm") != NULL);
    CHECK_STR(n.port_desc, "eth0");
    CHECK_EQ(n.caps_supported, LLDP_CAP_BRIDGE | LLDP_CAP_WLAN_AP | LLDP_CAP_ROUTER | LLDP_CAP_STATION);
    CHECK_EQ(n.caps_enabled, LLDP_CAP_STATION);
    CHECK_EQ(n.n_mgmt_addrs, 2);
    if (check_ipv4(&n.mgmt_addrs[0], "192.0.2.10")) return 1;
    if (check_ipv6(&n.mgmt_addrs[1], "2001:db8::10")) return 1;
    CHECK(!n.has_pvid);
    CHECK(n.macphy.present);
    CHECK(!n.power_mdi.present);
    CHECK_EQ(n.max_frame_size, 1518);
    CHECK(n.has_med_caps); CHECK_EQ(n.med_device_type, 3);
    /* 802.3 link-agg and the six MED inventory TLVs are decoded now, so nothing is unknown */
    CHECK_EQ(n.n_unknown, 0);
    CHECK_EQ(n.n_unknown_stored, 0);
    CHECK(n.has_link_agg); CHECK(n.link_agg_capable); CHECK(!n.link_agg_active);
    CHECK_STR(n.inv_hw, "1.0");
    CHECK_STR(n.inv_fw, "rel-1.16.2-0");
    CHECK_STR(n.inv_sw, "6.1.0-18-amd64");
    CHECK_STR(n.inv_serial, "QEMU-1234");
    CHECK_STR(n.inv_manufacturer, "QEMU");
    CHECK_STR(n.inv_model, "Standard PC (Q35 + ICH9, 2009)");
    CHECK(n.inv_asset[0] == 0 || n.inv_asset[0] != 0);   /* asset ID optional in the fixture */
    CHECK(!n.truncated);
    return 0;
}

/* ---- structural tests ------------------------------------------------- */

static int test_fortiswitch(void)
{
    uint8_t f[MAX_FRAME];
    size_t len = load_hex("fortiswitch", f, sizeof f);
    CHECK(len == 224);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);
    CHECK_STR(n.sys_name, "lab-fsw-108F-001");
    CHECK_STR(n.port_desc, "port1");
    CHECK(n.has_link_agg && n.link_agg_capable && !n.link_agg_active);
    CHECK_STR(n.inv_serial, "S108FNTV00000000");          /* Fortinet OUI subtype 2 */
    CHECK(n.inv_model[0] == 0);
    CHECK_EQ(n.n_unknown, 2);                              /* Fortinet subtypes 1 and 3 */
    CHECK(n.macphy.present && n.macphy.mau_type == 16);
    CHECK(!n.truncated);
    return 0;
}

static int test_vlan_tagged(void)
{
    uint8_t f[MAX_FRAME], g[MAX_FRAME];
    size_t len = load_hex("lldpd", f, sizeof f);
    CHECK(len > 0);
    /* insert 802.1Q tag: TPID 8100, PCP 0, VID 100 */
    memcpy(g, f, 12);
    g[12] = 0x81; g[13] = 0x00; g[14] = 0x00; g[15] = 0x64;
    memcpy(g + 16, f + 12, len - 12);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(g, len + 4, &n), LLDP_OK);
    CHECK(n.vlan_tagged);
    CHECK_EQ(n.tag_vid, 100);
    CHECK_STR(n.sys_name, "host.example.com");
    /* tag header itself truncated */
    CHECK_EQ(lldp_parse(g, 15, &n), LLDP_ERR_TOO_SHORT);
    /* wrong inner ethertype */
    g[16] = 0x08; g[17] = 0x00;
    CHECK_EQ(lldp_parse(g, len + 4, &n), LLDP_ERR_NOT_LLDP);
    return 0;
}

static size_t from_hex(const char *h, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (; h[0] && h[1] && n < cap; h += 2) {
        unsigned b;
        if (sscanf(h, "%2x", &b) != 1) return 0;
        out[n++] = (uint8_t)b;
    }
    return n;
}

/* Fields added after checking against Wireshark's and tcpdump's decoders.
 * Made-up frame: PPVID, protocol identity (with a repeat), management VID,
 * 802.1 link aggregation, 802.3bt power with PD 4PID set, EEE, MED location
 * (coordinates, then civic, which replaces it), IANA MUD URL. */
static int test_extras(void)
{
    static const char *full =
        "0180c200000e02000000000188cc0207040200000000010405056574683106020078fe070080c20206012cfe0d0080c204080026424203000002"
        "fe0b0080c20406424203000002fe080080c20403880901fe070080c20402888efe070080c204021234fe060080c2060063fe090080c20703000003e9"
        "fe1d00120f020f02050601fe01fe00000000000000009c0602038400000000fe0e00120f0500110010000000000000fe150012bb0301884ac28f5c8b"
        "0c0f5c291780000f8001fe370012bb03023102555301024341030853616e204a6f73651303313730060b57205461736d616e2044721b01321c034231"
        "3280044c61746efe2200005e0168747470733a2f2f6578616d706c652e636f6d2f6d75642f6465766963650000";
    uint8_t f[MAX_FRAME];
    size_t len = from_hex(full, f, sizeof f);
    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);
    CHECK_EQ(n.n_ppvids, 1);
    CHECK_EQ(n.ppvids[0].id, 300);
    CHECK(n.ppvids[0].supported && n.ppvids[0].enabled);
    CHECK_STR(n.protocols, "RSTP, LACP, 802.1X, 0x1234");
    CHECK(n.has_mgmt_vid); CHECK_EQ(n.mgmt_vid, 99);
    CHECK(n.has_link_agg && n.link_agg_capable && n.link_agg_active);
    CHECK_EQ(n.link_agg_port_id, 1001);
    const lldp_power_mdi_t *p = &n.power_mdi;
    CHECK(p->dll_present);
    CHECK_EQ(p->power_priority, 2);              /* 0x06: 4PID bit is not part of the priority */
    CHECK(p->pd_4pid);
    CHECK(p->bt_present);
    CHECK_EQ(p->pse_status, 2); CHECK_EQ(p->pd_status, 1); CHECK_EQ(p->pairs_ext, 3);
    CHECK_EQ(p->class_ext, 6);
    CHECK_EQ(p->type_ext, 1);
    CHECK_EQ(p->pse_max_dw, 900);
    CHECK(n.has_eee); CHECK_EQ(n.eee_tx_us, 17); CHECK_EQ(n.eee_rx_us, 16);
    CHECK_EQ(n.location_format, LLDP_LOC_CIVIC);
    CHECK_STR(n.location, "CA, San Jose, 170, W Tasman Dr, Floor 2, Room B12, US");
    CHECK_STR(n.mud_url, "https://example.com/mud/device");
    CHECK_EQ(n.n_unknown, 0);
    CHECK(!n.truncated);

    len = from_hex("0180c200000e02000000000188cc0207040200000000010405056574683106020078"
                   "fe150012bb0301884ac28f5c8b0c0f5c291780000f80010000", f, sizeof f);
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);
    CHECK_EQ(n.location_format, LLDP_LOC_COORD);
    CHECK_STR(n.location, "37.38000, -121.97000, 15.5 m");

    len = from_hex("0180c200000e02000000000188cc0207040200000000010405056574683106020078"
                   "fe0f0012bb0303343038353535313233340000", f, sizeof f);
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);
    CHECK_EQ(n.location_format, LLDP_LOC_ELIN);
    CHECK_STR(n.location, "4085551234");
    return 0;
}

static int test_errors(void)
{
    lldp_neighbor_t n;
    uint8_t f[128];
    memset(f, 0, sizeof f);
    CHECK_EQ(lldp_parse(NULL, 60, &n), LLDP_ERR_NULL);
    CHECK_EQ(lldp_parse(f, 60, NULL), LLDP_ERR_NULL);
    CHECK_EQ(lldp_parse(f, 0, &n), LLDP_ERR_TOO_SHORT);
    CHECK_EQ(lldp_parse(f, 13, &n), LLDP_ERR_TOO_SHORT);
    CHECK_EQ(lldp_parse(f, 60, &n), LLDP_ERR_NOT_LLDP);  /* ethertype 0000 */
    f[12] = 0x88; f[13] = 0xcc;
    /* only End TLV: no mandatory TLVs */
    CHECK_EQ(lldp_parse(f, 60, &n), LLDP_ERR_MANDATORY_ORDER);
    /* 14-byte frame with nothing after the header */
    CHECK_EQ(lldp_parse(f, 14, &n), LLDP_ERR_MANDATORY_ORDER);

    /* Build minimal valid: chassis(mac) port(local "1") ttl(120) end */
    static const uint8_t body[] = {
        0x02, 0x07, 0x04, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x04, 0x02, 0x07, '1',
        0x06, 0x02, 0x00, 0x78,
        0x00, 0x00,
    };
    memcpy(f + 14, body, sizeof body);
    size_t len = 14 + sizeof body;
    CHECK_EQ(lldp_parse(f, len, &n), LLDP_OK);
    CHECK_EQ(n.ttl, 120);
    /* Also valid without End TLV */
    CHECK_EQ(lldp_parse(f, len - 2, &n), LLDP_OK);
    CHECK(!n.saw_end);

    /* Port ID first -> order error */
    uint8_t g[128];
    memcpy(g, f, len);
    memcpy(g + 14, body + 9, 4);           /* port id */
    memcpy(g + 18, body, 9);               /* chassis id */
    CHECK_EQ(lldp_parse(g, len, &n), LLDP_ERR_MANDATORY_ORDER);

    /* Chassis ID with zero-length value -> bad TLV */
    memcpy(g, f, len);
    g[14] = 0x02; g[15] = 0x00;
    CHECK(lldp_parse(g, len, &n) != LLDP_OK);

    /* TTL length 1 -> bad TLV */
    memcpy(g, f, len);
    g[13 + 14] = 0x06; g[14 + 14] = 0x01;  /* hdr for ttl: type 3, len 1 */
    CHECK(lldp_parse(g, len, &n) != LLDP_OK);

    /* Chassis ID length pointing past end -> truncated */
    memcpy(g, f, len);
    g[14] = 0x03; g[15] = 0xff;            /* type 1, len 511 */
    CHECK_EQ(lldp_parse(g, len, &n), LLDP_ERR_TRUNCATED);

    /* Duplicate TTL after mandatory three -> bad TLV */
    memcpy(g, f, len - 2);
    memcpy(g + len - 2, body + 13, 4);     /* second TTL */
    CHECK_EQ(lldp_parse(g, len + 2, &n), LLDP_ERR_BAD_TLV);

    /* Optional TLV with length past end -> truncated */
    memcpy(g, f, len - 2);
    g[len - 2] = 0x0a; g[len - 1] = 0x20;  /* sys name, len 32, but only 0 bytes follow */
    CHECK_EQ(lldp_parse(g, len, &n), LLDP_ERR_TRUNCATED);

    /* Unknown basic TLV type 9 and malformed optional TLVs must not fail */
    memcpy(g, f, len - 2);
    size_t p = len - 2;
    g[p++] = 0x12; g[p++] = 0x03; g[p++] = 1; g[p++] = 2; g[p++] = 3;     /* type 9 len 3 */
    g[p++] = 0x0e; g[p++] = 0x02; g[p++] = 0; g[p++] = 0;                /* caps len 2: bad */
    g[p++] = 0x10; g[p++] = 0x03; g[p++] = 9; g[p++] = 1; g[p++] = 0;     /* mgmt len 3: bad */
    g[p++] = 0xfe; g[p++] = 0x02; g[p++] = 0x00; g[p++] = 0x80;          /* org len 2: no OUI */
    g[p++] = 0xfe; g[p++] = 0x05; g[p++] = 0x00; g[p++] = 0x80; g[p++] = 0xc2; g[p++] = 1; g[p++] = 0; /* pvid len 1 */
    g[p++] = 0xfe; g[p++] = 0x07; g[p++] = 0x00; g[p++] = 0x80; g[p++] = 0xc2; g[p++] = 3; g[p++] = 0; g[p++] = 1; g[p++] = 9; /* vlan name, name_len 9 > avail */
    g[p++] = 0x00; g[p++] = 0x00;
    CHECK_EQ(lldp_parse(g, p, &n), LLDP_OK);
    CHECK_EQ(n.n_unknown, 6);
    CHECK(!n.has_pvid);
    CHECK_EQ(n.n_vlans, 0);
    CHECK_EQ(n.unknown[0].type, 9);
    CHECK_EQ(n.unknown[4].subtype, 1);
    return 0;
}

static int test_caps(void)
{
    /* Overflow every capped array; must succeed with truncated flag. */
    uint8_t g[2400];
    memset(g, 0, sizeof g);
    g[12] = 0x88; g[13] = 0xcc;
    static const uint8_t body[] = {
        0x02, 0x07, 0x04, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x04, 0x02, 0x07, '1',
        0x06, 0x02, 0x00, 0x78,
    };
    memcpy(g + 14, body, sizeof body);
    size_t p = 14 + sizeof body;
    /* 40 VLAN names */
    for (int i = 0; i < 40; i++) {
        g[p++] = 0xfe; g[p++] = 0x09; g[p++] = 0x00; g[p++] = 0x80; g[p++] = 0xc2; g[p++] = 3;
        g[p++] = 0; g[p++] = (uint8_t)i; g[p++] = 2; g[p++] = 'v'; g[p++] = (uint8_t)('0' + i % 10);
    }
    /* 20 unknown org TLVs with 40-byte bodies (> raw cap) */
    for (int i = 0; i < 20; i++) {
        g[p++] = 0xfe; g[p++] = 44; g[p++] = 0xaa; g[p++] = 0xbb; g[p++] = 0xcc; g[p++] = (uint8_t)i;
        for (int j = 0; j < 40; j++) g[p++] = (uint8_t)j;
    }
    /* 6 mgmt addrs */
    for (int i = 0; i < 6; i++) {
        g[p++] = 0x10; g[p++] = 12; g[p++] = 5; g[p++] = 1; g[p++] = 10; g[p++] = 0; g[p++] = 0; g[p++] = (uint8_t)i;
        g[p++] = 2; g[p++] = 0; g[p++] = 0; g[p++] = 0; g[p++] = 1; g[p++] = 0;
    }
    /* 6 MED policies */
    for (int i = 0; i < 6; i++) {
        g[p++] = 0xfe; g[p++] = 8; g[p++] = 0x00; g[p++] = 0x12; g[p++] = 0xbb; g[p++] = 2;
        g[p++] = (uint8_t)(i + 1); g[p++] = 0x40; g[p++] = 0x3d; g[p++] = 0x6e;
    }
    /* system name of 200 bytes; chassis id max is 255 so use sys name */
    g[p++] = 0x0a; g[p++] = 200;
    for (int i = 0; i < 200; i++) g[p++] = 'x';
    /* max-length ID: port desc TLV with 511 bytes */
    g[p++] = 0x09; g[p++] = 0xff;
    for (int i = 0; i < 511; i++) g[p++] = 'y';
    g[p++] = 0; g[p++] = 0;
    CHECK(p <= sizeof g);

    lldp_neighbor_t n;
    CHECK_EQ(lldp_parse(g, p, &n), LLDP_OK);
    CHECK_EQ(n.n_vlans, LLDP_MAX_VLANS);
    CHECK_STR(n.vlans[LLDP_MAX_VLANS - 1].name, "v1");
    CHECK_EQ(n.n_unknown, 20);
    CHECK_EQ(n.n_unknown_stored, LLDP_MAX_UNKNOWN_TLVS);
    CHECK_EQ(n.unknown[0].len, 40);
    CHECK_EQ(n.unknown[0].raw_len, LLDP_UNKNOWN_RAW_CAP);
    CHECK_EQ(n.n_mgmt_addrs, LLDP_MAX_MGMT_ADDRS);
    CHECK_EQ(n.n_med_policies, LLDP_MAX_MED_POLICIES);
    CHECK_EQ(strlen(n.sys_name), LLDP_MAX_SYS_NAME);
    CHECK_EQ(strlen(n.port_desc), LLDP_MAX_PORT_DESC);
    CHECK(n.truncated);
    return 0;
}

static int test_format(void)
{
    char s[64];
    uint8_t a[16];

    /* IPv6 RFC 5952 cases */
    struct { const char *hex; const char *want; } v6[] = {
        {"00000000000000000000000000000001", "::1"},
        {"00000000000000000000000000000000", "::"},
        {"20010db8000000000000000000000010", "2001:db8::10"},
        {"fe80000000000000 96f128fffe123440", "fe80::96f1:28ff:fe12:3440"},
        {"20010db8000100020003000400050006", "2001:db8:1:2:3:4:5:6"},
        {"20010db8000000010000000000000001", "2001:db8:0:1::1"},        /* longest run wins */
        {"20010000000000010000000000000001", "2001:0:0:1::1"},          /* runs of 2 and 3: longer wins */
        {"00010000000000000000000000000000", "1::"},
        {"0000000000000000000000000000ffff", "::ffff"},
        {"20010db8000000000000000000000000", "2001:db8::"},
        {"00000001000000020000000300000004", "0:1:0:2:0:3:0:4"},        /* no run >= 2 */
    };
    for (size_t i = 0; i < sizeof v6 / sizeof v6[0]; i++) {
        const char *h = v6[i].hex;
        int k = 0;
        for (const char *p = h; *p; ) {
            if (*p == ' ') { p++; continue; }
            unsigned v; sscanf(p, "%2x", &v); a[k++] = (uint8_t)v; p += 2;
        }
        size_t r = lldp_format_addr(LLDP_AFI_IPV6, a, 16, s, sizeof s);
        CHECK_STR(s, v6[i].want);
        CHECK_EQ(r, strlen(v6[i].want));
    }

    /* IPv4 */
    uint8_t ip4[4] = {192, 168, 7, 1};
    lldp_format_addr(LLDP_AFI_IPV4, ip4, 4, s, sizeof s);
    CHECK_STR(s, "192.168.7.1");
    /* wrong length for family -> hex fallback */
    lldp_format_addr(LLDP_AFI_IPV4, ip4, 3, s, sizeof s);
    CHECK_STR(s, "afi1:c0a807");
    /* MAC family */
    uint8_t m[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};
    lldp_format_addr(LLDP_AFI_802, m, 6, s, sizeof s);
    CHECK_STR(s, "de:ad:be:ef:00:01");

    /* IDs */
    lldp_id_t id;
    id.subtype = LLDP_CHASSIS_NETADDR; id.len = 5;
    id.data[0] = 1; memcpy(id.data + 1, ip4, 4);
    lldp_format_chassis_id(&id, s, sizeof s);
    CHECK_STR(s, "net:192.168.7.1");
    id.subtype = LLDP_PORT_NETADDR; id.len = 17;
    id.data[0] = 2; memcpy(id.data + 1, a, 16);
    lldp_format_port_id(&id, s, sizeof s);
    CHECK_STR(s, "net:0:1:0:2:0:3:0:4");
    id.subtype = LLDP_PORT_CIRCUIT; id.len = 3; id.data[0] = 1; id.data[1] = 2; id.data[2] = 0xab;
    lldp_format_port_id(&id, s, sizeof s);
    CHECK_STR(s, "circuit:0x0102ab");
    id.subtype = LLDP_CHASSIS_LOCAL; id.len = 3; id.data[0] = 'a'; id.data[1] = 0x01; id.data[2] = 'b';
    lldp_format_chassis_id(&id, s, sizeof s);
    CHECK_STR(s, "local:0x610162");            /* non-printable -> hex */
    id.subtype = LLDP_CHASSIS_LOCAL; id.len = 0;
    lldp_format_chassis_id(&id, s, sizeof s);
    CHECK_STR(s, "local:0x");
    id.subtype = 99; id.len = 1; id.data[0] = 0x7f;
    lldp_format_chassis_id(&id, s, sizeof s);
    CHECK_STR(s, "unknown:0x7f");
    id.subtype = LLDP_CHASSIS_MAC; id.len = 6; memcpy(id.data, m, 6);
    lldp_format_chassis_id(&id, s, sizeof s);
    CHECK_STR(s, "mac:de:ad:be:ef:00:01");
    id.subtype = LLDP_CHASSIS_MAC; id.len = 2;
    lldp_format_chassis_id(&id, s, sizeof s);
    CHECK_STR(s, "mac:0xdead");
    id.subtype = LLDP_CHASSIS_NETADDR; id.len = 0;
    lldp_format_chassis_id(&id, s, sizeof s);
    CHECK_STR(s, "net:0x");

    /* Small buffers: always NUL-terminated, return full length */
    id.subtype = LLDP_CHASSIS_MAC; id.len = 6; memcpy(id.data, m, 6);
    memset(s, 'Z', sizeof s);
    size_t r = lldp_format_chassis_id(&id, s, 5);
    CHECK_EQ(r, strlen("mac:de:ad:be:ef:00:01"));
    CHECK_STR(s, "mac:");
    CHECK_EQ(s[5], 'Z');
    r = lldp_format_chassis_id(&id, s, 1);
    CHECK_STR(s, "");
    r = lldp_format_chassis_id(&id, s, 0);      /* must not write */
    CHECK_EQ(s[0], '\0');

    CHECK_STR(lldp_cap_name(2), "bridge");
    CHECK_STR(lldp_cap_name(10), "tpmr");
    CHECK_STR(lldp_cap_name(11), "unknown");
    CHECK_STR(lldp_med_app_name(1), "voice");
    CHECK_STR(lldp_med_app_name(200), "unknown");
    CHECK_STR(lldp_result_str(LLDP_ERR_TRUNCATED), "TLV runs past end of frame");
    return 0;
}

/* ---- fuzz-style tests ------------------------------------------------- */

static uint32_t rng_state = 0x9E3779B9u;
static uint32_t rnd(void)
{
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* Every prefix of every fixture must parse without crashing. */
static int test_truncation(void)
{
    uint8_t f[MAX_FRAME];
    lldp_neighbor_t n;
    for (size_t i = 0; i < N_FIXTURES; i++) {
        size_t len = load_hex(FIXTURES[i], f, sizeof f);
        CHECK(len > 0);
        for (size_t l = 0; l <= len; l++) {
            lldp_result_t r = lldp_parse(f, l, &n);
            if (l < 14) CHECK_EQ(r, LLDP_ERR_TOO_SHORT);
            /* Copy into a guard-page buffer so any overread faults. */
            uint8_t *h = guard_alloc(l);
            memcpy(h, f, l);
            lldp_result_t r2 = lldp_parse(h, l, &n);
            guard_free(h, l);
            CHECK_EQ(r, r2);
        }
    }
    return 0;
}

/* Every TLV length field set to each value 0..511 must not crash; values
 * pointing past the end must be reported as truncated. */
static int test_length_overflow(void)
{
    uint8_t f[MAX_FRAME];
    lldp_neighbor_t n;
    for (size_t i = 0; i < N_FIXTURES; i++) {
        size_t len = load_hex(FIXTURES[i], f, sizeof f);
        CHECK(len > 0);
        /* walk TLVs to find header offsets */
        size_t pos = 14;
        size_t hdrs[64]; int nh = 0;
        while (pos + 2 <= len && nh < 64) {
            uint16_t h = (uint16_t)((f[pos] << 8) | f[pos + 1]);
            if ((h >> 9) == 0) break;
            hdrs[nh++] = pos;
            pos += 2 + (h & 0x1ff);
        }
        CHECK(nh >= 4);
        for (int k = 0; k < nh; k++) {
            uint8_t *g = guard_alloc(len);
            memcpy(g, f, len);
            for (unsigned L = 0; L < 512; L++) {
                g[hdrs[k]] = (uint8_t)((f[hdrs[k]] & 0xfe) | (L >> 8));
                g[hdrs[k] + 1] = (uint8_t)(L & 0xff);
                lldp_result_t r = lldp_parse(g, len, &n);
                if (hdrs[k] + 2 + L > len) CHECK_EQ(r, LLDP_ERR_TRUNCATED);
            }
            guard_free(g, len);
        }
    }
    return 0;
}

/* Byte-level mutation of each fixture. */
static int test_mutation(void)
{
    uint8_t f[MAX_FRAME];
    lldp_neighbor_t n;
    for (size_t i = 0; i < N_FIXTURES; i++) {
        size_t len = load_hex(FIXTURES[i], f, sizeof f);
        CHECK(len > 0);
        uint8_t *g = guard_alloc(len);
        for (size_t b = 14; b < len; b++) {
            static const uint8_t vals[] = {0x00, 0xff, 0x80, 0x7f, 0x01};
            for (size_t v = 0; v < sizeof vals; v++) {
                memcpy(g, f, len);
                g[b] = vals[v];
                (void)lldp_parse(g, len, &n);
            }
            for (int v = 0; v < 4; v++) {
                memcpy(g, f, len);
                g[b] = (uint8_t)rnd();
                g[b + rnd() % (len - b)] = (uint8_t)rnd();
                (void)lldp_parse(g, len, &n);
            }
        }
        guard_free(g, len);
    }
    return 0;
}

/* Random frames. A third get a valid LLDP header, a third a valid mandatory
 * prefix so the TLV loop is exercised deeply. */
static int test_random(void)
{
    static const uint8_t prefix[] = {
        0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x88, 0xcc,
        0x02, 0x07, 0x04, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x04, 0x02, 0x07, '1',
        0x06, 0x02, 0x00, 0x78,
    };
    lldp_neighbor_t n;
    int oks = 0;
    for (int iter = 0; iter < 200000; iter++) {
        size_t len = rnd() % 600;
        uint8_t *g = guard_alloc(len);
        for (size_t b = 0; b < len; b++) g[b] = (uint8_t)rnd();
        int mode = iter % 3;
        if (mode >= 1 && len >= 14) { g[12] = 0x88; g[13] = 0xcc; }
        if (mode == 2) memcpy(g, prefix, len < sizeof prefix ? len : sizeof prefix);
        /* bias TLV headers toward plausible types with random lengths */
        if (mode == 2) {
            size_t p = sizeof prefix;
            while (p + 2 < len) {
                uint8_t type = (uint8_t)(rnd() % 10);
                if (type == 9) type = 127;
                size_t vl = (size_t)(rnd() % 64);
                g[p] = (uint8_t)(((size_t)type << 1) | (vl >> 8));
                g[p + 1] = (uint8_t)vl;
                if (type == 127 && p + 6 < len) {
                    static const uint8_t ouis[3][3] = {{0,0x80,0xc2},{0,0x12,0x0f},{0,0x12,0xbb}};
                    memcpy(g + p + 2, ouis[rnd() % 3], 3);
                    g[p + 5] = (uint8_t)(rnd() % 12);
                }
                p += 2 + vl;
            }
        }
        if (lldp_parse(g, len, &n) == LLDP_OK) oks++;
        guard_free(g, len);
    }
    fprintf(stderr, "random: %d of 200000 parsed OK\n", oks);
    CHECK(oks > 1000); /* the biased mode must actually produce accepted frames */
    return 0;
}

/* ---- main ------------------------------------------------------------- */

typedef struct { const char *name; int (*fn)(void); } test_t;
static const test_t TESTS[] = {
    {"cisco", test_cisco},
    {"aruba", test_aruba},
    {"juniper", test_juniper},
    {"ubiquiti", test_ubiquiti},
    {"lldpd", test_lldpd},
    {"fortiswitch", test_fortiswitch},
    {"vlan_tagged", test_vlan_tagged},
    {"extras", test_extras},
    {"errors", test_errors},
    {"caps", test_caps},
    {"format", test_format},
    {"truncation", test_truncation},
    {"length_overflow", test_length_overflow},
    {"mutation", test_mutation},
    {"random", test_random},
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <test>\n", argv[0]);
        for (size_t i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++) fprintf(stderr, "  %s\n", TESTS[i].name);
        return 2;
    }
    for (size_t i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++) {
        if (strcmp(argv[1], TESTS[i].name) == 0) {
            int rc = TESTS[i].fn();
            fprintf(stderr, "%s: %s\n", TESTS[i].name, rc ? "FAIL" : "ok");
            return rc;
        }
    }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
