/*
 * probe.c - see probe.h.
 */
#include "probe.h"
#include "settings.h"
#include "eth_net.h"

#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/raw.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

#define PING_ID        0x5043
#define PING_DATA_LEN  32
#define PING_TIMEOUT_MS 1000
#define ARP_TIMEOUT_MS  2000
#define DNS_TIMEOUT_MS  5000

typedef enum { ST_IDLE, ST_ARP, ST_PING_GW, ST_PING_INET, ST_DNS, ST_WAIT } state_t;

static struct raw_pcb *ping_pcb;
static probe_result_t res;
static state_t state;
static absolute_time_t deadline;       /* current step timeout          */
static absolute_time_t next_run;
static uint16_t ping_seq;
static absolute_time_t ping_sent_at;
static bool ping_waiting;
static ip_addr_t ping_target;
static ip_addr_t custom_dns, saved_dns0;
static probe_ping_t *ping_stats;
static ip_addr_t inet_addr;
static ip_addr_t dns_result;
static absolute_time_t dns_started;
static bool dns_cb_done, dns_cb_ok;
static uint32_t last_lease_count;

static const char *phase_name(state_t s)
{
    switch (s) {
    case ST_IDLE: return "idle";
    case ST_ARP: return "arp";
    case ST_PING_GW: return "ping_gw";
    case ST_PING_INET: return "ping_inet";
    case ST_DNS: return "dns";
    case ST_WAIT: return "done";
    }
    return "?";
}

/* ---- ICMP echo ------------------------------------------------------- */

static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    (void)arg; (void)pcb;
    if (!ping_waiting || !ip_addr_cmp(addr, &ping_target)) return 0;
    const struct ip_hdr *iph = (const struct ip_hdr *)p->payload;
    u16_t hlen = (u16_t)(IPH_HL_BYTES(iph));
    if (p->tot_len < hlen + sizeof(struct icmp_echo_hdr)) return 0;
    struct icmp_echo_hdr e;
    pbuf_copy_partial(p, &e, sizeof e, hlen);
    if (ICMPH_TYPE(&e) != ICMP_ER || e.id != lwip_htons(PING_ID) || e.seqno != lwip_htons(ping_seq)) return 0;

    int64_t us = absolute_time_diff_us(ping_sent_at, get_absolute_time());
    uint16_t ms = (uint16_t)(us / 1000);
    if (ping_stats) {
        ping_stats->replies++;
        if (ping_stats->replies == 1 || ms < ping_stats->best_rtt_ms) ping_stats->best_rtt_ms = ms;
    }
    ping_waiting = false;
    pbuf_free(p);
    return 1;
}

static void ping_send(const ip_addr_t *target, probe_ping_t *st)
{
    struct pbuf *p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + PING_DATA_LEN, PBUF_RAM);
    if (!p) return;
    struct icmp_echo_hdr *e = (struct icmp_echo_hdr *)p->payload;
    ping_seq++;
    ICMPH_TYPE_SET(e, ICMP_ECHO);
    ICMPH_CODE_SET(e, 0);
    e->chksum = 0;
    e->id = lwip_htons(PING_ID);
    e->seqno = lwip_htons(ping_seq);
    uint8_t *d = (uint8_t *)p->payload + sizeof *e;
    for (int i = 0; i < PING_DATA_LEN; i++) d[i] = (uint8_t)('a' + i % 26);
    e->chksum = inet_chksum(e, p->len);

    ping_target = *target;
    ping_stats = st;
    st->sent++;
    st->sent_any = true;
    ping_sent_at = get_absolute_time();
    ping_waiting = true;
    raw_sendto(ping_pcb, p, target);
    pbuf_free(p);
    deadline = make_timeout_time_ms(PING_TIMEOUT_MS);
}

/* Drive one ping target; returns true when PROBE_PINGS have been tried. */
static bool ping_step(const ip_addr_t *target, probe_ping_t *st)
{
    if (ping_waiting) {
        if (!time_reached(deadline)) return false;
        ping_waiting = false;           /* timed out */
    }
    if (st->sent >= PROBE_PINGS) return true;
    ping_send(target, st);
    return false;
}

/* ---- DNS ------------------------------------------------------------- */

static void dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name; (void)arg;
    dns_cb_done = true;
    dns_cb_ok = ipaddr != NULL;
    if (ipaddr) dns_result = *ipaddr;
}

/* ---- state machine --------------------------------------------------- */

