/*
 * tool.c - see tool.h.
 */
#include "tool.h"
#include "eth_net.h"

#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/raw.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

#define TOOL_ID           0x5054          /* "PT": distinct from the probe's id */
#define PING_INTERVAL_MS  1000
#define PING_TIMEOUT_MS   1000
#define TRACE_TIMEOUT_MS  1500
#define DNS_TIMEOUT_MS    5000
#define DATA_LEN          32

typedef enum { S_IDLE, S_RESOLVE, S_SEND, S_WAIT, S_DONE } st_t;

static struct raw_pcb *pcb;
static tool_state_t ts;
static st_t st;
static ip_addr_t target;
static uint16_t seq;
static absolute_time_t sent_at, deadline, dns_deadline;
static bool waiting;
static uint8_t cur_ttl;
static bool dns_done, dns_ok;
static ip_addr_t dns_addr;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static void bump(void) { ts.generation++; }

static void finish(const char *err)
{
    if (err) { strncpy(ts.error, err, sizeof ts.error - 1); ts.error[sizeof ts.error - 1] = '\0'; }
    ts.running = false;
    ts.finished_ms = now_ms();
    waiting = false;
    st = S_DONE;
    bump();
}

/* ---- receive: echo replies for ping, time-exceeded / unreachable / echo for trace ---- */
static u8_t recv_cb(void *arg, struct raw_pcb *rp, struct pbuf *p, const ip_addr_t *addr)
{
    (void)arg; (void)rp;
    if (!waiting || st != S_WAIT) return 0;
    const struct ip_hdr *iph = (const struct ip_hdr *)p->payload;
    u16_t hlen = (u16_t)IPH_HL_BYTES(iph);
    if (p->tot_len < hlen + sizeof(struct icmp_echo_hdr)) return 0;
    struct icmp_echo_hdr e;
    pbuf_copy_partial(p, &e, sizeof e, hlen);
    uint8_t type = ICMPH_TYPE(&e);
    bool ours = false;
    if (type == ICMP_ER) {
        ours = e.id == lwip_htons(TOOL_ID) && e.seqno == lwip_htons(seq) && ip_addr_cmp(addr, &target);
    } else if (type == ICMP_TE || type == ICMP_DUR) {
        /* inner: original IP header (variable) + first 8 bytes of our echo */
        u16_t off = (u16_t)(hlen + 8);
        if (p->tot_len < off + 20 + 8) return 0;
        uint8_t inner[28];
        pbuf_copy_partial(p, inner, sizeof inner, off);
        u16_t ihl = (u16_t)((inner[0] & 0x0F) * 4);
        if (ihl != 20) { if (p->tot_len < off + ihl + 8) return 0; pbuf_copy_partial(p, inner + 20, 8, (u16_t)(off + ihl)); }
        const uint8_t *ie = inner + 20;
        uint16_t iid = (uint16_t)(ie[4] << 8 | ie[5]), iseq = (uint16_t)(ie[6] << 8 | ie[7]);
        ours = ie[0] == ICMP_ECHO && iid == TOOL_ID && iseq == seq;
    }
    if (!ours) return 0;

    uint16_t ms = (uint16_t)(absolute_time_diff_us(sent_at, get_absolute_time()) / 1000);
    waiting = false;
    if (ts.kind == TOOL_PING) {
        if (type == ICMP_ER) {
            ts.recv++;
            ts.rtts[(ts.sent - 1) % TOOL_PING_MAX] = ms;
            ts.last_ms = ms; ts.sum_ms += ms;
            if (ts.recv == 1 || ms < ts.min_ms) ts.min_ms = ms;
            if (ms > ts.max_ms) ts.max_ms = ms;
        } else {
            ts.rtts[(ts.sent - 1) % TOOL_PING_MAX] = TOOL_RTT_TIMEOUT;   /* unreachable counts as lost */
        }
        st = S_SEND;
        bump();
    } else {
        tool_hop_t *h = &ts.hops[ts.n_hops - 1];
        h->answered = true;
        h->ms = ms;
        memcpy(h->addr, &ip_2_ip4(addr)->addr, 4);
        if (type == ICMP_ER) { h->reached = true; ts.reached = true; finish(NULL); }
        else if (type == ICMP_DUR) { h->unreachable = true; finish("destination unreachable"); }
        else st = S_SEND;
        bump();
    }
    pbuf_free(p);
    return 1;
}

