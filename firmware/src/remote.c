/*
 * remote.c - see remote.h.
 */
#include "remote.h"
#include "eth_net.h"
#include "neighbor_table.h"
#include "lldp.h"
#include "settings.h"
#include "board_config.h"
#include "json.h"           /* STATUS_JSON_MAX */

#include "lwip/apps/mdns.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

static uint8_t self_mac[6];
static char name[PEERS_NAME_MAX + 1];
static int8_t svc_slot = -1;
static bool conflicted;
static peers_table_t tab;
static bool link_up;
static bool searching;
static u8_t search_id;
static uint32_t last_scan_ms;
static char txt_sig[160];

/* the tester being viewed: newest status, and the fetch in flight */
static struct {
    char     name[PEERS_NAME_MAX + 1];
    char     body[STATUS_JSON_MAX];
    size_t   len;
    uint32_t got_ms;
    char     err[48];
} cache;
static struct {
    struct tcp_pcb *pcb;
    char     name[PEERS_NAME_MAX + 1];
    char     rx[STATUS_JSON_MAX + 512];
    size_t   n;
    bool     overflow;
    uint32_t started_ms;
} fx;

static uint32_t now_ms(void) { uint32_t t = to_ms_since_boot(get_absolute_time()); return t ? t : 1; }

const peers_table_t *remote_peers(void) { return &tab; }
const char *remote_name(void) { return name; }
uint32_t remote_generation(void) { return tab.generation; }

/* ---- advertising ------------------------------------------------------ */

static void base_name(char *out, size_t cap)
{
    uint8_t p = settings_get()->probe;
    if (p <= 9) snprintf(out, cap, "%s-%u", USB_NET_HOSTNAME, p);
    else snprintf(out, cap, "%s", USB_NET_HOSTNAME);
}

static void pick_name(void)
{
    base_name(name, sizeof name);
    if (conflicted) {
        size_t l = strlen(name);
        snprintf(name + l, sizeof name - l, "-%02x%02x", self_mac[4], self_mac[5]);
    }
}

/* The switch this tester sees, as the other testers show it: the first LLDP
 * neighbor that is not a tester, else the first CDP one. */
static const lldp_neighbor_t *my_switch(void)
{
    const lldp_neighbor_t *cdp = NULL;
    for (int i = 0; i < neighbor_table_count(); i++) {
        const neighbor_entry_t *e = neighbor_table_get(i);
        if (!e || lldp_neighbor_is_picoplug(&e->n)) continue;
        if (e->n.proto == NEIGHBOR_PROTO_LLDP) return &e->n;
        if (!cdp) cdp = &e->n;
    }
    return cdp;
}

static void add_txt(struct mdns_service *svc, const char *k, const char *v)
{
    char b[64];
    int n = snprintf(b, sizeof b, "%s=%s", k, v);
    if (n > 0) mdns_resp_add_service_txtitem(svc, b, (u8_t)(n < (int)sizeof b ? n : (int)sizeof b - 1));
}

static void build_txt(char probe[2], char ch[33], char sw[33], char port[25], char pvid[6])
{
    uint8_t p = settings_get()->probe;
    probe[0] = p <= 9 ? (char)('0' + p) : '\0'; probe[1] = '\0';
    ch[0] = sw[0] = port[0] = pvid[0] = '\0';
    const lldp_neighbor_t *n = my_switch();
    if (!n) return;
    lldp_format_chassis_id(&n->chassis_id, ch, 33);
    if (n->has_sys_name) snprintf(sw, 33, "%.32s", n->sys_name);
    lldp_format_port_id(&n->port_id, port, 25);
    if (n->has_pvid) snprintf(pvid, 6, "%u", n->pvid);
}

static void txt_fn(struct mdns_service *svc, void *arg)
{
    (void)arg;
    char probe[2], ch[33], sw[33], port[25], pvid[6];
    build_txt(probe, ch, sw, port, pvid);
    if (probe[0]) add_txt(svc, "probe", probe);
    if (ch[0]) add_txt(svc, "ch", ch);
    if (sw[0]) add_txt(svc, "sw", sw);
    if (port[0]) add_txt(svc, "port", port);
    if (pvid[0]) add_txt(svc, "pvid", pvid);
    add_txt(svc, "fw", FW_VERSION);
}

static void name_result(struct netif *netif, u8_t result, s8_t slot)
{
    (void)slot;
    if (netif != eth_net_netif()) return;
    if (result == MDNS_PROBING_SUCCESSFUL) { printf("remote: mdns name %s.local\n", name); return; }
    /* someone else holds the name: add the last MAC digits and try again */
    if (conflicted) { printf("remote: mdns name %s taken as well; staying quiet\n", name); return; }
    conflicted = true;
    pick_name();
    printf("remote: mdns name taken, now %s\n", name);
    mdns_resp_rename_netif(netif, name);
    if (svc_slot >= 0) mdns_resp_rename_service(netif, (u8_t)svc_slot, name);
}

