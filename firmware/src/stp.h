/*
 * stp.h - Spanning Tree BPDU decoder (802.1D STP, 802.1w RSTP, the CIST
 * part of 802.1s MSTP, and Cisco PVST+), plus a small tracker that
 * remembers what the port has heard.
 *
 * Pure code, host-tested. The point for a port tester: which bridge is
 * root, whether the switch port is designated/forwarding, the timers,
 * topology changes, and whether BPDUs arrive at all (an edge port with
 * BPDU filter, or STP off, is silent).
 */
#ifndef STP_H
#define STP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { STP_VERSION_STP = 0, STP_VERSION_RSTP = 2, STP_VERSION_MSTP = 3 };
enum { STP_TYPE_CONFIG = 0x00, STP_TYPE_RST = 0x02, STP_TYPE_TCN = 0x80 };
enum { STP_FLAG_TC = 0x01, STP_FLAG_PROPOSAL = 0x02, STP_FLAG_LEARNING = 0x10,
       STP_FLAG_FORWARDING = 0x20, STP_FLAG_AGREEMENT = 0x40, STP_FLAG_TC_ACK = 0x80 };
enum { STP_ROLE_UNKNOWN = 0, STP_ROLE_ALTERNATE = 1, STP_ROLE_ROOT = 2, STP_ROLE_DESIGNATED = 3 };

typedef struct {
    uint8_t  version;            /* STP_VERSION_*                         */
    uint8_t  type;               /* STP_TYPE_*                            */
    bool     pvst;               /* Cisco PVST+ (SNAP 00-00-0c/01-0b)      */
    bool     tagged;             /* frame carried an 802.1Q tag            */
    uint16_t vlan;               /* PVST+ VLAN (tag or trailer), 0 = none  */
    uint8_t  src_mac[6];
    /* Config / RST fields (not present for TCN) */
    uint8_t  flags;              /* STP_FLAG_*; role = (flags >> 2) & 3    */
    uint16_t root_priority;      /* full 16-bit field: priority | ext sys id */
    uint8_t  root_mac[6];
    uint32_t root_cost;
    uint16_t bridge_priority;
    uint8_t  bridge_mac[6];
    uint16_t port_id;            /* priority(4) | number(12)               */
    uint16_t msg_age_256;        /* times in 1/256 s                       */
    uint16_t max_age_256;
    uint16_t hello_256;
    uint16_t fwd_delay_256;
    /* MSTP (version 3) CIST section. In an MST BPDU the body's bridge ID is
     * the CIST regional root; the sender's own ID is here, and bridge_* above
     * is set to it so callers see the neighbor in both cases. */
    bool     mst;
    char     mst_region[33];
    uint16_t mst_revision;
    uint16_t regional_root_priority;
    uint8_t  regional_root_mac[6];
    uint32_t cist_internal_cost;
    uint8_t  cist_hops;
    uint8_t  msti_count;
} stp_bpdu_t;

bool stp_is_bpdu(const uint8_t *frame, size_t len);
/* Returns false if the frame is not a well-formed BPDU. */
bool stp_parse(const uint8_t *frame, size_t len, stp_bpdu_t *out);

const char *stp_version_name(uint8_t v);          /* "stp" "rstp" "mstp" */
const char *stp_type_name(uint8_t t);             /* "config" "rst" "tcn" */
const char *stp_role_name(uint8_t flags);         /* "designated" ...    */
const char *stp_state_name(uint8_t flags);        /* "forwarding" "learning" "discarding" */

/* ---- tracker ------------------------------------------------------------ */
typedef struct {
    bool       seen;             /* any BPDU since reset                   */
    stp_bpdu_t last;
    uint32_t   count;
    uint32_t   tc_count;         /* BPDUs with the TC flag or TCN BPDUs    */
    uint32_t   root_changes;     /* root bridge ID changed between BPDUs   */
    uint32_t   first_ms, last_ms;
    uint32_t   interval_ms;      /* between the last two BPDUs             */
    uint32_t   generation;       /* bumps when something worth pushing changed */
} stp_state_t;

void stp_state_reset(stp_state_t *s);
void stp_state_update(stp_state_t *s, const stp_bpdu_t *b, uint32_t now_ms);

#endif /* STP_H */
