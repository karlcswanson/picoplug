/*
 * usb_net.c - see usb_net.h. Based on TinyUSB's net_lwip_webserver example.
 */
#include "usb_net.h"
#include "settings.h"
#include "board_config.h"

#include "tusb.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/apps/mdns.h"
#include "netif/ethernet.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

#define INIT_IP4_(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }
#define INIT_IP4(...) INIT_IP4_(__VA_ARGS__)   /* lets USB_NET_IP expand to 4 args */

/* Referenced by TinyUSB's net driver and by usb_descriptors.c. */
uint8_t tud_network_mac_address[6];

static struct netif netif_data;
static struct pbuf *received_frame;
static usb_net_stats_t stats;
static bool was_mounted;

/* Link watchdog state (see USB_NET_REENUM_* in board_config.h). */
static uint32_t alt1_ms, alt0_ms;       /* when the host last set alt 1 / alt 0 (0 = never) */
static uint8_t cur_alt;
static uint8_t reenum_budget_used;
static absolute_time_t reconnect_at;
static bool reconnect_pending;

static const ip4_addr_t default_ip = INIT_IP4(USB_NET_IP);
static ip4_addr_t ipaddr  = INIT_IP4(USB_NET_IP);          /* overwritten from settings in usb_net_init */
static const ip4_addr_t netmask = INIT_IP4(USB_NET_MASK);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

/* ---- lwIP -> USB ------------------------------------------------------ */

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (!tud_ready()) { stats.tx_dropped++; return ERR_IF; }

    /* Give TinyUSB a bounded chance to free a transmit buffer. lwIP will
     * retransmit TCP data if we drop; never spin forever here. */
    absolute_time_t deadline = make_timeout_time_ms(20);
    for (;;) {
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            stats.tx_frames++;
            return ERR_OK;
        }
        if (time_reached(deadline)) { stats.tx_dropped++; return ERR_WOULDBLOCK; }
        tud_task();
    }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
    return etharp_output(netif, p, addr);
}

static err_t netif_init_cb(struct netif *netif)
{
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6 | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = NULL;
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;   /* link-local only: lwIP's mDNS probes on IPv4 and IPv6 and stalls without it */
#endif
#if LWIP_NETIF_HOSTNAME
    netif->hostname = USB_NET_HOSTNAME;
#endif
    return ERR_OK;
}

void usb_net_init(const uint8_t host_mac[6], const uint8_t dev_mac[6])
{
    memcpy(tud_network_mac_address, host_mac, 6);

    lwip_init();

    struct netif *netif = &netif_data;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, dev_mac, 6);

    if (!ip4addr_aton(settings_get()->usb_ip, &ipaddr)) ipaddr = default_ip;
    netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ethernet_input);
    netif_set_default(netif);
    netif_set_up(netif);
    netif_create_ip6_linklocal_address(netif, 1);
    netif_set_link_up(netif);

#if LWIP_MDNS_RESPONDER
    /* Bonjour: the page is reachable as http://picoplug.local/ */
    mdns_resp_init();
    if (mdns_resp_add_netif(netif, USB_NET_HOSTNAME) == ERR_OK)
        printf("usbnet: mdns responder for %s.local\n", USB_NET_HOSTNAME);
#endif

    printf("usbnet: lwip up, device %s, dev mac %02x:%02x:%02x:%02x:%02x:%02x, host mac %02x:%02x:%02x:%02x:%02x:%02x\n",
           ip4addr_ntoa(&ipaddr),
           dev_mac[0], dev_mac[1], dev_mac[2], dev_mac[3], dev_mac[4], dev_mac[5],
           host_mac[0], host_mac[1], host_mac[2], host_mac[3], host_mac[4], host_mac[5]);
}

