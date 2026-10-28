/*
 * lacp.h - passive LACP (IEEE 802.1AX) detection.
 *
 * A switch port that is a member of an LACP port channel sends LACPDUs
 * (slow protocols, EtherType 0x8809 subtype 1, to 01:80:c2:00:00:02) every
 * second (fast) or every 30 s (slow). We never answer, so the switch sees
 * no partner: most switches then keep the port out of the bundle and pass
 * only link-local frames (LLDP, LACP itself), some run it as an individual
 * port. The actor state bits the switch sends say which. Listen only.
 */
#ifndef LACP_H
#define LACP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LACP_ETHERTYPE 0x8809

/* actor/partner state bits */
enum {
    LACP_ST_ACTIVE = 0x01, LACP_ST_FAST = 0x02, LACP_ST_AGGREGATABLE = 0x04, LACP_ST_SYNC = 0x08,
    LACP_ST_COLLECTING = 0x10, LACP_ST_DISTRIBUTING = 0x20, LACP_ST_DEFAULTED = 0x40, LACP_ST_EXPIRED = 0x80,
};

typedef struct {
    uint16_t sys_priority;
    uint8_t  sys_mac[6];
    uint16_t key;
    uint16_t port_priority;
    uint16_t port;
    uint8_t  state;
} lacp_info_t;

typedef struct {
    uint8_t     src_mac[6];
    uint8_t     version;
    lacp_info_t actor;           /* the switch port                       */
    lacp_info_t partner;         /* what the switch knows of us (zeros)   */
} lacp_pdu_t;

/* Slow protocols EtherType with LACP subtype, plain or behind one 802.1Q tag. */
bool lacp_is_lacp(const uint8_t *frame, size_t len);
/* Returns false if not LACP or truncated. */
bool lacp_parse(const uint8_t *frame, size_t len, lacp_pdu_t *out);

/* What the port has sent since link-up. */
typedef struct {
    bool        seen;
    uint32_t    count;
    uint32_t    first_ms, last_ms;
    uint32_t    interval_ms;     /* between the last two LACPDUs          */
    uint32_t    changes;         /* actor or partner info changed after the first */
    uint8_t     src_mac[6];
    uint8_t     version;
    lacp_info_t actor, partner;
    uint32_t    generation;      /* bumps when the content changes        */
} lacp_state_t;

void lacp_state_reset(lacp_state_t *s);
void lacp_state_update(lacp_state_t *s, const lacp_pdu_t *p, uint32_t now_ms);

#endif /* LACP_H */
