/*
 * dhcpd.c - see dhcpd.h. RFC 2131/2132 subset: DISCOVER, REQUEST, INFORM,
 * RELEASE/DECLINE (ignored). Raw lwIP UDP API, no allocation beyond the
 * reply pbuf.
 */
#include "dhcpd.h"
#include "settings.h"
#include "board_config.h"
#include "usb_net.h"

#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/udp.h"

#include <stdio.h>
#include <string.h>

#define DHCP_SERVER_PORT   67
#define DHCP_CLIENT_PORT   68
#define BOOTP_MIN_LEN      240   /* fixed header (236) + magic cookie (4) */
#define BOOTREQUEST        1
#define BOOTREPLY          2
#define BOOTP_FLAG_BCAST   0x8000

enum { DHCPDISCOVER = 1, DHCPOFFER, DHCPREQUEST, DHCPDECLINE, DHCPACK, DHCPNAK, DHCPRELEASE, DHCPINFORM };
enum { OPT_PAD = 0, OPT_SUBNET = 1, OPT_REQ_IP = 50, OPT_LEASE = 51, OPT_MSGTYPE = 53,
       OPT_SERVER_ID = 54, OPT_RENEW_T1 = 58, OPT_REBIND_T2 = 59, OPT_END = 255 };

static const uint8_t magic[4] = {99, 130, 83, 99};

static struct udp_pcb *pcb;
static uint32_t leases;
static uint8_t client_mac[6];
static bool have_client;

static uint8_t server_ip[4]  = {USB_NET_IP};      /* both set from settings in dhcpd_init */
static uint8_t client_ip[4]  = {USB_NET_CLIENT_IP};
static const uint8_t subnet_mask[4] = {USB_NET_MASK};

/* Find option `code` in the options area; returns pointer to its length byte. */
static const uint8_t *find_option(const uint8_t *opts, size_t len, uint8_t code)
{
    size_t i = 0;
    while (i < len) {
        uint8_t c = opts[i];
        if (c == OPT_END) break;
        if (c == OPT_PAD) { i++; continue; }
        if (i + 1 >= len) break;
        uint8_t l = opts[i + 1];
        if (i + 2 + l > len) break;
        if (c == code) return &opts[i + 1];
        i += 2 + l;
    }
    return NULL;
}

static uint8_t *put_opt(uint8_t *o, uint8_t code, const void *v, uint8_t l)
{
    *o++ = code; *o++ = l;
    memcpy(o, v, l);
    return o + l;
}

static void send_reply(const uint8_t *req, uint8_t msgtype, bool assign, const ip_addr_t *dst_hint)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 300, PBUF_RAM);
    if (!p) return;
    uint8_t *r = (uint8_t *)p->payload;
    memset(r, 0, 300);

    r[0] = BOOTREPLY;
    r[1] = req[1]; r[2] = req[2];              /* htype, hlen */
    memcpy(r + 4, req + 4, 4);                 /* xid */
    memcpy(r + 10, req + 10, 2);               /* flags */
    memcpy(r + 12, req + 12, 4);               /* ciaddr echo */
    if (assign) memcpy(r + 16, client_ip, 4);  /* yiaddr */
    memcpy(r + 20, server_ip, 4);              /* siaddr */
    memcpy(r + 24, req + 24, 4);               /* giaddr */
    memcpy(r + 28, req + 28, 16);              /* chaddr */
    memcpy(r + 236, magic, 4);

    uint8_t *o = r + 240;
    o = put_opt(o, OPT_MSGTYPE, &msgtype, 1);
    o = put_opt(o, OPT_SERVER_ID, server_ip, 4);
    if (msgtype != DHCPNAK) {
        uint32_t lease = PP_HTONL(USB_NET_DHCP_LEASE_S);
        uint32_t t1 = PP_HTONL(USB_NET_DHCP_LEASE_S / 2);
        uint32_t t2 = PP_HTONL(USB_NET_DHCP_LEASE_S * 7 / 8);
        if (assign) {
            o = put_opt(o, OPT_LEASE, &lease, 4);
            o = put_opt(o, OPT_RENEW_T1, &t1, 4);
            o = put_opt(o, OPT_REBIND_T2, &t2, 4);
        }
        o = put_opt(o, OPT_SUBNET, subnet_mask, 4);
        /* Intentionally no OPT_ROUTER (3) and no OPT_DNS (6). */
    }
    *o++ = OPT_END;
    pbuf_realloc(p, (u16_t)(o - r));

    /* RFC 2131 4.1: broadcast unless the client already has an address it
     * can be reached at (RENEW: ciaddr set, unicast request). */
    ip_addr_t dst;
    uint16_t flags = (uint16_t)((req[10] << 8) | req[11]);
    uint32_t ciaddr;
    memcpy(&ciaddr, req + 12, 4);
    if (ciaddr != 0 && !(flags & BOOTP_FLAG_BCAST) && dst_hint && !ip_addr_isany(dst_hint) &&
        !ip_addr_isbroadcast(dst_hint, usb_net_netif())) {
        dst = *dst_hint;
    } else {
        IP_ADDR4(&dst, 255, 255, 255, 255);
    }
    err_t e = udp_sendto_if(pcb, p, &dst, DHCP_CLIENT_PORT, usb_net_netif());
    if (e != ERR_OK) printf("dhcpd: send failed (%d)\n", e);
    pbuf_free(p);
}

