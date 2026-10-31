/*
 * lldp.h - IEEE 802.1AB LLDP frame parser.
 *
 * Pure C99, no heap, no hardware or OS dependencies. Builds and tests on the
 * host (see firmware/test) and runs unchanged on the RP2040/RP2350.
 *
 * Usage:
 *   lldp_neighbor_t n;
 *   if (lldp_parse(frame, frame_len, &n) == LLDP_OK) { ... }
 *
 * All output buffers are fixed size. Fields longer than their buffer are
 * truncated (and `truncated` is set); the frame is still accepted. Frames
 * whose TLV length fields run past the end of the buffer are rejected and
 * never read out of bounds.
 */
#ifndef LLDP_H
#define LLDP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Sizing (tune for RAM; all are hard caps, never overrun) ---------- */
#define LLDP_MAX_ID_LEN         64   /* chassis/port ID raw bytes            */
#define LLDP_MAX_PORT_DESC      64
#define LLDP_MAX_SYS_NAME       64
#define LLDP_MAX_SYS_DESC       255
#define LLDP_MAX_MGMT_ADDRS     4
#define LLDP_MAX_VLANS          32
#define LLDP_MAX_VLAN_NAME      32
#define LLDP_MAX_MED_POLICIES   4
#define LLDP_MAX_UNKNOWN_TLVS   8    /* unknown TLVs kept with raw bytes     */
#define LLDP_MAX_PLATFORM       64   /* CDP platform string                  */
#define LLDP_MAX_INV            32   /* inventory strings (serial, model, ...) */
#define LLDP_MAX_VTP_DOMAIN     32   /* CDP VTP domain                       */
#define LLDP_UNKNOWN_RAW_CAP    32   /* raw bytes kept per unknown TLV       */
#define LLDP_MAX_PPVIDS         4    /* 802.1 port and protocol VLAN IDs     */
#define LLDP_MAX_PROTOCOLS      47   /* 802.1 protocol identity names, ", "-joined */
#define LLDP_MAX_LOCATION       95   /* LLDP-MED location, formatted         */
#define LLDP_MAX_MUD_URL        95   /* IANA MUD URL (RFC 8520)              */

/* Size of a buffer that can hold any output of lldp_format_id(). */
#define LLDP_ID_STR_LEN         (8 + 3 * LLDP_MAX_ID_LEN + 1)
/* Size of a buffer that can hold any output of lldp_format_addr(). */
#define LLDP_ADDR_STR_LEN       48

/* ---- Constants ------------------------------------------------------- */
#define LLDP_ETHERTYPE          0x88CC
#define LLDP_VLAN_ETHERTYPE     0x8100

/* TLV types */
enum {
    LLDP_TLV_END          = 0,
    LLDP_TLV_CHASSIS_ID   = 1,
    LLDP_TLV_PORT_ID      = 2,
    LLDP_TLV_TTL          = 3,
    LLDP_TLV_PORT_DESC    = 4,
    LLDP_TLV_SYS_NAME     = 5,
    LLDP_TLV_SYS_DESC     = 6,
    LLDP_TLV_SYS_CAPS     = 7,
    LLDP_TLV_MGMT_ADDR    = 8,
    LLDP_TLV_ORG          = 127,
};

/* Chassis ID subtypes (IEEE 802.1AB-2016 Table 8-2) */
enum {
    LLDP_CHASSIS_COMPONENT  = 1,
    LLDP_CHASSIS_IFALIAS    = 2,
    LLDP_CHASSIS_PORTCOMP   = 3,
    LLDP_CHASSIS_MAC        = 4,
    LLDP_CHASSIS_NETADDR    = 5,
    LLDP_CHASSIS_IFNAME     = 6,
    LLDP_CHASSIS_LOCAL      = 7,
};

/* Port ID subtypes (Table 8-3) */
enum {
    LLDP_PORT_IFALIAS       = 1,
    LLDP_PORT_PORTCOMP      = 2,
    LLDP_PORT_MAC           = 3,
    LLDP_PORT_NETADDR       = 4,
    LLDP_PORT_IFNAME        = 5,
    LLDP_PORT_CIRCUIT       = 6,
    LLDP_PORT_LOCAL         = 7,
};

/* IANA address family numbers used in network-address fields */
enum {
    LLDP_AFI_IPV4 = 1,
    LLDP_AFI_IPV6 = 2,
    LLDP_AFI_802  = 6,   /* 48-bit MAC */
};

