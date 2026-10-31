/*
 * lldp_tx.h - build the LLDPDU this device advertises on its Ethernet port.
 *
 * Pure function, no hardware dependencies, so the host tests can round-trip
 * the frame through lldp_parse(). The scheduler that decides when to send
 * lives in advertise.c.
 *
 * The frame identifies the tester as an LLDP-MED endpoint so that switches
 * reply with their MED TLVs (network policy, extended power).
 */
#ifndef LLDP_TX_H
#define LLDP_TX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *mac;          /* source MAC and Chassis ID (6 bytes)      */
    const char *port_id;         /* Port ID, interface-name subtype          */
    const char *port_desc;       /* Port Description, NULL to omit           */
    const char *sys_name;        /* System Name                              */
    const char *sys_desc;        /* System Description, NULL to omit         */
    uint16_t ttl_s;              /* Time To Live; 0 = shutdown LLDPDU        */
    bool     has_ip;             /* send a Management Address TLV (IPv4)     */
    uint8_t  ip[4];
    uint16_t speed;              /* 10 or 100, 0 = omit MAC/PHY TLV          */
    bool     full;
    uint8_t  med_device_type;    /* LLDP-MED class (1 generic .. 3 comm), 0 = no MED TLV */
} lldp_tx_cfg_t;

/* Largest frame lldp_tx_build() can produce. */
#define LLDP_TX_MAX_FRAME 640

/* Writes a complete Ethernet frame (no FCS) into buf. Returns its length,
 * or 0 if cfg is invalid or buf is too small. */
size_t lldp_tx_build(uint8_t *buf, size_t cap, const lldp_tx_cfg_t *cfg);

#endif /* LLDP_TX_H */
