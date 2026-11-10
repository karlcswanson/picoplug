/*
 * cdp.c - see cdp.h.
 */
#include "cdp.h"

#include <string.h>

static const uint8_t CDP_MCAST[6] = {0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCC};
static const uint8_t CDP_SNAP[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x0C, 0x20, 0x00};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool copy_str(char *dst, size_t cap, const uint8_t *src, size_t n)
{
    bool cut = n > cap;
    if (cut) n = cap;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return cut;
}

/* Locate the CDP payload; returns offset or 0 if not CDP. Handles an
 * optional 802.1Q tag. */
static size_t cdp_offset(const uint8_t *f, size_t len, bool *tagged, uint16_t *vid)
{
    if (len < 14 + 8 + 4) return 0;
    if (memcmp(f, CDP_MCAST, 6) != 0) return 0;
    size_t p = 12;
    *tagged = false;
    if (rd16(f + p) == 0x8100) {
        if (len < p + 4 + 8 + 4) return 0;
        *tagged = true;
        *vid = rd16(f + p + 2) & 0x0FFF;
        p += 4;
    }
    /* 802.3 length field (must be <= 1500); then LLC/SNAP */
    if (rd16(f + p) > 1500) return 0;
    p += 2;
    if (p + 8 > len || memcmp(f + p, CDP_SNAP, 8) != 0) return 0;
    return p + 8;
}

bool cdp_is_cdp_frame(const uint8_t *frame, size_t len)
{
    bool t; uint16_t v;
    return frame && cdp_offset(frame, len, &t, &v) != 0;
}

/* Internet checksum as Cisco computes it. For an odd-length payload the
 * last byte is moved into the LOW byte of a final 16-bit word; if that byte
 * is >= 0x80 the word becomes 0xFF00 | (byte - 1) (the off-by-one that IOS
 * has always had, see Wireshark's packet-cdp.c). Returns the checksum field
 * value that makes the payload verify. */
