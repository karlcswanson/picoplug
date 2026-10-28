/*
 * mcast.c - see mcast.h.
 */
#include "mcast.h"

#include <string.h>

#define PTP_ETHERTYPE 0x88F7

/* Ethernet: EtherType offset (12 or 16 with one 802.1Q tag), 0 if too short. */
static size_t ethertype_at(const uint8_t *f, size_t len)
{
    if (!f || len < 14) return 0;
    size_t o = 12;
    if (f[o] == 0x81 && f[o + 1] == 0x00) { o += 4; if (len < o + 2) return 0; }
    return o;
}

/* IPv4 payload: returns offset of the L4 header, fills proto/src/dst; 0 if not IPv4 or truncated. */
static size_t ipv4_payload(const uint8_t *f, size_t len, uint8_t *proto, const uint8_t **src, const uint8_t **dst)
{
    size_t e = ethertype_at(f, len);
    if (!e || f[e] != 0x08 || f[e + 1] != 0x00) return 0;
    size_t ip = e + 2;
    if (len < ip + 20 || (f[ip] >> 4) != 4) return 0;
    size_t ihl = (size_t)(f[ip] & 0x0F) * 4;
    if (ihl < 20 || len < ip + ihl) return 0;
    size_t total = (size_t)(f[ip + 2] << 8 | f[ip + 3]);
    if (total < ihl || len < ip + total) return 0;
    *proto = f[ip + 9];
    *src = f + ip + 12;
    *dst = f + ip + 16;
    return ip + ihl;
}

/* ---- IGMP ---------------------------------------------------------------- */

bool igmp_parse(const uint8_t *frame, size_t len, igmp_msg_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    uint8_t proto; const uint8_t *src, *dst;
    size_t o = ipv4_payload(frame, len, &proto, &src, &dst);
    if (!o || proto != 2) return false;
    size_t ip_total = (size_t)(frame[ethertype_at(frame, len) + 2 + 2] << 8 | frame[ethertype_at(frame, len) + 2 + 3]);
    size_t igmp_len = ethertype_at(frame, len) + 2 + ip_total - o;
    if (igmp_len < 8) return false;
    memcpy(out->src_mac, frame + 6, 6);
    memcpy(out->src_ip, src, 4);
    out->type = frame[o];
    memcpy(out->group, frame + o + 4, 4);
    if (out->type == 0x11) {
        uint8_t mrc = frame[o + 1];
        if (igmp_len >= 12) {
            out->version = 3;
            out->max_resp_ds = mrc < 128 ? mrc : (uint16_t)(((mrc & 0x0F) | 0x10) << (((mrc >> 4) & 0x07) + 3));
            uint8_t qqic = frame[o + 9];
            out->qqi_s = qqic < 128 ? qqic : (uint16_t)(((qqic & 0x0F) | 0x10) << (((qqic >> 4) & 0x07) + 3));
            out->qrv = frame[o + 8] & 0x07;
            out->s_flag = (frame[o + 8] & 0x08) != 0;
        } else {
            out->version = mrc ? 2 : 1;
            out->max_resp_ds = mrc ? mrc : 100;
        }
    }
    return true;
}

bool pim_parse(const uint8_t *frame, size_t len, pim_msg_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    uint8_t proto; const uint8_t *src, *dst;
    size_t o = ipv4_payload(frame, len, &proto, &src, &dst);
    if (!o || proto != 103 || len < o + 4) return false;
    if ((frame[o] >> 4) != 2) return false;                 /* PIMv2 */
    memcpy(out->src_mac, frame + 6, 6);
    memcpy(out->src_ip, src, 4);
    out->type = frame[o] & 0x0F;
    if (out->type != 0) return true;
    /* hello options: type(2) len(2) value */
    size_t p = o + 4;
    while (p + 4 <= len) {
        uint16_t t = (uint16_t)(frame[p] << 8 | frame[p + 1]), l = (uint16_t)(frame[p + 2] << 8 | frame[p + 3]);
        p += 4;
        if (p + l > len) break;
        if (t == 1 && l == 2) out->holdtime_s = (uint16_t)(frame[p] << 8 | frame[p + 1]);
        else if (t == 19 && l == 4) { out->dr_priority = (uint32_t)frame[p] << 24 | (uint32_t)frame[p + 1] << 16 | (uint32_t)frame[p + 2] << 8 | frame[p + 3]; out->has_dr_priority = true; }
        p += l;
    }
    return true;
}

void igmp_state_reset(igmp_state_t *s) { memset(s, 0, sizeof *s); }

