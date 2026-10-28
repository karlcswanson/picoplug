/*
 * dhcpd.h - minimal single-lease DHCP server for the USB link.
 *
 * Offers exactly one address (USB_NET_CLIENT_IP) with subnet mask, server
 * identifier and lease time. Deliberately sends NO router (3) and NO DNS
 * (6) option so the host keeps using Wi-Fi/cellular for the internet.
 */
#ifndef DHCPD_H
#define DHCPD_H

#include <stdbool.h>
#include <stdint.h>

/* Bind UDP :67 on the default netif. Returns false if lwIP is out of PCBs. */
bool dhcpd_init(void);

/* Number of ACKs sent (diagnostic). */
uint32_t dhcpd_leases_granted(void);

/* MAC of the current lessee, or NULL if none yet. */
const uint8_t *dhcpd_client_mac(void);

#endif /* DHCPD_H */
