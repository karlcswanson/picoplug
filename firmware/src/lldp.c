/*
 * lldp.c - IEEE 802.1AB LLDP frame parser. See lldp.h.
 *
 * Every read of the input buffer goes through a bounds check. The parser
 * never allocates and never reads past `len`.
 */
#include "lldp.h"

#include <string.h>

/* ---- small helpers ---------------------------------------------------- */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static const uint8_t OUI_8021[3] = {0x00, 0x80, 0xC2};
static const uint8_t OUI_8023[3] = {0x00, 0x12, 0x0F};
static const uint8_t OUI_MED[3]  = {0x00, 0x12, 0xBB};
static const uint8_t OUI_FORTINET[3] = {0x08, 0x5B, 0x0E};

/* Copy up to `cap` bytes into a NUL-terminated string; returns true if cut. */
static bool copy_str(char *dst, size_t cap, const uint8_t *src, size_t n)
{
    bool cut = n > cap;
    if (cut) n = cap;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return cut;
}

static bool copy_id(lldp_id_t *id, uint8_t subtype, const uint8_t *src, size_t n)
{
    bool cut = n > LLDP_MAX_ID_LEN;
    if (cut) n = LLDP_MAX_ID_LEN;
    id->subtype = subtype;
    id->len = (uint8_t)n;
    memcpy(id->data, src, n);
    return cut;
}

static void record_unknown(lldp_neighbor_t *out, uint8_t type, bool is_org,
                           const uint8_t *oui, uint8_t subtype,
                           const uint8_t *val, size_t vlen)
{
    out->n_unknown++;
    if (out->n_unknown_stored >= LLDP_MAX_UNKNOWN_TLVS) return;
    lldp_unknown_tlv_t *u = &out->unknown[out->n_unknown_stored++];
    u->type = type;
    u->is_org = is_org;
    if (is_org) memcpy(u->oui, oui, 3); else memset(u->oui, 0, 3);
    u->subtype = subtype;
    u->len = (uint16_t)vlen;
    size_t keep = vlen > LLDP_UNKNOWN_RAW_CAP ? LLDP_UNKNOWN_RAW_CAP : vlen;
    u->raw_len = (uint8_t)keep;
    memcpy(u->raw, val, keep);
}

/* ---- TLV decoders ------------------------------------------------------
 * Each takes the TLV value (v, vlen), already known to lie inside the frame.
 * They return false only for a mandatory TLV that is structurally invalid.
 */

static void decode_mgmt_addr(lldp_neighbor_t *out, const uint8_t *v, size_t vlen)
{
    /* addr_str_len(1) | afi(1) | addr(n) | if_subtype(1) | if_num(4) | oid_len(1) | oid */
    if (vlen < 9) goto bad;
    uint8_t addr_str_len = v[0];
    if (addr_str_len < 2 || (size_t)addr_str_len + 1 > vlen) goto bad;
    uint8_t afi = v[1];
    size_t addr_len = addr_str_len - 1;
    if (addr_len > 16) goto bad;
    size_t p = 1 + addr_str_len;
    if (p + 5 > vlen) goto bad;

    if (out->n_mgmt_addrs >= LLDP_MAX_MGMT_ADDRS) { out->truncated = true; return; }
    lldp_mgmt_addr_t *m = &out->mgmt_addrs[out->n_mgmt_addrs++];
    m->afi = afi;
    m->len = (uint8_t)addr_len;
    memset(m->addr, 0, sizeof m->addr);
    memcpy(m->addr, &v[2], addr_len);
    m->if_subtype = v[p];
    m->if_number = rd32(&v[p + 1]);
    /* OID is ignored; its length byte and bytes are bounds-checked but unused */
    return;
bad:
    record_unknown(out, LLDP_TLV_MGMT_ADDR, false, NULL, 0, v, vlen);
}