void igmp_state_pim(igmp_state_t *s, const pim_msg_t *m, uint32_t now_ms)
{
    if (m->type != 0) return;
    bool changed = !s->pim_seen || memcmp(s->pim_ip, m->src_ip, 4) != 0 || s->pim_holdtime_s != m->holdtime_s || s->pim_dr_priority != m->dr_priority;
    s->pim_seen = true;
    s->pim_hellos++;
    s->pim_last_ms = now_ms;
    memcpy(s->pim_ip, m->src_ip, 4);
    memcpy(s->pim_mac, m->src_mac, 6);
    s->pim_holdtime_s = m->holdtime_s;
    s->pim_dr_priority = m->dr_priority;
    if (changed) s->generation++;
}

void igmp_state_update(igmp_state_t *s, const igmp_msg_t *m, uint32_t now_ms)
{
    if (m->type != 0x11) {
        if (m->type == 0x12 || m->type == 0x16 || m->type == 0x22) s->reports++;
        return;
    }
    static const uint8_t any[4] = {0, 0, 0, 0};
    if (memcmp(m->group, any, 4) != 0) { s->group_queries++; return; }
    if (s->seen && memcmp(s->querier_ip, m->src_ip, 4) != 0) { s->querier_changes++; memcpy(s->prev_querier_ip, s->querier_ip, 4); }
    if (!s->seen) { s->seen = true; s->first_ms = now_ms; }
    else s->interval_ms = now_ms - s->last_ms;
    s->queries++;
    s->last_ms = now_ms;
    memcpy(s->querier_ip, m->src_ip, 4);
    memcpy(s->querier_mac, m->src_mac, 6);
    s->version = m->version;
    s->qqi_s = m->qqi_s;
    s->qrv = m->qrv;
    s->s_flag = m->s_flag;
    s->max_resp_ds = m->max_resp_ds;
    s->generation++;
}

/* ---- PTP ----------------------------------------------------------------- */

static bool parse_ptp_body(const uint8_t *p, size_t n, ptp_msg_t *out)
{
    if (n < 2) return false;
    if (p[0] == 0x00 && p[1] == 0x01) {
        /* PTPv1 (IEEE 1588-2002), as used by Dante: versionPTP, versionNetwork, subdomain[16],
         * messageType, sourceCommunicationTechnology, sourceUuid[6], sourcePortId, sequenceId, control */
        if (n < 40) return false;
        out->version = 1;
        memcpy(out->subdomain, p + 4, 16); out->subdomain[16] = '\0';
        for (int i = 15; i >= 0 && (out->subdomain[i] == '\0' || out->subdomain[i] == ' '); i--) out->subdomain[i] = '\0';
        memcpy(out->clock_id, p + 22, 6);
        out->port_number = (uint16_t)(p[28] << 8 | p[29]);
        out->sequence = (uint16_t)(p[30] << 8 | p[31]);
        out->msg_type = p[32];                      /* control field: 0 sync 1 delay_req 2 follow_up 3 delay_resp 4 management */
        return true;
    }
    if ((p[1] & 0x0F) != 2) return false;
    if (n < 34) return false;
    out->version = 2;
    out->msg_type = p[0] & 0x0F;
    size_t mlen = (size_t)(p[2] << 8 | p[3]);
    if (mlen < 34 || mlen > n) return false;
    out->domain = p[4];
    out->flags = (uint16_t)(p[6] << 8 | p[7]);
    memcpy(out->clock_id, p + 20, 8);
    out->port_number = (uint16_t)(p[28] << 8 | p[29]);
    out->sequence = (uint16_t)(p[30] << 8 | p[31]);
    out->log_interval = (int8_t)p[33];
    if (out->msg_type == PTP_MSG_ANNOUNCE && mlen >= 64) {
        const uint8_t *a = p + 34;                  /* originTimestamp[10] currentUtcOffset[2] reserved gmPriority1 gmClockQuality[4] gmPriority2 gmIdentity[8] stepsRemoved[2] timeSource */
        out->announce = true;
        out->utc_offset = (int16_t)(a[10] << 8 | a[11]);
        out->priority1 = a[13];
        out->clock_class = a[14];
        out->clock_accuracy = a[15];
        out->priority2 = a[18];
        memcpy(out->gm_id, a + 19, 8);
        out->steps_removed = (uint16_t)(a[27] << 8 | a[28]);
        out->time_source = a[29];
    }
    return true;
}

bool ptp_parse(const uint8_t *frame, size_t len, ptp_msg_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    size_t e = ethertype_at(frame, len);
    if (!e) return false;
    memcpy(out->src_mac, frame + 6, 6);
    if (frame[e] == (PTP_ETHERTYPE >> 8) && frame[e + 1] == (PTP_ETHERTYPE & 0xFF)) {
        out->l2 = true;
        return parse_ptp_body(frame + e + 2, len - e - 2, out);
    }
    uint8_t proto; const uint8_t *src, *dst;
    size_t o = ipv4_payload(frame, len, &proto, &src, &dst);
    if (!o || proto != 17 || len < o + 8) return false;
    uint16_t dport = (uint16_t)(frame[o + 2] << 8 | frame[o + 3]);
    if (dport != 319 && dport != 320) return false;
    size_t ulen = (size_t)(frame[o + 4] << 8 | frame[o + 5]);
    if (ulen < 8 || len < o + ulen) return false;
    memcpy(out->src_ip, src, 4);
    return parse_ptp_body(frame + o + 8, ulen - 8, out);
}