static void start_run(void)
{
    uint32_t runs = res.runs;
    memset(&res, 0, sizeof res);
    if (!ipaddr_aton(settings_get()->ping_target, &inet_addr)) ipaddr_aton(PROBE_INET_TARGET, &inet_addr);
    res.runs = runs + 1;
    res.running = true;
    state = ST_ARP;
    deadline = make_timeout_time_ms(ARP_TIMEOUT_MS);
    etharp_request(eth_net_netif(), netif_ip4_gw(eth_net_netif()));
}

bool probe_init(void)
{
    ping_pcb = raw_new(IP_PROTO_ICMP);
    if (!ping_pcb) return false;
    raw_recv(ping_pcb, ping_recv, NULL);
    raw_bind(ping_pcb, IP_ADDR_ANY);
    ipaddr_aton(PROBE_INET_TARGET, &inet_addr);
    res.phase = "idle";
    return true;
}

void probe_restart(void)
{
    state = ST_IDLE;
    res.running = false;
    next_run = get_absolute_time();
}

void probe_poll(void)
{
    eth_net_info_t info;
    eth_net_get_info(&info);
    uint32_t runs = res.runs;

    if (!info.bound) {
        if (state != ST_IDLE) {
            memset(&res, 0, sizeof res);
            res.runs = runs;
            state = ST_IDLE;
            ping_waiting = false;
        }
        res.phase = "idle";
        return;
    }
    /* a fresh lease (new port?) restarts immediately */
    if (info.leases != last_lease_count) {
        last_lease_count = info.leases;
        state = ST_IDLE;
        next_run = get_absolute_time();
    }

    struct netif *nif = eth_net_netif();
    switch (state) {
    case ST_IDLE:
    case ST_WAIT:
        if (time_reached(next_run)) start_run();
        break;

    case ST_ARP: {
        struct eth_addr *mac = NULL;
        const ip4_addr_t *ipret = NULL;
        if (etharp_find_addr(nif, netif_ip4_gw(nif), &mac, &ipret) >= 0 && mac) {
            res.gw_arp_ok = true;
            memcpy(res.gw_mac, mac->addr, 6);
            state = ST_PING_GW;
        } else if (time_reached(deadline)) {
            res.gw_arp_ok = false;
            state = ST_PING_GW;         /* try anyway; ping does its own ARP */
        }
        break;
    }

    case ST_PING_GW: {
        ip_addr_t gw;
        ip_addr_copy_from_ip4(gw, *netif_ip4_gw(nif));
        if (ping_step(&gw, &res.gw)) state = ST_PING_INET;
        break;
    }

    case ST_PING_INET:
        if (ping_step(&inet_addr, &res.inet)) {
            state = ST_DNS;
            dns_cb_done = false;
            dns_started = get_absolute_time();
            const settings_t *cfg = settings_get();
            res.dns_custom = cfg->dns_server[0] && ipaddr_aton(cfg->dns_server, &custom_dns);
            if (res.dns_custom) {
                /* lwIP has one resolver list; swap slot 0 for this lookup and put it back after */
                ip_addr_copy(saved_dns0, *dns_getserver(0));
                dns_setserver(0, &custom_dns);
            }
            ipaddr_ntoa_r(dns_getserver(0), res.dns_server, sizeof res.dns_server);
            err_t e = dns_gethostbyname(cfg->dns_name, &dns_result, dns_cb, NULL);
            if (e == ERR_OK) { dns_cb_done = true; dns_cb_ok = true; }
            else if (e != ERR_INPROGRESS) { dns_cb_done = true; dns_cb_ok = false; }
            deadline = make_timeout_time_ms(DNS_TIMEOUT_MS);
        }
        break;

    case ST_DNS:
        if (dns_cb_done || time_reached(deadline)) {
            if (res.dns_custom) dns_setserver(0, &saved_dns0);
            res.dns_done = true;
            res.dns_ok = dns_cb_done && dns_cb_ok;
            res.dns_ms = (uint16_t)(absolute_time_diff_us(dns_started, get_absolute_time()) / 1000);
            if (res.dns_ok) ipaddr_ntoa_r(&dns_result, res.dns_addr, sizeof res.dns_addr);
            res.valid = true;
            res.running = false;
            res.completed_at_s = to_ms_since_boot(get_absolute_time()) / 1000u;
            state = ST_WAIT;
            next_run = make_timeout_time_ms(PROBE_PERIOD_S * 1000);
            printf("probe: gw %s (%u/%u, %u ms), inet %u/%u (%u ms), dns %s\n",
                   res.gw_arp_ok ? "arp ok" : "no arp", res.gw.replies, res.gw.sent, res.gw.best_rtt_ms,
                   res.inet.replies, res.inet.sent, res.inet.best_rtt_ms, res.dns_ok ? res.dns_addr : "failed");
        }
        break;
    }
    res.phase = phase_name(state);
}

const probe_result_t *probe_result(void) { return &res; }