static void decode_8021(lldp_neighbor_t *out, uint8_t subtype, const uint8_t *v, size_t vlen)
{
    switch (subtype) {
    case 1: /* Port VLAN ID */
        if (vlen >= 2) { out->has_pvid = true; out->pvid = rd16(v); return; }
        break;
    case 3: { /* VLAN Name: vid(2) | name_len(1) | name */
        if (vlen < 3) break;
        uint8_t nlen = v[2];
        if ((size_t)nlen + 3 > vlen) break;
        if (out->n_vlans >= LLDP_MAX_VLANS) { out->truncated = true; return; }
        lldp_vlan_t *vl = &out->vlans[out->n_vlans++];
        vl->id = rd16(v);
        if (copy_str(vl->name, LLDP_MAX_VLAN_NAME, &v[3], nlen)) out->truncated = true;
        return;
    }
    default:
        break;
    }
    record_unknown(out, LLDP_TLV_ORG, true, OUI_8021, subtype, v, vlen);
}

static void decode_8023(lldp_neighbor_t *out, uint8_t subtype, const uint8_t *v, size_t vlen)
{
    switch (subtype) {
    case 1: /* MAC/PHY Configuration/Status: flags(1) | pmd_adv(2) | mau(2) */
        if (vlen >= 5) {
            out->macphy.present = true;
            out->macphy.autoneg_supported = (v[0] & 0x01) != 0;
            out->macphy.autoneg_enabled = (v[0] & 0x02) != 0;
            out->macphy.pmd_adv = rd16(&v[1]);
            out->macphy.mau_type = rd16(&v[3]);
            return;
        }
        break;
    case 2: { /* Power via MDI: support(1) | pairs(1) | class(1) [| at ext (5)] [| bt ext] */
        if (vlen < 3) break;
        lldp_power_mdi_t *p = &out->power_mdi;
        p->present = true;
        p->pse = (v[0] & 0x01) != 0;
        p->mdi_power_supported = (v[0] & 0x02) != 0;
        p->mdi_power_enabled = (v[0] & 0x04) != 0;
        p->pairs_control = (v[0] & 0x08) != 0;
        p->power_pairs = v[1];
        /* 802.3: 1 = class 0, 2 = class 1, ... 5 = class 4 */
        p->power_class = (v[2] >= 1 && v[2] <= 5) ? (uint8_t)(v[2] - 1) : 0xFF;
        if (vlen >= 8) {
            p->dll_present = true;
            p->power_type = (v[3] >> 6) & 0x03;
            p->power_source = (v[3] >> 4) & 0x03;
            p->power_priority = v[3] & 0x0F;
            p->pd_requested_dw = rd16(&v[4]);
            p->pse_allocated_dw = rd16(&v[6]);
        }
        /* 802.3bt fields (vlen >= 29) are not decoded yet. */
        return;
    }
    case 3: /* Link Aggregation: status(1) | aggregated port id(4) */
        if (vlen >= 5) {
            out->has_link_agg = true;
            out->link_agg_capable = (v[0] & 0x01) != 0;
            out->link_agg_active = (v[0] & 0x02) != 0;
            out->link_agg_port_id = ((uint32_t)v[1] << 24) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 8) | v[4];
            return;
        }
        break;
    case 4: /* Maximum Frame Size */
        if (vlen >= 2) { out->has_max_frame = true; out->max_frame_size = rd16(v); return; }
        break;
    default:
        break;
    }
    record_unknown(out, LLDP_TLV_ORG, true, OUI_8023, subtype, v, vlen);
}