static void recv_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)upcb; (void)port;
    /* Serve the USB link only. DHCP DISCOVERs from other hosts on the
     * switch also arrive here (Ethernet netif) and must never be answered. */
    if (ip_current_input_netif() != usb_net_netif()) { pbuf_free(p); return; }
    if (p->tot_len < BOOTP_MIN_LEN || p->tot_len > 1500) { pbuf_free(p); return; }

    /* Flatten into a local buffer (requests are small). */
    static uint8_t req[600];
    uint16_t len = p->tot_len > sizeof req ? (uint16_t)sizeof req : p->tot_len;
    pbuf_copy_partial(p, req, len, 0);
    pbuf_free(p);

    if (req[0] != BOOTREQUEST || req[1] != 1 || req[2] != 6) return;
    if (memcmp(req + 236, magic, 4) != 0) return;

    const uint8_t *opts = req + 240;
    size_t olen = len - 240;
    const uint8_t *mt = find_option(opts, olen, OPT_MSGTYPE);
    if (!mt || mt[0] != 1) return;
    uint8_t msgtype = mt[1];

    const uint8_t *mac = req + 28;
    const uint8_t *sid = find_option(opts, olen, OPT_SERVER_ID);
    const uint8_t *rip = find_option(opts, olen, OPT_REQ_IP);

    switch (msgtype) {
    case DHCPDISCOVER:
        printf("dhcpd: DISCOVER from %02x:%02x:%02x:%02x:%02x:%02x -> OFFER %u.%u.%u.%u\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
               client_ip[0], client_ip[1], client_ip[2], client_ip[3]);
        send_reply(req, DHCPOFFER, true, addr);
        break;

    case DHCPREQUEST: {
        /* Addressed to another server? Then it is not ours to answer. */
        if (sid && sid[0] == 4 && memcmp(sid + 1, server_ip, 4) != 0) return;
        const uint8_t *want = NULL;
        if (rip && rip[0] == 4) want = rip + 1;
        else if (memcmp(req + 12, "\0\0\0\0", 4) != 0) want = req + 12;   /* RENEW/REBIND: ciaddr */
        if (want && memcmp(want, client_ip, 4) == 0) {
            memcpy(client_mac, mac, 6);
            have_client = true;
            leases++;
            printf("dhcpd: REQUEST %u.%u.%u.%u from %02x:%02x:%02x:%02x:%02x:%02x -> ACK\n",
                   want[0], want[1], want[2], want[3], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            send_reply(req, DHCPACK, true, addr);
        } else {
            printf("dhcpd: REQUEST for %u.%u.%u.%u -> NAK\n",
                   want ? want[0] : 0, want ? want[1] : 0, want ? want[2] : 0, want ? want[3] : 0);
            send_reply(req, DHCPNAK, false, addr);
        }
        break;
    }

    case DHCPINFORM:
        printf("dhcpd: INFORM -> ACK (no address)\n");
        send_reply(req, DHCPACK, false, addr);
        break;

    case DHCPRELEASE:
    case DHCPDECLINE:
        printf("dhcpd: %s from client\n", msgtype == DHCPRELEASE ? "RELEASE" : "DECLINE");
        break;

    default:
        break;
    }
}

bool dhcpd_init(void)
{
    settings_parse_ip(settings_get()->usb_ip, server_ip);
    settings_usb_peer_ip(settings_get(), client_ip);
    pcb = udp_new();
    if (!pcb) return false;
    ip_set_option(pcb, SOF_BROADCAST);
    if (udp_bind(pcb, IP_ADDR_ANY, DHCP_SERVER_PORT) != ERR_OK) {
        udp_remove(pcb);
        pcb = NULL;
        return false;
    }
    udp_recv(pcb, recv_cb, NULL);
    printf("dhcpd: listening on :67, offering %u.%u.%u.%u (no router/DNS options)\n",
           client_ip[0], client_ip[1], client_ip[2], client_ip[3]);
    return true;
}

uint32_t dhcpd_leases_granted(void) { return leases; }
const uint8_t *dhcpd_client_mac(void) { return have_client ? client_mac : NULL; }