void remote_name_changed(void)
{
    conflicted = false;
    pick_name();
    struct netif *n = eth_net_netif();
    if (!mdns_resp_netif_active(n)) return;
    mdns_resp_rename_netif(n, name);
    if (svc_slot >= 0) mdns_resp_rename_service(n, (u8_t)svc_slot, name);
}

/* ---- finding ------------------------------------------------------------ */

static void on_answer(struct mdns_answer *a, const char *varpart, int varlen, int flags, void *arg)
{
    (void)flags; (void)arg;
    if (varlen < 0) return;
    if (peers_record(&tab, name, a->info.type, a->info.domain.name, a->info.domain.length,
                     (const uint8_t *)varpart, (size_t)varlen, a->ttl, now_ms())) {
        const peers_entry_t *e = NULL;
        char who[PEERS_NAME_MAX + 1] = "";
        if (peers_label(a->info.domain.name, a->info.domain.length, 0, who, sizeof who)) e = peers_find(&tab, who);
        if (e && a->info.type == PEERS_RR_TXT) printf("remote: %s probe %d on %s %s\n", e->name, e->probe == PEERS_PROBE_NONE ? -1 : e->probe,
                                                      e->sys_name[0] ? e->sys_name : e->chassis, e->port);
    }
}

void remote_scan(void)
{
    if (!link_up) return;
    uint32_t t = now_ms();
    if (last_scan_ms && t - last_scan_ms < REMOTE_SCAN_MIN_MS) return;
    last_scan_ms = t;
    if (searching) { mdns_search_stop(search_id); searching = false; }
    err_t e = mdns_search_service(NULL, PEERS_SERVICE, DNSSD_PROTO_TCP, eth_net_netif(), on_answer, NULL, &search_id);
    searching = e == ERR_OK;
    if (!searching) printf("remote: mdns query failed (%d)\n", e);
}

/* ---- viewing ------------------------------------------------------------ */

static bool fx_aborted;     /* fetch_done had to abort the pcb: a callback must return ERR_ABRT */

static void fetch_done(const char *err)
{
    fx_aborted = false;
    if (fx.pcb) {
        tcp_arg(fx.pcb, NULL); tcp_recv(fx.pcb, NULL); tcp_err(fx.pcb, NULL); tcp_sent(fx.pcb, NULL); tcp_poll(fx.pcb, NULL, 0);
        if (tcp_close(fx.pcb) != ERR_OK) { tcp_abort(fx.pcb); fx_aborted = true; }
        fx.pcb = NULL;
    }
    bool same = strcmp(cache.name, fx.name) == 0;
    if (!err) {
        fx.rx[fx.n < sizeof fx.rx ? fx.n : sizeof fx.rx - 1] = '\0';
        const char *body = strstr(fx.rx, "\r\n\r\n");
        if (strncmp(fx.rx, "HTTP/1.", 7) || strncmp(fx.rx + 8, " 200", 4)) err = "no status from it";
        else if (!body || fx.overflow) err = "its status did not fit";
        else {
            body += 4;
            size_t l = fx.n - (size_t)(body - fx.rx);
            if (l >= sizeof cache.body) err = "its status did not fit";
            else {
                memcpy(cache.name, fx.name, sizeof cache.name);
                memcpy(cache.body, body, l);
                cache.len = l;
                cache.got_ms = now_ms();
                cache.err[0] = '\0';
            }
        }
    }
    if (err) {
        if (!same) { memcpy(cache.name, fx.name, sizeof cache.name); cache.len = 0; }
        snprintf(cache.err, sizeof cache.err, "%s", err);
    }
    fx.name[0] = '\0';
}

static err_t fx_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (err != ERR_OK) { if (p) pbuf_free(p); return err; }
    if (!p) { fetch_done(NULL); return fx_aborted ? ERR_ABRT : ERR_OK; }
    tcp_recved(pcb, p->tot_len);
    size_t room = sizeof fx.rx - 1 - fx.n;
    if (p->tot_len > room) fx.overflow = true;
    size_t n = p->tot_len < room ? p->tot_len : room;
    pbuf_copy_partial(p, fx.rx + fx.n, (u16_t)n, 0);
    fx.n += n;
    pbuf_free(p);
    return ERR_OK;
}

static void fx_err(void *arg, err_t err)
{
    (void)arg; (void)err;
    fx.pcb = NULL;              /* already freed by lwIP */
    fetch_done("no answer");
}

static err_t fx_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) { fetch_done("no answer"); return fx_aborted ? ERR_ABRT : ERR_OK; }
    char req[96];
    int n = snprintf(req, sizeof req, "GET /status.json HTTP/1.0\r\nHost: %s.local\r\n\r\n", fx.name);
    if (tcp_write(pcb, req, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) { fetch_done("could not ask it"); return fx_aborted ? ERR_ABRT : ERR_OK; }
    tcp_output(pcb);
    return ERR_OK;
}