static void decode_med(lldp_neighbor_t *out, uint8_t subtype, const uint8_t *v, size_t vlen)
{
    switch (subtype) {
    case 1: /* LLDP-MED Capabilities: caps(2) | device_type(1) */
        if (vlen >= 3) {
            out->has_med_caps = true;
            out->med_caps = rd16(v);
            out->med_device_type = v[2];
            return;
        }
        break;
    case 2: { /* Network Policy: app(1) | U T X vid(12) pri(3) dscp(6) */
        if (vlen < 4) break;
        if (out->n_med_policies >= LLDP_MAX_MED_POLICIES) { out->truncated = true; return; }
        lldp_med_policy_t *pol = &out->med_policies[out->n_med_policies++];
        uint32_t bits = ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
        pol->app_type = v[0];
        pol->unknown_policy = (bits >> 23) & 1;
        pol->tagged = (bits >> 22) & 1;
        pol->vlan_id = (bits >> 9) & 0x0FFF;
        pol->l2_priority = (bits >> 6) & 0x07;
        pol->dscp = bits & 0x3F;
        return;
    }
    case 5: if (copy_str(out->inv_hw, LLDP_MAX_INV, v, vlen)) out->truncated = true; return;           /* Hardware Revision */
    case 6: if (copy_str(out->inv_fw, LLDP_MAX_INV, v, vlen)) out->truncated = true; return;           /* Firmware Revision */
    case 7: if (copy_str(out->inv_sw, LLDP_MAX_INV, v, vlen)) out->truncated = true; return;           /* Software Revision */
    case 8: if (copy_str(out->inv_serial, LLDP_MAX_INV, v, vlen)) out->truncated = true; return;       /* Serial Number */
    case 9: if (copy_str(out->inv_manufacturer, LLDP_MAX_INV, v, vlen)) out->truncated = true; return; /* Manufacturer Name */
    case 10: if (copy_str(out->inv_model, LLDP_MAX_INV, v, vlen)) out->truncated = true; return;       /* Model Name */
    case 11: if (copy_str(out->inv_asset, LLDP_MAX_INV, v, vlen)) out->truncated = true; return;       /* Asset ID */
    case 4: /* Extended Power via MDI: type(2b) source(2b) prio(4b) | value(2) */
        if (vlen >= 3) {
            out->med_power.present = true;
            out->med_power.power_type = (v[0] >> 6) & 0x03;
            out->med_power.power_source = (v[0] >> 4) & 0x03;
            out->med_power.power_priority = v[0] & 0x0F;
            out->med_power.power_dw = rd16(&v[1]);
            return;
        }
        break;
    default:
        break;
    }
    record_unknown(out, LLDP_TLV_ORG, true, OUI_MED, subtype, v, vlen);
}

static void decode_org(lldp_neighbor_t *out, const uint8_t *v, size_t vlen)
{
    if (vlen < 4) {
        record_unknown(out, LLDP_TLV_ORG, false, NULL, 0, v, vlen);
        return;
    }
    const uint8_t *oui = v;
    uint8_t subtype = v[3];
    const uint8_t *body = v + 4;
    size_t blen = vlen - 4;

    if (memcmp(oui, OUI_8021, 3) == 0)      decode_8021(out, subtype, body, blen);
    else if (memcmp(oui, OUI_8023, 3) == 0) decode_8023(out, subtype, body, blen);
    else if (memcmp(oui, OUI_MED, 3) == 0)  decode_med(out, subtype, body, blen);
    else if (memcmp(oui, OUI_FORTINET, 3) == 0 && subtype == 2 && blen) {   /* FortiSwitch serial number */
        if (copy_str(out->inv_serial, LLDP_MAX_INV, body, blen)) out->truncated = true;
    }
    else record_unknown(out, LLDP_TLV_ORG, true, oui, subtype, body, blen);
}

/* ---- main entry point ------------------------------------------------- */

