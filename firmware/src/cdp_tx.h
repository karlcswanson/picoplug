/*
 * cdp_tx.h - build the CDPv2 frame this device advertises.
 *
 * Pure function (host-testable); advertise.c schedules it.
 */
#ifndef CDP_TX_H
#define CDP_TX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *mac;          /* source MAC                               */
    const char *device_id;       /* Device ID (hostname)                     */
    const char *port_id;         /* Port ID                                  */
    const char *sw_version;      /* Software Version, NULL to omit           */
    const char *platform;        /* Platform, NULL to omit                   */
    uint8_t  ttl_s;              /* holdtime, 0 = leaving                    */
    bool     has_ip;
    uint8_t  ip[4];
    bool     full_duplex;
} cdp_tx_cfg_t;

#define CDP_TX_MAX_FRAME 512

size_t cdp_tx_build(uint8_t *buf, size_t cap, const cdp_tx_cfg_t *cfg);

#endif /* CDP_TX_H */