const char *ptp_msg_name(uint8_t version, uint8_t t)
{
    if (version == 1) {
        switch (t) { case 0: return "sync"; case 1: return "delay_req"; case 2: return "follow_up"; case 3: return "delay_resp"; case 4: return "management"; default: return "unknown"; }
    }
    switch (t) {
    case PTP_MSG_SYNC: return "sync"; case PTP_MSG_DELAY_REQ: return "delay_req"; case PTP_MSG_PDELAY_REQ: return "pdelay_req";
    case PTP_MSG_PDELAY_RESP: return "pdelay_resp"; case PTP_MSG_FOLLOW_UP: return "follow_up"; case PTP_MSG_DELAY_RESP: return "delay_resp";
    case PTP_MSG_PDELAY_RESP_FUP: return "pdelay_resp_follow_up"; case PTP_MSG_ANNOUNCE: return "announce";
    case PTP_MSG_SIGNALING: return "signaling"; case PTP_MSG_MANAGEMENT: return "management"; default: return "unknown";
    }
}

const char *ptp_time_source_name(uint8_t s)
{
    switch (s) {
    case 0x10: return "atomic"; case 0x20: return "gps"; case 0x30: return "terrestrial_radio"; case 0x40: return "ptp";
    case 0x50: return "ntp"; case 0x60: return "handset"; case 0x90: return "other"; case 0xA0: return "oscillator";
    case 0x39: return "serial_time_code"; default: return "unknown";
    }
}

void ptp_state_reset(ptp_state_t *s, uint8_t version) { memset(s, 0, sizeof *s); s->version = version; }

void ptp_state_update(ptp_state_t *s, const ptp_msg_t *m, uint32_t now_ms)
{
    if (m->version != s->version) return;
    /* Only the master's side of the protocol says anything about the plant: Sync, Follow_Up, Announce.
     * Delay_Req comes from slaves (counted so a busy port is visible), everything else is noise. */
    bool from_master = (m->version == 2 && (m->msg_type == PTP_MSG_SYNC || m->msg_type == PTP_MSG_FOLLOW_UP || m->msg_type == PTP_MSG_ANNOUNCE))
                    || (m->version == 1 && (m->msg_type == 0 || m->msg_type == 2));
    bool delay_req = (m->version == 2 && m->msg_type == PTP_MSG_DELAY_REQ) || (m->version == 1 && m->msg_type == 1);
    if (delay_req) { s->delay_reqs++; return; }
    if (!from_master) { s->other++; return; }
    if (!s->seen) {
        s->seen = true; s->first_ms = now_ms; s->domain = m->domain; s->l2 = m->l2;
        strcpy(s->subdomain, m->subdomain);
        s->generation++;
    } else if (m->domain != s->domain) { s->other_domains++; return; }
    bool sync = (m->version == 2 && m->msg_type == PTP_MSG_SYNC) || (m->version == 1 && m->msg_type == 0);
    if (sync) {
        if (s->syncs) s->sync_interval_ms = now_ms - s->last_sync_ms;
        s->syncs++;
        s->last_sync_ms = now_ms;
        if (memcmp(s->master_id, m->clock_id, 8) != 0) { memcpy(s->master_id, m->clock_id, 8); s->generation++; }
        memcpy(s->master_ip, m->src_ip, 4);
        memcpy(s->master_mac, m->src_mac, 6);
        s->two_step = (m->flags & 0x0200) != 0;
        s->log_sync_interval = m->log_interval;
        s->l2 = m->l2;
    } else if (m->announce) {
        if (s->have_gm && memcmp(s->gm_id, m->gm_id, 8) != 0) { s->gm_changes++; s->generation++; }
        if (!s->have_gm || s->priority1 != m->priority1 || s->clock_class != m->clock_class || s->steps_removed != m->steps_removed
            || s->time_source != m->time_source || s->priority2 != m->priority2) s->generation++;
        s->have_gm = true;
        memcpy(s->gm_id, m->gm_id, 8);
        s->priority1 = m->priority1; s->priority2 = m->priority2;
        s->clock_class = m->clock_class; s->clock_accuracy = m->clock_accuracy;
        s->steps_removed = m->steps_removed; s->time_source = m->time_source; s->utc_offset = m->utc_offset;
        s->log_announce_interval = m->log_interval;
        s->announces++;
        s->last_announce_ms = now_ms;
    } else {
        s->follow_ups++;
    }
}