lldp_result_t lldp_parse(const uint8_t *frame, size_t len, lldp_neighbor_t *out)
{
    if (!frame || !out) return LLDP_ERR_NULL;
    memset(out, 0, sizeof *out);
    out->power_mdi.power_class = 0xFF;
    out->proto = NEIGHBOR_PROTO_LLDP;
    out->checksum_ok = true;

    if (len < 14) return LLDP_ERR_TOO_SHORT;
    memcpy(out->dst_mac, frame, 6);
    memcpy(out->src_mac, frame + 6, 6);

    size_t pos = 12;
    uint16_t ethertype = rd16(frame + pos);
    pos += 2;
    if (ethertype == LLDP_VLAN_ETHERTYPE) {
        if (len < pos + 4) return LLDP_ERR_TOO_SHORT;
        out->vlan_tagged = true;
        out->tag_vid = rd16(frame + pos) & 0x0FFF;
        ethertype = rd16(frame + pos + 2);
        pos += 4;
    }
    if (ethertype != LLDP_ETHERTYPE) return LLDP_ERR_NOT_LLDP;

    unsigned idx = 0; /* TLV index, for mandatory-order check */
    bool have_chassis = false, have_port = false, have_ttl = false;

    while (pos + 2 <= len) {
        uint16_t hdr = rd16(frame + pos);
        uint8_t type = (uint8_t)(hdr >> 9);
        size_t vlen = hdr & 0x01FF;
        pos += 2;

        if (type == LLDP_TLV_END) {
            out->saw_end = true;
            break; /* anything after End is padding */
        }
        if (pos + vlen > len) return LLDP_ERR_TRUNCATED;
        const uint8_t *v = frame + pos;
        pos += vlen;
        out->n_tlvs++;

        /* IEEE 802.1AB-2016 8.5: chassis, port, TTL must be the first three. */
        if (idx == 0 && type != LLDP_TLV_CHASSIS_ID) return LLDP_ERR_MANDATORY_ORDER;
        if (idx == 1 && type != LLDP_TLV_PORT_ID)    return LLDP_ERR_MANDATORY_ORDER;
        if (idx == 2 && type != LLDP_TLV_TTL)        return LLDP_ERR_MANDATORY_ORDER;
        idx++;

        switch (type) {
        case LLDP_TLV_CHASSIS_ID:
            if (vlen < 2 || have_chassis) return LLDP_ERR_BAD_TLV;
            have_chassis = true;
            if (copy_id(&out->chassis_id, v[0], v + 1, vlen - 1)) out->truncated = true;
            break;
        case LLDP_TLV_PORT_ID:
            if (vlen < 2 || have_port) return LLDP_ERR_BAD_TLV;
            have_port = true;
            if (copy_id(&out->port_id, v[0], v + 1, vlen - 1)) out->truncated = true;
            break;
        case LLDP_TLV_TTL:
            if (vlen < 2 || have_ttl) return LLDP_ERR_BAD_TLV;
            have_ttl = true;
            out->ttl = rd16(v);
            break;
        case LLDP_TLV_PORT_DESC:
            out->has_port_desc = true;
            if (copy_str(out->port_desc, LLDP_MAX_PORT_DESC, v, vlen)) out->truncated = true;
            break;
        case LLDP_TLV_SYS_NAME:
            out->has_sys_name = true;
            if (copy_str(out->sys_name, LLDP_MAX_SYS_NAME, v, vlen)) out->truncated = true;
            break;
        case LLDP_TLV_SYS_DESC:
            out->has_sys_desc = true;
            if (copy_str(out->sys_desc, LLDP_MAX_SYS_DESC, v, vlen)) out->truncated = true;
            break;
        case LLDP_TLV_SYS_CAPS:
            if (vlen >= 4) {
                out->has_caps = true;
                out->caps_supported = rd16(v);
                out->caps_enabled = rd16(v + 2);
            } else {
                record_unknown(out, type, false, NULL, 0, v, vlen);
            }
            break;
        case LLDP_TLV_MGMT_ADDR:
            decode_mgmt_addr(out, v, vlen);
            break;
        case LLDP_TLV_ORG:
            decode_org(out, v, vlen);
            break;
        default:
            record_unknown(out, type, false, NULL, 0, v, vlen);
            break;
        }
    }

    if (!have_chassis || !have_port || !have_ttl) return LLDP_ERR_MANDATORY_ORDER;
    return LLDP_OK;
}

/* ---- formatting ------------------------------------------------------- */

const char *lldp_result_str(lldp_result_t r)
{
    switch (r) {
    case LLDP_OK:                  return "ok";
    case LLDP_ERR_TOO_SHORT:       return "frame too short";
    case LLDP_ERR_NOT_LLDP:        return "not an LLDP frame";
    case LLDP_ERR_TRUNCATED:       return "TLV runs past end of frame";
    case LLDP_ERR_MANDATORY_ORDER: return "mandatory TLVs missing or out of order";
    case LLDP_ERR_BAD_TLV:         return "malformed mandatory TLV";
    case LLDP_ERR_NULL:            return "null argument";
    }
    return "unknown";
}

/* Bounded append helpers with snprintf-like "would have written" semantics. */
typedef struct { char *buf; size_t cap; size_t pos; } sb_t;