/* ---- USB -> lwIP ------------------------------------------------------ */

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    /* One frame in flight at a time; refuse until service_traffic drains it. */
    if (received_frame) return false;

    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) {
            memcpy(p->payload, src, size);
            received_frame = p;
        } else {
            stats.rx_dropped++;
        }
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    (void)arg;
    struct pbuf *p = (struct pbuf *)ref;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

/* From the patched NCM driver (tusb_ncm_device.c): host selected an alternate
 * setting of the data interface. alt 1 = data flows, alt 0 = closed. */
void tud_network_itf_alt_cb(uint8_t alt)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (alt == 1) alt1_ms = now ? now : 1; else alt0_ms = now ? now : 1;
    cur_alt = alt;
}

/* The failure signature seen on macOS (and matching the "plug it in again"
 * experience on iOS): the host enables the data interface, drops it again a
 * few ms later, and then ignores every link notification. Only a fresh
 * enumeration recovers it, so do that ourselves instead of asking the user
 * to replug. Returns true while a re-enumeration is in progress. */
static bool link_watchdog(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (reconnect_pending) {
        if (time_reached(reconnect_at)) {
            reconnect_pending = false;
            tud_connect();
        }
        return true;
    }
    if (cur_alt == 1) {
        if (alt1_ms && now - alt1_ms > USB_NET_REENUM_GOOD_S * 1000u) reenum_budget_used = 0;
        return false;
    }
    bool dropped_after_enable = alt1_ms && alt0_ms && alt0_ms >= alt1_ms &&
                                alt0_ms - alt1_ms < USB_NET_REENUM_DROP_WINDOW_MS &&
                                now - alt0_ms >= USB_NET_REENUM_SETTLE_MS;
    if (!dropped_after_enable || !tud_mounted()) return false;

    if (reenum_budget_used >= USB_NET_REENUM_MAX) {
        printf("usbnet: host dropped the data interface again; giving up after %u re-enumerations\n",
               (unsigned)reenum_budget_used);
        alt1_ms = alt0_ms = 0;
        return false;
    }
    reenum_budget_used++;
    stats.reenums++;
    printf("usbnet: host dropped the data interface %lu ms after enabling it; re-enumerating (%u/%u)\n",
           (unsigned long)(alt0_ms - alt1_ms), (unsigned)reenum_budget_used, (unsigned)USB_NET_REENUM_MAX);
    alt1_ms = alt0_ms = 0;
    tud_disconnect();
    reconnect_at = make_timeout_time_ms(USB_NET_REENUM_OFF_MS);
    reconnect_pending = true;
    return true;
}

void tud_network_init_cb(void)
{
    /* USB (re)configured: drop any half-processed frame. */
    if (received_frame) {
        pbuf_free(received_frame);
        received_frame = NULL;
    }
}

void usb_net_poll(void)
{
    if (received_frame) {
        struct pbuf *p = received_frame;
        received_frame = NULL;
        stats.rx_frames++;
        /* ethernet_input takes ownership unless it returns an error. */
        if (ethernet_input(p, &netif_data) != ERR_OK) pbuf_free(p);
        tud_network_recv_renew();
    }

    bool mounted = tud_mounted();
    if (mounted != was_mounted) {
        was_mounted = mounted;
        if (mounted) stats.mounts++;
        else cur_alt = 0;
        printf("usbnet: host %s\n", mounted ? "connected" : "disconnected");
    }

    link_watchdog();
    sys_check_timeouts();
}

bool usb_net_mounted(void) { return tud_mounted(); }
struct netif *usb_net_netif(void) { return &netif_data; }
const char *usb_net_ip_str(void) { return ip4addr_ntoa(&ipaddr); }

const usb_net_stats_t *usb_net_stats(void) { return &stats; }

/* ---- lwIP NO_SYS glue -------------------------------------------------- */

sys_prot_t sys_arch_protect(void) { return 0; }
void sys_arch_unprotect(sys_prot_t pval) { (void)pval; }
uint32_t sys_now(void) { return to_ms_since_boot(get_absolute_time()); }