uint16_t cdp_checksum(const uint8_t *d, size_t n)
{
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 1 < n; i += 2) sum += rd16(d + i);
    if (i < n) {
        uint8_t b = d[i];
        sum += (b & 0x80) ? (0xFF00u | (uint32_t)(b - 1)) : b;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* Verifies any of: plain RFC 1071 (generators), the Cisco rule above, or
 * the simplified rule (odd byte in the low byte, no 0xFF00 compensation)
 * that some generators and older dissectors use. Diagnostic only, so a
 * superset is fine. The checksum field is part of d. */
static bool checksum_ok(const uint8_t *d, size_t n)
{
    if (cdp_checksum(d, n) == 0) return true;
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 1 < n; i += 2) sum += rd16(d + i);
    if (i < n) {
        uint32_t s1 = sum + (uint32_t)(d[i] << 8), s2 = sum + d[i];
        while (s1 >> 16) s1 = (s1 & 0xFFFF) + (s1 >> 16);
        while (s2 >> 16) s2 = (s2 & 0xFFFF) + (s2 >> 16);
        return (uint16_t)~s1 == 0 || (uint16_t)~s2 == 0;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum == 0;
}

/* Addresses / Management Addresses: count(4) then records */
static void decode_addresses(lldp_neighbor_t *out, const uint8_t *v, size_t vlen)
{
    if (vlen < 4) return;
    uint32_t count = rd32(v);
    size_t p = 4;
    for (uint32_t i = 0; i < count && p + 4 <= vlen; i++) {
        uint8_t ptype = v[p], plen = v[p + 1];
        if (p + 2 + plen + 2 > vlen) break;
        const uint8_t *proto = v + p + 2;
        uint16_t alen = rd16(v + p + 2 + plen);
        size_t ap = p + 2 + plen + 2;
        if (ap + alen > vlen) break;
        uint8_t afi = 0;
        if (ptype == 1 && plen == 1 && proto[0] == 0xCC && alen == 4) afi = LLDP_AFI_IPV4;
        else if (ptype == 2 && plen == 8 && proto[6] == 0x86 && proto[7] == 0xDD && alen == 16) afi = LLDP_AFI_IPV6;
        if (afi && out->n_mgmt_addrs < LLDP_MAX_MGMT_ADDRS) {
            /* skip duplicates (Addresses and Management Addresses often repeat) */
            bool dup = false;
            for (int k = 0; k < out->n_mgmt_addrs; k++)
                if (out->mgmt_addrs[k].afi == afi && memcmp(out->mgmt_addrs[k].addr, v + ap, alen) == 0) dup = true;
            if (!dup) {
                lldp_mgmt_addr_t *m = &out->mgmt_addrs[out->n_mgmt_addrs++];
                memset(m, 0, sizeof *m);
                m->afi = afi;
                m->len = (uint8_t)alen;
                memcpy(m->addr, v + ap, alen);
                m->if_subtype = LLDP_IFNUM_UNKNOWN;
            }
        } else if (afi) {
            out->truncated = true;
        }
        p = ap + alen;
    }
}

static void record_unknown(lldp_neighbor_t *out, uint16_t type, const uint8_t *v, size_t vlen)
{
    out->n_unknown++;
    if (out->n_unknown_stored >= LLDP_MAX_UNKNOWN_TLVS) return;
    lldp_unknown_tlv_t *u = &out->unknown[out->n_unknown_stored++];
    memset(u, 0, sizeof *u);
    u->type = (uint8_t)(type > 255 ? 255 : type);
    u->is_org = false;
    u->len = (uint16_t)vlen;
    size_t keep = vlen > LLDP_UNKNOWN_RAW_CAP ? LLDP_UNKNOWN_RAW_CAP : vlen;
    u->raw_len = (uint8_t)keep;
    memcpy(u->raw, v, keep);
}

static uint16_t map_caps(uint32_t c)
{
    uint16_t m = 0;
    if (c & CDP_CAP_ROUTER) m |= LLDP_CAP_ROUTER;
    if (c & (CDP_CAP_TB_BRIDGE | CDP_CAP_SR_BRIDGE | CDP_CAP_SWITCH)) m |= LLDP_CAP_BRIDGE;
    if (c & CDP_CAP_HOST) m |= LLDP_CAP_STATION;
    if (c & CDP_CAP_REPEATER) m |= LLDP_CAP_REPEATER;
    if (c & CDP_CAP_PHONE) m |= LLDP_CAP_TELEPHONE;
    if (c & (CDP_CAP_IGMP | CDP_CAP_REMOTE_MGMT | CDP_CAP_CVTA | CDP_CAP_MAC_RELAY)) m |= LLDP_CAP_OTHER;
    return m;
}

cdp_result_t cdp_parse(const uint8_t *frame, size_t len, lldp_neighbor_t *out)
{
    if (!frame || !out) return CDP_ERR_NULL;
    memset(out, 0, sizeof *out);
    out->proto = NEIGHBOR_PROTO_CDP;
    out->power_mdi.power_class = 0xFF;

    if (len < 14) return CDP_ERR_TOO_SHORT;
    bool tagged = false; uint16_t vid = 0;
    size_t p = cdp_offset(frame, len, &tagged, &vid);
    if (p == 0) return memcmp(frame, CDP_MCAST, 6) == 0 && len < 26 ? CDP_ERR_TOO_SHORT : CDP_ERR_NOT_CDP;
    memcpy(out->dst_mac, frame, 6);
    memcpy(out->src_mac, frame + 6, 6);
    out->vlan_tagged = tagged;
    out->tag_vid = vid;

    /* The 802.3 length bounds the payload (ignore Ethernet padding). */
    size_t llen = rd16(frame + (tagged ? 16 : 12));
    size_t end = (tagged ? 18 : 14) + llen;
    if (end > len) return CDP_ERR_TRUNCATED;
    if (end < p + 4) return CDP_ERR_TOO_SHORT;   /* length field shorter than LLC/SNAP + CDP header */
    const uint8_t *c = frame + p;
    size_t clen = end - p;

    out->cdp_version = c[0];
    if (out->cdp_version != 1 && out->cdp_version != 2) return CDP_ERR_BAD_VERSION;
    out->ttl = c[1];
    out->checksum_ok = checksum_ok(c, clen);

    bool have_device = false, have_port = false, have_sysname = false;
    size_t pos = 4;
    while (pos + 4 <= clen) {
        uint16_t type = rd16(c + pos);
        uint16_t tl = rd16(c + pos + 2);
        if (tl < 4 || pos + tl > clen) return CDP_ERR_TRUNCATED;
        const uint8_t *v = c + pos + 4;
        size_t vlen = tl - 4;
        pos += tl;
        out->n_tlvs++;

        switch (type) {
        case CDP_TLV_DEVICE_ID:
            have_device = true;
            out->chassis_id.subtype = LLDP_CHASSIS_LOCAL;
            out->chassis_id.len = (uint8_t)(vlen > LLDP_MAX_ID_LEN ? LLDP_MAX_ID_LEN : vlen);
            memcpy(out->chassis_id.data, v, out->chassis_id.len);
            if (vlen > LLDP_MAX_ID_LEN) out->truncated = true;
            if (!have_sysname) {          /* System Name TLV overrides if present */
                out->has_sys_name = true;
                if (copy_str(out->sys_name, LLDP_MAX_SYS_NAME, v, vlen)) out->truncated = true;
            }
            break;
        case CDP_TLV_PORT_ID:
            have_port = true;
            out->port_id.subtype = LLDP_PORT_IFNAME;
            out->port_id.len = (uint8_t)(vlen > LLDP_MAX_ID_LEN ? LLDP_MAX_ID_LEN : vlen);
            memcpy(out->port_id.data, v, out->port_id.len);
            if (vlen > LLDP_MAX_ID_LEN) out->truncated = true;
            break;
        case CDP_TLV_ADDRESSES:
        case CDP_TLV_MGMT_ADDR:
            decode_addresses(out, v, vlen);
            break;
        case CDP_TLV_CAPABILITIES:
            if (vlen >= 4) {
                out->has_caps = true;
                out->cdp_caps = rd32(v);
                out->caps_supported = out->caps_enabled = map_caps(out->cdp_caps);
            } else record_unknown(out, type, v, vlen);
            break;
        case CDP_TLV_SW_VERSION:
            out->has_sys_desc = true;
            if (copy_str(out->sys_desc, LLDP_MAX_SYS_DESC, v, vlen)) out->truncated = true;
            break;
        case CDP_TLV_PLATFORM:
            out->has_platform = true;
            if (copy_str(out->platform, LLDP_MAX_PLATFORM, v, vlen)) out->truncated = true;
            break;
        case CDP_TLV_VTP_DOMAIN:
            out->has_vtp_domain = true;
            if (copy_str(out->vtp_domain, LLDP_MAX_VTP_DOMAIN, v, vlen)) out->truncated = true;
            break;
        case CDP_TLV_NATIVE_VLAN:
            if (vlen >= 2) { out->has_pvid = true; out->pvid = rd16(v); }
            else record_unknown(out, type, v, vlen);
            break;
        case CDP_TLV_DUPLEX:
            if (vlen >= 1) { out->has_duplex = true; out->duplex_full = v[0] != 0; }
            else record_unknown(out, type, v, vlen);
            break;
        case CDP_TLV_VOIP_VLAN:
            /* status/type(1) vlan(2): the voice VLAN, tagged on the wire */
            if (vlen >= 3 && out->n_med_policies < LLDP_MAX_MED_POLICIES) {
                lldp_med_policy_t *pol = &out->med_policies[out->n_med_policies++];
                memset(pol, 0, sizeof *pol);
                pol->app_type = LLDP_MED_APP_VOICE;
                pol->tagged = true;
                pol->no_qos = true;           /* CDP carries the VLAN only */
                pol->vlan_id = rd16(v + 1) & 0x0FFF;
            } else record_unknown(out, type, v, vlen);
            break;
        case CDP_TLV_POWER:
            if (vlen >= 2) { out->has_power_mw = true; out->power_mw = rd16(v); }
            else record_unknown(out, type, v, vlen);
            break;
        case CDP_TLV_MTU:
            if (vlen == 4) { out->has_mtu = true; out->mtu = rd32(v); }
            else if (vlen == 2) { out->has_mtu = true; out->mtu = rd16(v); }
            else record_unknown(out, type, v, vlen);
            break;
        case CDP_TLV_SYSTEM_NAME:
            have_sysname = true;
            out->has_sys_name = true;
            if (copy_str(out->sys_name, LLDP_MAX_SYS_NAME, v, vlen)) out->truncated = true;
            break;
        case CDP_TLV_POWER_REQUEST:
            /* req_id(2) mgmt_id(2) then one or more power(4) mW; take the first */
            if (vlen >= 8) { out->has_power_req_mw = true; out->power_req_mw = rd32(v + 4); }
            else record_unknown(out, type, v, vlen);
            break;
        case CDP_TLV_POWER_AVAIL:
            if (vlen >= 8) { out->has_power_avail_mw = true; out->power_avail_mw = rd32(v + 4); }
            else record_unknown(out, type, v, vlen);
            break;
        default:
            record_unknown(out, type, v, vlen);
            break;
        }
    }

    if (!have_device || !have_port) return CDP_ERR_TRUNCATED;   /* mandatory in practice */
    if (out->ttl == 0) out->ttl = 180;
    return CDP_OK;
}

const char *cdp_result_str(cdp_result_t r)
{
    switch (r) {
    case CDP_OK: return "ok";
    case CDP_ERR_TOO_SHORT: return "frame too short";
    case CDP_ERR_NOT_CDP: return "not a CDP frame";
    case CDP_ERR_BAD_VERSION: return "unsupported CDP version";
    case CDP_ERR_TRUNCATED: return "TLV runs past end of frame or mandatory TLV missing";
    case CDP_ERR_NULL: return "null argument";
    }
    return "unknown";
}

const char *cdp_cap_name(unsigned bit)
{
    static const char *const names[] = {
        "router", "tb_bridge", "sr_bridge", "switch", "host", "igmp", "repeater",
        "phone", "remote_mgmt", "cvta", "mac_relay",
    };
    return bit < sizeof names / sizeof names[0] ? names[bit] : "unknown";
}