static void sb_putc(sb_t *s, char c)
{
    if (s->pos + 1 < s->cap) s->buf[s->pos] = c;
    s->pos++;
}
static void sb_puts(sb_t *s, const char *str)
{
    while (*str) sb_putc(s, *str++);
}
static void sb_hex2(sb_t *s, uint8_t b)
{
    static const char hx[] = "0123456789abcdef";
    sb_putc(s, hx[b >> 4]);
    sb_putc(s, hx[b & 0x0F]);
}
static void sb_uint(sb_t *s, uint32_t v)
{
    char tmp[11];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) sb_putc(s, tmp[--n]);
}
static void sb_hex4(sb_t *s, uint16_t v)
{
    static const char hx[] = "0123456789abcdef";
    bool started = false;
    for (int shift = 12; shift >= 0; shift -= 4) {
        unsigned nib = (v >> shift) & 0xF;
        if (nib || started || shift == 0) { sb_putc(s, hx[nib]); started = true; }
    }
}
static void sb_finish(sb_t *s)
{
    if (s->cap == 0) return;
    size_t end = s->pos < s->cap ? s->pos : s->cap - 1;
    s->buf[end] = '\0';
}

static void sb_mac(sb_t *s, const uint8_t *m)
{
    for (int i = 0; i < 6; i++) {
        if (i) sb_putc(s, ':');
        sb_hex2(s, m[i]);
    }
}

static void sb_ipv6(sb_t *s, const uint8_t *a)
{
    /* RFC 5952: compress the longest run of >= 2 zero groups. */
    uint16_t g[8];
    for (int i = 0; i < 8; i++) g[i] = rd16(a + 2 * i);
    int best_start = -1, best_len = 0;
    for (int i = 0; i < 8;) {
        if (g[i] != 0) { i++; continue; }
        int j = i;
        while (j < 8 && g[j] == 0) j++;
        if (j - i > best_len && j - i >= 2) { best_start = i; best_len = j - i; }
        i = j;
    }
    for (int i = 0; i < 8; i++) {
        if (i == best_start) {
            sb_puts(s, "::");
            i += best_len - 1; /* loop i++ moves past the zero run */
            continue;
        }
        /* separator unless first group or immediately after "::" */
        if (i > 0 && !(best_start >= 0 && i == best_start + best_len)) sb_putc(s, ':');
        sb_hex4(s, g[i]);
    }
}

static void sb_addr(sb_t *s, uint8_t afi, const uint8_t *addr, uint8_t len)
{
    if (afi == LLDP_AFI_IPV4 && len == 4) {
        for (int i = 0; i < 4; i++) {
            if (i) sb_putc(s, '.');
            sb_uint(s, addr[i]);
        }
    } else if (afi == LLDP_AFI_IPV6 && len == 16) {
        sb_ipv6(s, addr);
    } else if (afi == LLDP_AFI_802 && len == 6) {
        sb_mac(s, addr);
    } else {
        sb_puts(s, "afi");
        sb_uint(s, afi);
        sb_putc(s, ':');
        for (uint8_t i = 0; i < len; i++) sb_hex2(s, addr[i]);
    }
}

size_t lldp_format_addr(uint8_t afi, const uint8_t *addr, uint8_t len,
                        char *buf, size_t buflen)
{
    sb_t s = {buf, buflen, 0};
    sb_addr(&s, afi, addr, len);
    sb_finish(&s);
    return s.pos;
}

/* Emit a string ID; fall back to hex if any byte is not printable ASCII. */
static void sb_text_or_hex(sb_t *s, const uint8_t *d, uint8_t n)
{
    bool printable = n > 0;
    for (uint8_t i = 0; i < n; i++) {
        if (d[i] < 0x20 || d[i] > 0x7E) { printable = false; break; }
    }
    if (printable) {
        for (uint8_t i = 0; i < n; i++) sb_putc(s, (char)d[i]);
    } else {
        sb_puts(s, "0x");
        for (uint8_t i = 0; i < n; i++) sb_hex2(s, d[i]);
    }
}

