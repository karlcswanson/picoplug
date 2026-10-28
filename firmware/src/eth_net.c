/*
 * eth_net.c - see eth_net.h.
 */
#include "eth_net.h"
#include "board_config.h"
#include "usb_net.h"
#include "w5500_raw.h"
#include "dhcp_info.h"

#include "lwip/dhcp.h"
#include "lwip/igmp.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/prot/dhcp.h"
#include "lwip/sys.h"
#include "netif/ethernet.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

static struct netif eth_netif;
static bool link_up;
static bool was_bound;
static uint32_t bound_at_ms;
static uint32_t leases;
static uint8_t our_mac[6];

/* Trace DHCP frames on the wire (both directions) to debug the client. */
static void trace_dhcp(const char *dir, const uint8_t *f, size_t len)
{
    if (len < 14 + 20 + 8 + 240 || f[12] != 0x08 || f[13] != 0x00) return;
    const uint8_t *ip = f + 14;
    if (ip[9] != 17) return;                       /* UDP */
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4;
    const uint8_t *udp = ip + ihl;
    uint16_t sport = (uint16_t)((udp[0] << 8) | udp[1]), dport = (uint16_t)((udp[2] << 8) | udp[3]);
    if (dport != 67 && dport != 68) return;
    const uint8_t *bootp = udp + 8;
    /* find option 53 */
    const uint8_t *o = bootp + 240, *end = f + len;
    uint8_t mt = 0;
    while (o + 2 <= end && *o != 255) { if (*o == 0) { o++; continue; } if (*o == 53 && o[1] == 1) { mt = o[2]; break; } o += 2 + o[1]; }
    static const char *const names[] = {"?", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NAK", "RELEASE", "INFORM"};
    printf("ethnet: %s dhcp %s %02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u yiaddr %u.%u.%u.%u (%u bytes)\n",
           dir, mt < 9 ? names[mt] : "?", f[6], f[7], f[8], f[9], f[10], f[11], f[0], f[1], f[2], f[3], f[4], f[5],
           ip[12], ip[13], ip[14], ip[15], sport, ip[16], ip[17], ip[18], ip[19], dport,
           bootp[16], bootp[17], bootp[18], bootp[19], (unsigned)len);
}

static err_t eth_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    static uint8_t buf[1536];
    if (p->tot_len > sizeof buf) return ERR_BUF;
    pbuf_copy_partial(p, buf, p->tot_len, 0);
    trace_dhcp("tx", buf, p->tot_len);
    dhcp_info_observe(buf, p->tot_len, true, sys_now());
    bool ok = w5500_send(buf, p->tot_len);
    if (!ok) printf("ethnet: tx of %u bytes failed\n", p->tot_len);
    return ok ? ERR_OK : ERR_IF;
}

static err_t eth_init_cb(struct netif *netif)
{
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;
    netif->name[0] = 'e';
    netif->name[1] = 't';
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, our_mac, 6);
    netif->linkoutput = eth_linkoutput;
    netif->output = etharp_output;
#if LWIP_NETIF_HOSTNAME
    netif->hostname = USB_NET_HOSTNAME;     /* sent as DHCP option 12 */
#endif
    return ERR_OK;
}

void eth_net_init(const uint8_t mac[6])
{
    memcpy(our_mac, mac, 6);
    dhcp_info_init(mac);
    netif_add(&eth_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, eth_init_cb, ethernet_input);
    netif_set_up(&eth_netif);
    /* Link is down until the PHY says otherwise; dhcp_start() waits for it
     * (LWIP_DHCP_CHECK_LINK_UP) and netif_set_link_up() kicks it off. */
    dhcp_start(&eth_netif);
    /* PTP (v1 and v2 over UDP) is multicast to 224.0.1.129; a snooping switch only forwards
     * it to ports that asked. lwIP re-reports the group on every link-up. */
    ip4_addr_t ptp_group; IP4_ADDR(&ptp_group, 224, 0, 1, 129);
    if (igmp_joingroup_netif(&eth_netif, &ptp_group) != ERR_OK) printf("ethnet: igmp join 224.0.1.129 failed\n");
    printf("ethnet: lwip netif on W5500, dhcp client armed, igmp joined 224.0.1.129\n");
}

void eth_net_input(const uint8_t *frame, size_t len)
{
    trace_dhcp("rx", frame, len);
    dhcp_info_observe(frame, len, false, sys_now());
    if (len < 14 || !link_up) return;
    /* Promiscuous socket: only hand lwIP what a normal NIC would see. */
    if (!(frame[0] & 0x01) && memcmp(frame, our_mac, 6) != 0) return;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!p) return;
    pbuf_take(p, frame, (u16_t)len);
    if (ethernet_input(p, &eth_netif) != ERR_OK) pbuf_free(p);
}

