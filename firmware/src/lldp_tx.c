/*
 * lldp_tx.c - see lldp_tx.h.
 */
#include "lldp_tx.h"
#include "lldp.h"

#include <string.h>

static const uint8_t LLDP_MCAST[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};
static const uint8_t OUI_8023[3] = {0x00, 0x12, 0x0F};
static const uint8_t OUI_MED[3]  = {0x00, 0x12, 0xBB};

typedef struct { uint8_t *b; size_t cap, pos; bool overflow; } wr_t;

static void put(wr_t *w, const void *p, size_t n)
{
    if (!n) return;
    if (w->pos + n > w->cap) { w->overflow = true; return; }
    memcpy(w->b + w->pos, p, n);
    w->pos += n;
}
static void put16(wr_t *w, uint16_t v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; put(w, b, 2); }

/* TLV header: 7-bit type, 9-bit length. Values are < 512 by construction;
 * strings longer than LLDP's own limits are cut rather than rejected. */
static void tlv(wr_t *w, uint8_t type, const void *v, size_t n)
{
    if (n > 511) n = 511;
    put16(w, (uint16_t)(((unsigned)type << 9) | (unsigned)n));
    put(w, v, n);
}
static void tlv_str(wr_t *w, uint8_t type, const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n > max) n = max;
    tlv(w, type, s, n);
}
static void tlv_id(wr_t *w, uint8_t type, uint8_t subtype, const void *v, size_t n)
{
    uint8_t tmp[1 + LLDP_MAX_ID_LEN];
    if (n > LLDP_MAX_ID_LEN) n = LLDP_MAX_ID_LEN;
    tmp[0] = subtype;
    memcpy(tmp + 1, v, n);
    tlv(w, type, tmp, n + 1);
}
static void tlv_org(wr_t *w, const uint8_t oui[3], uint8_t subtype, const void *v, size_t n)
{
    uint8_t tmp[4 + 32];
    if (n > sizeof tmp - 4) return;
    memcpy(tmp, oui, 3);
    tmp[3] = subtype;
    memcpy(tmp + 4, v, n);
    tlv(w, LLDP_TLV_ORG, tmp, n + 4);
}

/* RFC 4836 ifMauTypeListBits / 802.3 Annex G, MSB first: 10T 0x4000, 10TFD
 * 0x2000, 100TX 0x0800, 100TXFD 0x0400. The W5500 PHY advertises 10/100. */
#define PMD_ADV_10_100 0x6C00

size_t lldp_tx_build(uint8_t *buf, size_t cap, const lldp_tx_cfg_t *cfg)
{
    if (!buf || !cfg || !cfg->mac || !cfg->port_id || !cfg->sys_name) return 0;
    wr_t w = { buf, cap, 0, false };

    put(&w, LLDP_MCAST, 6);
    put(&w, cfg->mac, 6);
    put16(&w, LLDP_ETHERTYPE);

    /* Mandatory, in this order (802.1AB 8.5). */
    tlv_id(&w, LLDP_TLV_CHASSIS_ID, LLDP_CHASSIS_MAC, cfg->mac, 6);
    tlv_id(&w, LLDP_TLV_PORT_ID, LLDP_PORT_IFNAME, cfg->port_id, strlen(cfg->port_id));
    { uint8_t t[2] = {(uint8_t)(cfg->ttl_s >> 8), (uint8_t)cfg->ttl_s}; tlv(&w, LLDP_TLV_TTL, t, 2); }

    if (cfg->ttl_s == 0) {               /* shutdown LLDPDU carries nothing else */
        tlv(&w, LLDP_TLV_END, NULL, 0);
        return w.overflow ? 0 : w.pos;
    }

    if (cfg->port_desc) tlv_str(&w, LLDP_TLV_PORT_DESC, cfg->port_desc, LLDP_MAX_PORT_DESC);
    tlv_str(&w, LLDP_TLV_SYS_NAME, cfg->sys_name, LLDP_MAX_SYS_NAME);
    if (cfg->sys_desc) tlv_str(&w, LLDP_TLV_SYS_DESC, cfg->sys_desc, LLDP_MAX_SYS_DESC);
    {
        /* supported(2) enabled(2): a station only */
        uint8_t c[4] = {(uint8_t)(LLDP_CAP_STATION >> 8), (uint8_t)LLDP_CAP_STATION,
                        (uint8_t)(LLDP_CAP_STATION >> 8), (uint8_t)LLDP_CAP_STATION};
        tlv(&w, LLDP_TLV_SYS_CAPS, c, 4);
    }
    if (cfg->has_ip) {
        /* addr_len(1) afi(1) addr(4) | if_subtype(1) if_num(4) | oid_len(1) */
        uint8_t m[12] = {5, LLDP_AFI_IPV4, cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3],
                         LLDP_IFNUM_IFINDEX, 0, 0, 0, 1, 0};
        tlv(&w, LLDP_TLV_MGMT_ADDR, m, sizeof m);
    }

    /* 802.3 MAC/PHY Configuration/Status: flags(1) pmd_adv(2) mau(2) */
    if (cfg->speed) {
        uint16_t mau = cfg->speed >= 100 ? (cfg->full ? 16 : 15) : (cfg->full ? 11 : 10);
        uint8_t v[5] = {0x03, PMD_ADV_10_100 >> 8, PMD_ADV_10_100 & 0xFF, (uint8_t)(mau >> 8), (uint8_t)mau};
        tlv_org(&w, OUI_8023, 1, v, sizeof v);
    }

    if (cfg->med_device_type) {
        /* LLDP-MED Capabilities: caps(2) device_type(1). Bits: 0 capabilities,
         * 1 network policy. */
        uint8_t c[3] = {0x00, 0x03, cfg->med_device_type};
        tlv_org(&w, OUI_MED, 1, c, sizeof c);
    }

    tlv(&w, LLDP_TLV_END, NULL, 0);
    return w.overflow ? 0 : w.pos;
}