static void sb_netaddr_id(sb_t *s, const uint8_t *d, uint8_t n)
{
    /* network address subtype: afi(1) | addr */
    if (n < 1) { sb_puts(s, "0x"); return; }
    sb_addr(s, d[0], d + 1, (uint8_t)(n - 1));
}

static size_t format_id(const lldp_id_t *id, bool chassis, char *buf, size_t buflen)
{
    sb_t s = {buf, buflen, 0};
    enum { TEXT, MAC, NET, HEX } kind = HEX;
    const char *prefix = "unknown";

    if (chassis) {
        switch (id->subtype) {
        case LLDP_CHASSIS_COMPONENT: prefix = "chassis"; kind = TEXT; break;
        case LLDP_CHASSIS_IFALIAS:   prefix = "ifalias"; kind = TEXT; break;
        case LLDP_CHASSIS_PORTCOMP:  prefix = "portcomp"; kind = TEXT; break;
        case LLDP_CHASSIS_MAC:       prefix = "mac"; kind = MAC; break;
        case LLDP_CHASSIS_NETADDR:   prefix = "net"; kind = NET; break;
        case LLDP_CHASSIS_IFNAME:    prefix = "ifname"; kind = TEXT; break;
        case LLDP_CHASSIS_LOCAL:     prefix = "local"; kind = TEXT; break;
        default: break;
        }
    } else {
        switch (id->subtype) {
        case LLDP_PORT_IFALIAS:  prefix = "ifalias"; kind = TEXT; break;
        case LLDP_PORT_PORTCOMP: prefix = "portcomp"; kind = TEXT; break;
        case LLDP_PORT_MAC:      prefix = "mac"; kind = MAC; break;
        case LLDP_PORT_NETADDR:  prefix = "net"; kind = NET; break;
        case LLDP_PORT_IFNAME:   prefix = "ifname"; kind = TEXT; break;
        case LLDP_PORT_CIRCUIT:  prefix = "circuit"; kind = HEX; break;
        case LLDP_PORT_LOCAL:    prefix = "local"; kind = TEXT; break;
        default: break;
        }
    }
    sb_puts(&s, prefix);
    sb_putc(&s, ':');
    switch (kind) {
    case TEXT: sb_text_or_hex(&s, id->data, id->len); break;
    case MAC:
        if (id->len == 6) sb_mac(&s, id->data);
        else { sb_puts(&s, "0x"); for (uint8_t i = 0; i < id->len; i++) sb_hex2(&s, id->data[i]); }
        break;
    case NET: sb_netaddr_id(&s, id->data, id->len); break;
    case HEX:
        sb_puts(&s, "0x");
        for (uint8_t i = 0; i < id->len; i++) sb_hex2(&s, id->data[i]);
        break;
    }
    sb_finish(&s);
    return s.pos;
}

size_t lldp_format_chassis_id(const lldp_id_t *id, char *buf, size_t buflen)
{
    return format_id(id, true, buf, buflen);
}

size_t lldp_format_port_id(const lldp_id_t *id, char *buf, size_t buflen)
{
    return format_id(id, false, buf, buflen);
}

const char *lldp_cap_name(unsigned bit)
{
    static const char *const names[] = {
        "other", "repeater", "bridge", "wlan_ap", "router", "telephone",
        "docsis", "station", "cvlan", "svlan", "tpmr",
    };
    return bit < sizeof names / sizeof names[0] ? names[bit] : "unknown";
}

const char *lldp_med_app_name(uint8_t app_type)
{
    static const char *const names[] = {
        "reserved", "voice", "voice_signaling", "guest_voice",
        "guest_voice_signaling", "softphone_voice", "video_conferencing",
        "streaming_video", "video_signaling",
    };
    return app_type < sizeof names / sizeof names[0] ? names[app_type] : "unknown";
}

const lldp_med_policy_t *lldp_find_med_policy(const lldp_neighbor_t *n, uint8_t app_type)
{
    for (uint8_t i = 0; i < n->n_med_policies; i++) {
        if (n->med_policies[i].app_type == app_type) return &n->med_policies[i];
    }
    return NULL;
}