/* System capability bits (Table 8-4) */
enum {
    LLDP_CAP_OTHER     = 1 << 0,
    LLDP_CAP_REPEATER  = 1 << 1,
    LLDP_CAP_BRIDGE    = 1 << 2,
    LLDP_CAP_WLAN_AP   = 1 << 3,
    LLDP_CAP_ROUTER    = 1 << 4,
    LLDP_CAP_TELEPHONE = 1 << 5,
    LLDP_CAP_DOCSIS    = 1 << 6,
    LLDP_CAP_STATION   = 1 << 7,
    LLDP_CAP_CVLAN     = 1 << 8,
    LLDP_CAP_SVLAN     = 1 << 9,
    LLDP_CAP_TPMR      = 1 << 10,
};

/* Management address interface numbering subtypes */
enum {
    LLDP_IFNUM_UNKNOWN  = 1,
    LLDP_IFNUM_IFINDEX  = 2,
    LLDP_IFNUM_SYSPORT  = 3,
};

/* LLDP-MED application types (ANSI/TIA-1057 Table 12) */
enum {
    LLDP_MED_APP_VOICE            = 1,
    LLDP_MED_APP_VOICE_SIGNALING  = 2,
    LLDP_MED_APP_GUEST_VOICE      = 3,
    LLDP_MED_APP_GUEST_VOICE_SIG  = 4,
    LLDP_MED_APP_SOFTPHONE_VOICE  = 5,
    LLDP_MED_APP_VIDEO_CONF       = 6,
    LLDP_MED_APP_STREAMING_VIDEO  = 7,
    LLDP_MED_APP_VIDEO_SIGNALING  = 8,
};

/* ---- Result codes ---------------------------------------------------- */
typedef enum {
    LLDP_OK = 0,
    LLDP_ERR_TOO_SHORT,          /* frame shorter than an Ethernet header   */
    LLDP_ERR_NOT_LLDP,           /* EtherType is not 0x88CC                 */
    LLDP_ERR_TRUNCATED,          /* a TLV length runs past the frame end    */
    LLDP_ERR_MANDATORY_ORDER,    /* first three TLVs are not 1, 2, 3        */
    LLDP_ERR_BAD_TLV,            /* a mandatory TLV has an invalid length   */
    LLDP_ERR_NULL,               /* NULL argument                            */
} lldp_result_t;

/* ---- Output structures ----------------------------------------------- */
typedef struct {
    uint8_t subtype;
    uint8_t len;                     /* bytes valid in data[] (<= cap)      */
    uint8_t data[LLDP_MAX_ID_LEN];
} lldp_id_t;

typedef struct {
    uint8_t  afi;                    /* LLDP_AFI_*                          */
    uint8_t  len;                    /* address length in bytes (<= 16)     */
    uint8_t  addr[16];
    uint8_t  if_subtype;             /* LLDP_IFNUM_*                        */
    uint32_t if_number;
} lldp_mgmt_addr_t;

typedef struct {
    uint16_t id;
    char     name[LLDP_MAX_VLAN_NAME + 1];
} lldp_vlan_t;

typedef struct {
    bool     present;
    bool     autoneg_supported;
    bool     autoneg_enabled;
    uint16_t pmd_adv;                /* PMD auto-negotiation advertised    */
    uint16_t mau_type;               /* operational MAU type (RFC 4836)     */
} lldp_macphy_t;

/* IEEE 802.3 Power via MDI (subtype 2), incl. 802.3at DLL extension */
typedef struct {
    bool     present;
    bool     pse;                    /* port class: 1 = PSE, 0 = PD         */
    bool     mdi_power_supported;
    bool     mdi_power_enabled;
    bool     pairs_control;
    uint8_t  power_pairs;            /* 1 = signal, 2 = spare               */
    uint8_t  power_class;            /* 0..4 (already adjusted), 0xFF n/a   */
    bool     dll_present;            /* 802.3at fields below are valid      */
    uint8_t  power_type;             /* 0 Type 2 PSE, 1 Type 2 PD, 2 Type 1 PSE, 3 Type 1 PD */
    uint8_t  power_source;
    uint8_t  power_priority;         /* 0 unknown 1 critical 2 high 3 low   */
    bool     pd_4pid;                /* PD supports powering on both pairs  */
    uint16_t pd_requested_dw;        /* deciwatts                           */
    uint16_t pse_allocated_dw;       /* deciwatts                           */
    /* 802.3bt extension (valid when bt_present) */
    bool     bt_present;
    uint16_t req_a_dw, req_b_dw;     /* dual-signature PD request per mode  */
    uint16_t alloc_a_dw, alloc_b_dw; /* PSE allocation per alternative      */
    uint8_t  pse_status;             /* 1 2-pair, 2 4-pair single sig, 3 4-pair dual sig */
    uint8_t  pd_status;              /* 1 single sig, 2 2-pair dual, 3 4-pair dual */
    uint8_t  pairs_ext;              /* 1 alt A, 2 alt B, 3 both            */
    uint8_t  class_a, class_b;       /* dual-signature class per mode, 1..5 */
    uint8_t  class_ext;              /* 1..8, 15 = dual signature           */
    uint8_t  type_ext;               /* 0 T3 PSE, 1 T4 PSE, 2 T3 ss PD, 3 T3 ds PD, 4 T4 ss PD, 5 T4 ds PD */
    bool     pd_load;
    uint16_t pse_max_dw;             /* PSE maximum available power         */
} lldp_power_mdi_t;

