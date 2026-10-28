/*
 * mcast.h - passive multicast-plant detection: IGMP querier and PTP.
 *
 * AV-over-IP networks (Dante, AES67, SMPTE 2110) live or die on two
 * things a port can show without any configuration: is there an IGMP
 * querier on the VLAN, and is a PTP grandmaster visible. Both are read
 * from frames the port receives; eth_net.c joins 224.0.1.129 so a
 * snooping switch forwards PTP to us. Pure code, host-tested; the
 * tracker instances live in main.c (app.h).
 */
#ifndef MCAST_H
#define MCAST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- IGMP ---------------------------------------------------------------- */

typedef struct {
    uint8_t  src_mac[6];
    uint8_t  src_ip[4];
    uint8_t  type;               /* 0x11 query, 0x12/0x16/0x22 reports, 0x17 leave */
    uint8_t  version;            /* for queries: 1, 2 or 3                     */
    uint8_t  group[4];           /* 0.0.0.0 = general query                    */
    uint16_t max_resp_ds;        /* max response time in 1/10 s (v2/v3)        */
    uint16_t qqi_s;              /* v3 querier's query interval, 0 if absent   */
    uint8_t  qrv;                /* v3 querier's robustness variable           */
    bool     s_flag;             /* v3 suppress router-side processing         */
} igmp_msg_t;

/* PIM Hello (IP protocol 103 to 224.0.0.13): a multicast router on the VLAN. */
typedef struct {
    uint8_t  src_mac[6];
    uint8_t  src_ip[4];
    uint8_t  type;               /* 0 = hello                                  */
    uint16_t holdtime_s;
    uint32_t dr_priority;
    bool     has_dr_priority;
} pim_msg_t;
bool pim_parse(const uint8_t *frame, size_t len, pim_msg_t *out);

/* IPv4 protocol 2 (router-alert option tolerated), plain or behind one 802.1Q tag. */
bool igmp_parse(const uint8_t *frame, size_t len, igmp_msg_t *out);

typedef struct {
    bool     seen;               /* a general query since reset                */
    uint32_t queries;            /* general queries                            */
    uint32_t group_queries;      /* group-specific queries                     */
    uint32_t reports;            /* membership reports from anyone             */
    uint32_t querier_changes;    /* general query from a different source IP   */
    uint8_t  querier_ip[4];
    uint8_t  querier_mac[6];
    uint8_t  prev_querier_ip[4]; /* before the last change                     */
    uint8_t  version;
    uint16_t qqi_s;              /* from a v3 query, else 0                    */
    uint8_t  qrv;
    bool     s_flag;
    uint16_t max_resp_ds;
    /* multicast router (PIM hello) */
    bool     pim_seen;
    uint32_t pim_hellos;
    uint8_t  pim_ip[4], pim_mac[6];
    uint16_t pim_holdtime_s;
    uint32_t pim_dr_priority;
    uint32_t pim_last_ms;
    uint32_t first_ms, last_ms;
    uint32_t interval_ms;        /* between the last two general queries       */
    uint32_t generation;
} igmp_state_t;

void igmp_state_reset(igmp_state_t *s);
void igmp_state_update(igmp_state_t *s, const igmp_msg_t *m, uint32_t now_ms);
void igmp_state_pim(igmp_state_t *s, const pim_msg_t *m, uint32_t now_ms);

/* ---- PTP ----------------------------------------------------------------- */

enum { PTP_MSG_SYNC = 0, PTP_MSG_DELAY_REQ = 1, PTP_MSG_PDELAY_REQ = 2, PTP_MSG_PDELAY_RESP = 3,
       PTP_MSG_FOLLOW_UP = 8, PTP_MSG_DELAY_RESP = 9, PTP_MSG_PDELAY_RESP_FUP = 10,
       PTP_MSG_ANNOUNCE = 11, PTP_MSG_SIGNALING = 12, PTP_MSG_MANAGEMENT = 13 };

typedef struct {
    uint8_t  src_mac[6];
    uint8_t  src_ip[4];          /* zero for layer-2 PTP                       */
    bool     l2;                 /* EtherType 0x88F7 rather than UDP 319/320   */
    uint8_t  version;            /* 1 or 2                                     */
    uint8_t  msg_type;           /* v2: PTP_MSG_*; v1: 0 sync, 1 delay_req, 2 follow_up, 3 delay_resp, 4 management */
    uint8_t  domain;             /* v2 domainNumber; v1: 0                     */
    uint16_t flags;              /* v2 flagField                               */
    uint8_t  clock_id[8];        /* v2 sourcePortIdentity clock; v1 sourceUuid in [0..5] */
    uint16_t port_number;
    uint16_t sequence;
    int8_t   log_interval;       /* v2 logMessageInterval                      */
    /* v2 Announce */
    bool     announce;
    uint8_t  gm_id[8];
    uint8_t  priority1, priority2, clock_class, clock_accuracy, time_source;
    uint16_t steps_removed;
    int16_t  utc_offset;
    /* v1 */
    char     subdomain[17];      /* v1 subdomain name, e.g. "_DFLT"            */
} ptp_msg_t;

/* UDP 319/320 (v1 or v2) or EtherType 0x88F7 (v2), plain or behind one 802.1Q tag. */
bool ptp_parse(const uint8_t *frame, size_t len, ptp_msg_t *out);
const char *ptp_msg_name(uint8_t version, uint8_t t);
const char *ptp_time_source_name(uint8_t s);   /* "gps" "ptp" "ntp" "handset" "other" "oscillator" "atomic" "terrestrial_radio" "serial_time_code" "unknown" */

/* One tracker per PTP version: Dante sites run PTPv1 for Dante clocking and PTPv2 for
 * AES67 / Dante Domain Manager side by side, with different masters. */
typedef struct {
    uint8_t  version;            /* fixed at reset: 1 or 2                     */
    bool     seen;
    bool     l2;
    uint32_t announces, syncs, follow_ups, delay_reqs, other;
    uint32_t other_domains;      /* messages whose domain differed from `domain` */
    uint8_t  domain;
    uint8_t  master_id[8];       /* clock identity sending Sync (v1: uuid in [0..5]) */
    uint8_t  master_ip[4];
    uint8_t  master_mac[6];
    bool     two_step;
    int8_t   log_sync_interval;  /* from Sync                                  */
    int8_t   log_announce_interval;
    uint32_t sync_interval_ms;   /* measured between the last two Syncs        */
    uint32_t first_ms, last_sync_ms, last_announce_ms;
    /* from Announce (v2) */
    bool     have_gm;
    uint8_t  gm_id[8];
    uint8_t  priority1, priority2, clock_class, clock_accuracy, time_source;
    uint16_t steps_removed;
    int16_t  utc_offset;
    uint32_t gm_changes;         /* grandmaster identity changed between Announces */
    char     subdomain[17];      /* v1 */
    uint32_t generation;         /* bumps on first sight, Announce changes, master change */
} ptp_state_t;

void ptp_state_reset(ptp_state_t *s, uint8_t version);
/* Messages of another version are ignored. */
void ptp_state_update(ptp_state_t *s, const ptp_msg_t *m, uint32_t now_ms);

#endif /* MCAST_H */
