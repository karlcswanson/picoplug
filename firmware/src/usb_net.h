/*
 * usb_net.h - TinyUSB network class <-> lwIP netif glue for the USB link.
 *
 * Bring-up order matters: call usb_net_init() and
 * start every lwIP service (DHCP server, TCP listeners) BEFORE tud_init().
 * TinyUSB 0.18 announces "network connected" to the host by itself as soon
 * as the host selects the data interface, and iOS sends its DHCP DISCOVER
 * exactly once after that. There is no API to hold the link down, so the
 * stack must already be listening when USB enumerates.
 */
#ifndef USB_NET_H
#define USB_NET_H

#include <stdbool.h>
#include <stdint.h>

struct netif;

/* Initialise lwIP and add the USB netif with the configured static address.
 * host_mac is what the host will use for its own adapter (tud_network_mac_address);
 * dev_mac is this device's lwIP MAC. Both should be locally administered. */
void usb_net_init(const uint8_t host_mac[6], const uint8_t dev_mac[6]);

/* Feed received frames into lwIP and run lwIP timers. Call every loop. */
void usb_net_poll(void);

/* True while the USB host has the device configured. */
bool usb_net_mounted(void);

struct netif *usb_net_netif(void);

/* Diagnostics */
typedef struct {
    uint32_t rx_frames, tx_frames, tx_dropped, rx_dropped, mounts;
    uint32_t reenums;   /* self-initiated re-enumerations (see usb_net.c) */
} usb_net_stats_t;
const usb_net_stats_t *usb_net_stats(void);
/* The address lwIP is actually using on the USB link (settings may differ until reboot). */
const char *usb_net_ip_str(void);

#endif /* USB_NET_H */