static void fetch_start(const peers_entry_t *e)
{
    ip_addr_t a;
    if (e->has_ip6) {
        ip_addr_t z;
        memcpy(&z.u_addr.ip6.addr, e->ip6, 16);
        IP_SET_TYPE_VAL(z, IPADDR_TYPE_V6);
        ip6_addr_clear_zone(ip_2_ip6(&z));
        ip6_addr_assign_zone(ip_2_ip6(&z), IP6_UNICAST, eth_net_netif());
        a = z;
    } else if (e->has_ip4) {
        IP_ADDR4(&a, e->ip4[0], e->ip4[1], e->ip4[2], e->ip4[3]);
    } else {
        snprintf(fx.name, sizeof fx.name, "%s", e->name);
        fetch_done("no address for it yet");
        return;
    }
    snprintf(fx.name, sizeof fx.name, "%s", e->name);
    fx.n = 0; fx.overflow = false;
    fx.started_ms = now_ms();
    fx.pcb = tcp_new_ip_type(IP_GET_TYPE(&a));
    if (!fx.pcb) { fetch_done("out of connections"); return; }
    tcp_bind_netif(fx.pcb, eth_net_netif());
    tcp_recv(fx.pcb, fx_recv);
    tcp_err(fx.pcb, fx_err);
    if (tcp_connect(fx.pcb, &a, e->http_port ? e->http_port : 80, fx_connected) != ERR_OK) fetch_done("could not reach it");
}

remote_view_t remote_view(const char *want, char *out, size_t cap, size_t *len, uint32_t *age_ms,
                          char *err, size_t errcap)
{
    *len = 0; *age_ms = 0; err[0] = '\0';
    const peers_entry_t *e = peers_find(&tab, want);
    if (!e) { snprintf(err, errcap, "not heard on this network"); return REMOTE_UNKNOWN; }
    if (!fx.pcb) fetch_start(e);
    if (strcmp(cache.name, e->name) != 0) return REMOTE_PENDING;
    if (!cache.len) {
        snprintf(err, errcap, "%s", cache.err);
        return cache.err[0] ? REMOTE_FAILED : REMOTE_PENDING;
    }
    if (cache.len >= cap) { snprintf(err, errcap, "its status did not fit"); return REMOTE_FAILED; }
    memcpy(out, cache.body, cache.len);
    *len = cache.len;
    *age_ms = now_ms() - cache.got_ms;
    snprintf(err, errcap, "%s", cache.err);     /* last refresh failed, older copy */
    return REMOTE_OK;
}

/* ---- life cycle --------------------------------------------------------- */

void remote_init(const uint8_t mac[6])
{
    memcpy(self_mac, mac, 6);
    peers_reset(&tab);
    pick_name();
    struct netif *n = eth_net_netif();
    mdns_resp_register_name_result_cb(name_result);
    if (mdns_resp_add_netif(n, name) != ERR_OK) { printf("remote: mdns on the port failed\n"); return; }
    svc_slot = mdns_resp_add_service(n, name, PEERS_SERVICE, DNSSD_PROTO_TCP, 80, txt_fn, NULL);
    if (svc_slot < 0) printf("remote: mdns service failed\n");
    printf("remote: advertising %s._picoplug._tcp on the port\n", name);
}

void remote_link_changed(bool up)
{
    if (up == link_up) return;
    link_up = up;
    if (!up) {
        if (searching) { mdns_search_stop(search_id); searching = false; }
        if (fx.pcb) fetch_done("link down");
        peers_reset(&tab);
        last_scan_ms = 0;
        txt_sig[0] = '\0';
    }
}

void remote_poll(void)
{
    uint32_t t = now_ms();
    if (peers_expire(&tab, t, REMOTE_PEER_AGE_MS)) printf("remote: a tester dropped out (%d left)\n", peers_count(&tab));
    if (fx.pcb && t - fx.started_ms > REMOTE_FETCH_MS) tcp_abort(fx.pcb);     /* fx_err finishes it */
    /* what we advertise changed (another switch or port, probe number): tell the others */
    static uint32_t next_sig;
    if (!link_up || (int32_t)(t - next_sig) < 0) return;
    next_sig = t + 1000;
    char probe[2], ch[33], sw[33], port[25], pvid[6], sig[sizeof txt_sig];
    build_txt(probe, ch, sw, port, pvid);
    snprintf(sig, sizeof sig, "%s|%s|%s|%s|%s", probe, ch, sw, port, pvid);
    if (strcmp(sig, txt_sig) != 0) {
        bool first = !txt_sig[0];
        memcpy(txt_sig, sig, sizeof txt_sig);
        if (!first && mdns_resp_netif_active(eth_net_netif())) mdns_resp_announce(eth_net_netif());
    }
}
