/*
 * app.h - things main.c owns that other modules read.
 */
#ifndef APP_H
#define APP_H

#include <stddef.h>
#include <stdint.h>
#include "stp.h"
#include "eapol.h"
#include "mcast.h"
#include "lacp.h"

/* main() is app_init() once, then app_loop_once() forever. They are separate so that a build which cannot
 * own the loop can drive the same code from a timer. */
void app_init(void);
void app_loop_once(void);

/* Every received Ethernet frame (W5500 poll or POST /inject). */
void app_handle_frame(const uint8_t *frame, size_t len);

/* The last raw frame of each kind, for GET /frame: kind 0 LLDP, 1 CDP, 2 BPDU, 3 LACP. Returns the length, 0 if none. */
size_t app_last_frame(int kind, const uint8_t **frame);

/* Spanning-tree BPDUs heard on the port since the last link-up. */
const stp_state_t *app_stp_state(void);
/* 802.1X (EAPOL) frames the port has sent since the last link-up. */
const eapol_state_t *app_dot1x_state(void);
/* LACPDUs the port has sent since the last link-up (LAG member). */
const lacp_state_t *app_lacp_state(void);
const uint8_t *app_eth_mac(void);       /* our Ethernet MAC, 6 bytes */
/* IGMP querier and PTP master seen on the port since the last link-up. */
const igmp_state_t *app_igmp_state(void);
const ptp_state_t *app_ptp_state(uint8_t version);   /* 1 or 2 */
/* Milliseconds since the PHY link last came up; 0 when the link is down. */
uint32_t app_link_up_for_ms(void);

#endif /* APP_H */