void eth_net_set_link(bool up)
{
    if (up == link_up) return;
    link_up = up;
    if (up) {
        dhcp_info_reset();
        netif_set_link_up(&eth_netif);      /* triggers DHCP discover/renew */
    } else {
        netif_set_link_down(&eth_netif);
        dhcp_release_and_stop(&eth_netif);
        dhcp_start(&eth_netif);
        netif_set_addr(&eth_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
    }
}

void eth_net_dhcp_retry(void)
{
    if (!link_up) return;
    dhcp_release_and_stop(&eth_netif);
    netif_set_addr(&eth_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
    dhcp_info_reset();
    dhcp_start(&eth_netif);
    printf("ethnet: dhcp restarted on request\n");
}

static const char *dhcp_state_name(u8_t st);

void eth_net_poll(void)
{
    static u8_t last_state = 0xFF;
    struct dhcp *d = netif_dhcp_data(&eth_netif);
    if (d && d->state != last_state) {
        last_state = d->state;
        printf("ethnet: dhcp %s (tries %u)\n", dhcp_state_name(d->state), d->tries);
    }
    bool bound = link_up && dhcp_supplied_address(&eth_netif);
    if (bound != was_bound) {
        was_bound = bound;
        if (bound) {
            igmp_report_groups(&eth_netif);     /* re-report with a real source address */
            leases++;
            bound_at_ms = to_ms_since_boot(get_absolute_time());
            netif_set_default(&eth_netif);
            printf("ethnet: dhcp bound %s/%s gw %s\n", ip4addr_ntoa(netif_ip4_addr(&eth_netif)),
                   ip4addr_ntoa(netif_ip4_netmask(&eth_netif)), ip4addr_ntoa(netif_ip4_gw(&eth_netif)));
        } else {
            netif_set_default(usb_net_netif());
            printf("ethnet: dhcp lease gone\n");
        }
    }
}

static const char *dhcp_state_name(u8_t st)
{
    switch (st) {
    case DHCP_STATE_OFF: return "off";
    case DHCP_STATE_REQUESTING: return "requesting";
    case DHCP_STATE_INIT: return "init";
    case DHCP_STATE_REBOOTING: return "rebooting";
    case DHCP_STATE_REBINDING: return "rebinding";
    case DHCP_STATE_RENEWING: return "renewing";
    case DHCP_STATE_SELECTING: return "selecting";
    case DHCP_STATE_INFORMING: return "informing";
    case DHCP_STATE_CHECKING: return "checking";
    case DHCP_STATE_PERMANENT: return "permanent";
    case DHCP_STATE_BOUND: return "bound";
    case DHCP_STATE_RELEASING: return "releasing";
    case DHCP_STATE_BACKING_OFF: return "backoff";
    default: return "unknown";
    }
}

void eth_net_get_info(eth_net_info_t *out)
{
    memset(out, 0, sizeof *out);
    out->link = link_up;
    struct dhcp *d = netif_dhcp_data(&eth_netif);
    strncpy(out->state, link_up ? (d ? dhcp_state_name(d->state) : "off") : "nolink", sizeof out->state - 1);
    out->bound = was_bound;
    out->leases = leases;
    if (!was_bound) return;
    ip4addr_ntoa_r(netif_ip4_addr(&eth_netif), out->ip, sizeof out->ip);
    ip4addr_ntoa_r(netif_ip4_netmask(&eth_netif), out->mask, sizeof out->mask);
    ip4addr_ntoa_r(netif_ip4_gw(&eth_netif), out->gateway, sizeof out->gateway);
    if (d) {
        ipaddr_ntoa_r(&d->server_ip_addr, out->server, sizeof out->server);
        out->lease_s = d->offered_t0_lease;
    }
    for (u8_t i = 0; i < 2 && i < DNS_MAX_SERVERS; i++) {
        const ip_addr_t *dns = dns_getserver(i);
        if (dns && !ip_addr_isany(dns)) {
            ipaddr_ntoa_r(dns, out->dns[out->n_dns], sizeof out->dns[0]);
            out->n_dns++;
        }
    }
    out->bound_for_s = (to_ms_since_boot(get_absolute_time()) - bound_at_ms) / 1000u;
}

struct netif *eth_net_netif(void) { return &eth_netif; }