static void send_echo(uint8_t ttl)
{
    struct pbuf *p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + DATA_LEN, PBUF_RAM);
    if (!p) { finish("out of memory"); return; }
    struct icmp_echo_hdr *e = (struct icmp_echo_hdr *)p->payload;
    seq++;
    ICMPH_TYPE_SET(e, ICMP_ECHO);
    ICMPH_CODE_SET(e, 0);
    e->chksum = 0;
    e->id = lwip_htons(TOOL_ID);
    e->seqno = lwip_htons(seq);
    uint8_t *d = (uint8_t *)p->payload + sizeof *e;
    for (int i = 0; i < DATA_LEN; i++) d[i] = (uint8_t)('a' + i % 26);
    e->chksum = inet_chksum(e, p->len);
    pcb->ttl = ttl;
    cur_ttl = ttl;
    sent_at = get_absolute_time();
    waiting = true;
    err_t r = raw_sendto(pcb, p, &target);
    pbuf_free(p);
    if (r != ERR_OK) { finish(r == ERR_RTE ? "no route to host" : "send failed"); return; }
    deadline = make_timeout_time_ms(ts.kind == TOOL_PING ? PING_TIMEOUT_MS : TRACE_TIMEOUT_MS);
    st = S_WAIT;
}

static void dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name; (void)arg;
    dns_done = true;
    dns_ok = ipaddr != NULL;
    if (ipaddr) dns_addr = *ipaddr;
}

static bool valid_host(const char *h)
{
    size_t n = strlen(h);
    if (!n || n > TOOL_HOST_MAX) return false;
    for (const char *p = h; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    return true;
}

bool tool_init(void)
{
    pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) return false;
    raw_recv(pcb, recv_cb, NULL);
    raw_bind(pcb, IP_ADDR_ANY);
    return true;
}

bool tool_start(tool_kind_t kind, const char *host, uint16_t count, char *err, size_t errcap)
{
    if (!pcb) { snprintf(err, errcap, "tool not initialised"); return false; }
    if (!valid_host(host)) { snprintf(err, errcap, "host: letters, digits, dots, dashes, up to %d", TOOL_HOST_MAX); return false; }
    eth_net_info_t ei; eth_net_get_info(&ei);
    if (!ei.bound) { snprintf(err, errcap, "the port has no address yet (DHCP %s)", ei.state); return false; }
    if (kind == TOOL_PING && count > TOOL_PING_MAX) { snprintf(err, errcap, "count: 0 (continuous) or 1..%d", TOOL_PING_MAX); return false; }
    uint32_t g = ts.generation;
    memset(&ts, 0, sizeof ts);
    ts.generation = g + 1;
    ts.kind = kind;
    ts.running = true;
    ts.count = kind == TOOL_PING ? count : 0;
    strncpy(ts.host, host, TOOL_HOST_MAX);
    ts.started_ms = now_ms();
    waiting = false;
    if (ipaddr_aton(host, &target)) {
        memcpy(ts.addr, &ip_2_ip4(&target)->addr, 4);
        ts.resolved = true;
        st = S_SEND;
        return true;
    }
    dns_done = false;
    err_t e = dns_gethostbyname(host, &dns_addr, dns_cb, NULL);
    if (e == ERR_OK) { dns_done = true; dns_ok = true; }
    else if (e != ERR_INPROGRESS) { finish("name lookup failed"); return true; }
    dns_deadline = make_timeout_time_ms(DNS_TIMEOUT_MS);
    st = S_RESOLVE;
    return true;
}

void tool_stop(void)
{
    if (ts.running) finish("stopped");
}

void tool_poll(void)
{
    switch (st) {
    case S_IDLE: case S_DONE: return;
    case S_RESOLVE:
        if (dns_done) {
            if (!dns_ok) { finish("name not found"); return; }
            target = dns_addr;
            memcpy(ts.addr, &ip_2_ip4(&target)->addr, 4);
            ts.resolved = true;
            bump();
            st = S_SEND;
        } else if (time_reached(dns_deadline)) finish("name lookup timed out");
        return;
    case S_SEND:
        if (ts.kind == TOOL_PING) {
            if (ts.sent >= (ts.count ? ts.count : TOOL_PING_CONT_MAX)) { finish(ts.count ? NULL : "stopped after 15 min"); return; }
            /* keep the 1 s cadence even when replies come back fast */
            if (ts.sent && absolute_time_diff_us(sent_at, get_absolute_time()) < PING_INTERVAL_MS * 1000) return;
            ts.sent++;
            ts.rtts[(ts.sent - 1) % TOOL_PING_MAX] = TOOL_RTT_TIMEOUT;
            send_echo(64);
        } else {
            if (ts.n_hops >= TOOL_TRACE_HOPS) { finish("no answer within 20 hops"); return; }
            tool_hop_t *h = &ts.hops[ts.n_hops++];
            memset(h, 0, sizeof *h);
            h->ttl = (uint8_t)ts.n_hops;
            send_echo(h->ttl);
        }
        return;
    case S_WAIT:
        if (waiting && time_reached(deadline)) {
            waiting = false;
            bump();
            st = S_SEND;              /* ping: rtts[] already says timeout; trace: hop stays unanswered */
        }
        return;
    }
}

const tool_state_t *tool_get(void) { return &ts; }
