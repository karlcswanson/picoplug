/*
 * advertise.h - send our own LLDP and CDP frames on the Ethernet port.
 *
 * Purpose: make the switch treat the tester as a real endpoint. Switches
 * list us in their neighbor table, and most only send LLDP-MED TLVs
 * (voice VLAN policy, power) once they have heard a MED endpoint.
 *
 * Timing: a burst on link-up and when a protocol is (re)enabled (LLDP-MED
 * fast start: 1 s apart), then LLDP every ADV_LLDP_INTERVAL_S and CDP
 * every ADV_CDP_INTERVAL_S. Nothing is sent while the link is down.
 */
#ifndef ADVERTISE_H
#define ADVERTISE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     lldp_enabled, cdp_enabled;
    uint32_t lldp_frames, cdp_frames;      /* sent since boot */
    uint32_t lldp_last_ms, cdp_last_ms;    /* 0 = never */
} advertise_state_t;

void advertise_init(const uint8_t mac[6]);
void advertise_poll(void);

/* Disabling a protocol sends one shutdown frame (TTL 0) first. */
void advertise_set_enabled(bool lldp, bool cdp);
/* The advertised name changed: send a fresh burst so the switch picks it up. */
void advertise_name_changed(void);
void advertise_get_state(advertise_state_t *out);

/* Feeds the fingerprint used by the status stream. */
uint32_t advertise_generation(void);

#endif /* ADVERTISE_H */