/* IEEE 802.1 Port and Protocol VLAN ID (subtype 2) */
typedef struct {
    uint16_t id;
    bool     supported;
    bool     enabled;
} lldp_ppvid_t;

/* LLDP-MED Location Identification (subtype 3) data formats */
enum { LLDP_LOC_NONE = 0, LLDP_LOC_COORD = 1, LLDP_LOC_CIVIC = 2, LLDP_LOC_ELIN = 3 };

/* LLDP-MED Network Policy (subtype 2) */
typedef struct {
    uint8_t  app_type;               /* LLDP_MED_APP_*                      */
    bool     unknown_policy;         /* U flag                              */
    bool     tagged;                 /* T flag                              */
    uint16_t vlan_id;
    uint8_t  l2_priority;
    uint8_t  dscp;
} lldp_med_policy_t;

/* LLDP-MED Extended Power via MDI (subtype 4) */
typedef struct {
    bool     present;
    uint8_t  power_type;             /* 0 PSE, 1 PD                         */
    uint8_t  power_source;
    uint8_t  power_priority;         /* 0 unknown 1 critical 2 high 3 low   */
    uint16_t power_dw;               /* deciwatts                           */
} lldp_med_power_t;

typedef struct {
    uint8_t  type;                   /* TLV type (127 for org-specific)     */
    bool     is_org;
    uint8_t  oui[3];
    uint8_t  subtype;
    uint16_t len;                    /* original TLV value length           */
    uint8_t  raw_len;                /* bytes kept in raw[]                 */
    uint8_t  raw[LLDP_UNKNOWN_RAW_CAP];
} lldp_unknown_tlv_t;

/* Which protocol produced a neighbor record. lldp_neighbor_t is the
 * unified neighbor structure; cdp.c fills the same fields where they map
 * (device ID -> chassis_id, port ID -> port_id, native VLAN -> pvid, ...)
 * and the CDP-only fields below. */
enum { NEIGHBOR_PROTO_LLDP = 0, NEIGHBOR_PROTO_CDP = 1 };

