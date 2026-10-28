/*
 * eth_net.h - lwIP network interface on the W5500 (Ethernet side).
 *
 * Runs a DHCP client so the device learns which subnet the switch port
 * lands on, and gives probe.c ARP/ICMP/DNS on that port. Nothing listens on
 * this interface; the API and viewer bind to the USB interface only.
 */
#ifndef ETH_NET_H
#define ETH_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct netif;

typedef struct {
    bool     link;
    bool     bound;                 /* DHCP lease held               */
    char     state[12];             /* lwIP DHCP state name          */
    char     ip[16], mask[16], gateway[16], server[16];
    char     dns[2][16];
    uint8_t  n_dns;
    uint32_t lease_s;
    uint32_t bound_for_s;           /* seconds since the lease was obtained */
    uint32_t leases;                /* leases obtained since boot    */
} eth_net_info_t;

/* Call after lwip_init(). mac is the W5500 MAC (also used as SHAR). */
void eth_net_init(const uint8_t mac[6]);

/* Feed a received frame (from w5500_poll). Frames for other unicast MACs
 * are dropped here; broadcast, multicast, and our own pass to lwIP. */
void eth_net_input(const uint8_t *frame, size_t len);

/* Report PHY link changes; restarts DHCP on link up. */
void eth_net_set_link(bool up);

/* Housekeeping: default-route switching when a lease comes and goes. */
void eth_net_poll(void);
/* Drop the lease (if any) and start DHCP again from DISCOVER. No-op without a link. */
void eth_net_dhcp_retry(void);

void eth_net_get_info(eth_net_info_t *out);
struct netif *eth_net_netif(void);

#endif /* ETH_NET_H */