typedef struct {
    uint8_t  proto;                  /* NEIGHBOR_PROTO_*                    */

    /* Ethernet header */
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    bool     vlan_tagged;
    uint16_t tag_vid;

    /* Mandatory */
    lldp_id_t chassis_id;
    lldp_id_t port_id;
    uint16_t ttl;

    /* Optional basic */
    bool     has_port_desc;
    char     port_desc[LLDP_MAX_PORT_DESC + 1];
    bool     has_sys_name;
    char     sys_name[LLDP_MAX_SYS_NAME + 1];
    bool     has_sys_desc;
    char     sys_desc[LLDP_MAX_SYS_DESC + 1];
    bool     has_caps;
    uint16_t caps_supported;
    uint16_t caps_enabled;
    uint8_t  n_mgmt_addrs;
    lldp_mgmt_addr_t mgmt_addrs[LLDP_MAX_MGMT_ADDRS];

    /* IEEE 802.1 */
    bool     has_pvid;
    uint16_t pvid;
    uint8_t  n_vlans;
    lldp_vlan_t vlans[LLDP_MAX_VLANS];

    /* IEEE 802.3 */
    lldp_macphy_t    macphy;
    lldp_power_mdi_t power_mdi;
    bool     has_max_frame;
    uint16_t max_frame_size;

    /* IEEE 802.1: management VLAN (subtype 6), protocol VLANs (2),
     * protocol identity (4) as names, e.g. "RSTP, LACP, 802.1X" */
    bool     has_mgmt_vid;
    uint16_t mgmt_vid;
    uint8_t  n_ppvids;
    lldp_ppvid_t ppvids[LLDP_MAX_PPVIDS];
    char     protocols[LLDP_MAX_PROTOCOLS + 1];

    /* IEEE 802.3 Energy-Efficient Ethernet (subtype 5): wake times in us */
    bool     has_eee;
    uint16_t eee_tx_us, eee_rx_us;

    /* Cisco 4-pair PoE (00-01-42 subtype 1): raw flag bits */
    bool     has_upoe;
    uint8_t  upoe;

    /* IANA MUD URL (00-00-5e subtype 1). Empty string = not sent. */
    char     mud_url[LLDP_MAX_MUD_URL + 1];

    /* Link Aggregation: IEEE 802.3 subtype 3, or its successor 802.1 subtype 7 */
    bool     has_link_agg;
    bool     link_agg_capable, link_agg_active;
    uint32_t link_agg_port_id;

    /* Inventory: LLDP-MED subtypes 5-11, or vendor TLVs that carry the same
     * (Fortinet 08-5b-0e subtype 2 = serial). Empty string = not sent. */
    char     inv_hw[LLDP_MAX_INV + 1], inv_fw[LLDP_MAX_INV + 1], inv_sw[LLDP_MAX_INV + 1];
    char     inv_serial[LLDP_MAX_INV + 1], inv_manufacturer[LLDP_MAX_INV + 1];
    char     inv_model[LLDP_MAX_INV + 1], inv_asset[LLDP_MAX_INV + 1];

    /* LLDP-MED */
    bool     has_med_caps;
    uint16_t med_caps;
    uint8_t  med_device_type;
    uint8_t  n_med_policies;
    lldp_med_policy_t med_policies[LLDP_MAX_MED_POLICIES];
    lldp_med_power_t  med_power;
    uint8_t  location_format;        /* LLDP_LOC_*                          */
    char     location[LLDP_MAX_LOCATION + 1];

    /* CDP only */
    uint8_t  cdp_version;
    bool     checksum_ok;            /* CDP checksum verified (LLDP: true)  */
    uint32_t cdp_caps;               /* raw CDP capability bits             */
    bool     has_platform;
    char     platform[LLDP_MAX_PLATFORM + 1];
    bool     has_vtp_domain;
    char     vtp_domain[LLDP_MAX_VTP_DOMAIN + 1];
    bool     has_duplex;
    bool     duplex_full;
    bool     has_mtu;
    uint32_t mtu;
    bool     has_power_mw;           /* Power TLV: power the switch supplies */
    uint32_t power_mw;
    bool     has_power_req_mw;       /* Power Request (phones)               */
    uint32_t power_req_mw;
    bool     has_power_avail_mw;     /* Power Available (switches)           */
    uint32_t power_avail_mw;

    /* Bookkeeping */
    uint16_t n_tlvs;                 /* TLVs seen, excluding End            */
    uint16_t n_unknown;              /* unknown / undecoded TLVs seen       */
    uint8_t  n_unknown_stored;
    lldp_unknown_tlv_t unknown[LLDP_MAX_UNKNOWN_TLVS];
    bool     truncated;              /* some field was cut to fit a buffer  */
    bool     saw_end;                /* End of LLDPDU TLV was present       */
} lldp_neighbor_t;

/* ---- API ------------------------------------------------------------- */

/* Parse a complete Ethernet frame (starting at the destination MAC). */
lldp_result_t lldp_parse(const uint8_t *frame, size_t len, lldp_neighbor_t *out);

/* Human-readable name for a result code. */
const char *lldp_result_str(lldp_result_t r);

/*
 * Format a chassis or port ID as "<kind>:<value>", e.g. "mac:00:11:22:33:44:55",
 * "ifname:Gi1/0/12", "local:12", "net:10.0.0.1". Non-printable bytes in string
 * subtypes are rendered as hex. Returns the length that would have been
 * written (snprintf semantics). buf is always NUL-terminated if buflen > 0.
 */
size_t lldp_format_chassis_id(const lldp_id_t *id, char *buf, size_t buflen);
size_t lldp_format_port_id(const lldp_id_t *id, char *buf, size_t buflen);

/*
 * True when the neighbor is another picoplug (system description or CDP
 * platform starting with "picoplug", or "picolldp" from firmware before
 * the rename). Its frames reached us through a switch that floods them,
 * so it is not this port's switch.
 */
bool lldp_neighbor_is_picoplug(const lldp_neighbor_t *n);

/* Format a network address (IPv4 dotted, IPv6 RFC 5952, MAC, or hex). */
size_t lldp_format_addr(uint8_t afi, const uint8_t *addr, uint8_t len,
                        char *buf, size_t buflen);

/* Name of a system capability bit (0..10), or "unknown". */
const char *lldp_cap_name(unsigned bit);

/* Name of an LLDP-MED application type, or "unknown". */
const char *lldp_med_app_name(uint8_t app_type);

/* First MED policy of the given application type, or NULL. */
const lldp_med_policy_t *lldp_find_med_policy(const lldp_neighbor_t *n, uint8_t app_type);

#ifdef __cplusplus
}
#endif

#endif /* LLDP_H */
