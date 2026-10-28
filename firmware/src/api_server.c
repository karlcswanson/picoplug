/*
 * api_server.c - see api_server.h. lwIP raw TCP API, no RTOS.
 */
#include "api_server.h"
#include "board_config.h"
#include "status.h"
#include "usb_net.h"

#include "lwip/tcp.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    struct tcp_pcb *pcb;
    bool     pending;          /* a message is owed but did not fit yet */
    uint32_t pending_since;
} client_t;

static client_t clients[API_MAX_CLIENTS];
static struct tcp_pcb *listen_pcb;
static char msg[STATUS_JSON_MAX + 1];   /* +1 for the newline */
static size_t msg_len;
static uint32_t seq;
static uint32_t last_fingerprint;
static absolute_time_t next_heartbeat;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void client_close(client_t *c)
{
    if (!c->pcb) return;
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_sent(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_poll(c->pcb, NULL, 0);
    if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    c->pcb = NULL;
    c->pending = false;
}

/* Build the current message (once per push, shared by all clients). */
static void build(void)
{
    seq++;
    msg_len = status_render(msg, sizeof msg - 1, seq);
    if (msg_len == 0) {
        printf("api: status did not fit in %u bytes\n", (unsigned)sizeof msg);
        return;
    }
    msg[msg_len++] = '\n';
}

/* Try to hand the message to one client; sets pending if it did not fit. */
static void send_to(client_t *c)
{
    if (!c->pcb || msg_len == 0) return;
    if (tcp_sndbuf(c->pcb) < msg_len) {
        if (!c->pending) { c->pending = true; c->pending_since = now_ms(); }
        return;
    }
    err_t e = tcp_write(c->pcb, msg, (u16_t)msg_len, TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK) {
        tcp_output(c->pcb);
        c->pending = false;
    } else if (e == ERR_MEM) {
        if (!c->pending) { c->pending = true; c->pending_since = now_ms(); }
    } else {
        printf("api: write error %d, dropping client\n", e);
        client_close(c);
    }
}

static void push_all(void)
{
    build();
    for (int i = 0; i < API_MAX_CLIENTS; i++) send_to(&clients[i]);
}

/* ---- lwIP callbacks --------------------------------------------------- */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    client_t *c = arg;
    if (!p) {                       /* remote closed */
        client_close(c);
        return ERR_OK;
    }
    if (err == ERR_OK) tcp_recved(pcb, p->tot_len);   /* input is ignored */
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb; (void)len;
    client_t *c = arg;
    if (c && c->pending) send_to(c);
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    client_t *c = arg;
    (void)err;
    if (c) { c->pcb = NULL; c->pending = false; }   /* pcb already freed by lwIP */
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)pcb;
    client_t *c = arg;
    /* A client that cannot drain a 4 KB message in 10 s is gone. */
    if (c && c->pending && now_ms() - c->pending_since > 10000) {
        printf("api: client stalled, dropping\n");
        client_close(c);
    }
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    client_t *c = NULL;
    for (int i = 0; i < API_MAX_CLIENTS; i++) if (!clients[i].pcb) { c = &clients[i]; break; }
    if (!c) {
        printf("api: too many clients, refusing\n");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    c->pcb = newpcb;
    c->pending = false;
    tcp_arg(newpcb, c);
    tcp_recv(newpcb, on_recv);
    tcp_sent(newpcb, on_sent);
    tcp_err(newpcb, on_err);
    tcp_poll(newpcb, on_poll, 4);           /* every 2 s */
    tcp_nagle_disable(newpcb);
    ip_set_option(newpcb, SOF_KEEPALIVE);
    newpcb->keep_idle = 10000;
    newpcb->keep_intvl = 5000;
    newpcb->keep_cnt = 3;
    printf("api: client connected (%d)\n", api_server_clients());

    /* First message immediately; restart the heartbeat from now so the
     * (possibly stale) timer does not fire a duplicate right after. */
    build();
    send_to(c);
    last_fingerprint = status_fingerprint();
    next_heartbeat = make_timeout_time_ms(API_HEARTBEAT_MS);
    return ERR_OK;
}

/* ---- public ----------------------------------------------------------- */

bool api_server_init(void)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, API_TCP_PORT) != ERR_OK) { tcp_abort(pcb); return false; }
    tcp_bind_netif(pcb, usb_net_netif());     /* never reachable from the switch side */
    listen_pcb = tcp_listen_with_backlog(pcb, 2);
    if (!listen_pcb) { tcp_abort(pcb); return false; }
    tcp_accept(listen_pcb, on_accept);
    last_fingerprint = status_fingerprint();
    next_heartbeat = make_timeout_time_ms(API_HEARTBEAT_MS);
    printf("api: NDJSON on :%d\n", API_TCP_PORT);
    return true;
}

void api_server_poll(void)
{
    if (api_server_clients() == 0) {
        last_fingerprint = status_fingerprint();
        return;
    }
    uint32_t fp = status_fingerprint();
    bool changed = fp != last_fingerprint;
    if (changed || time_reached(next_heartbeat)) {
        last_fingerprint = fp;
        next_heartbeat = make_timeout_time_ms(API_HEARTBEAT_MS);
        push_all();
    }
}

int api_server_clients(void)
{
    int n = 0;
    for (int i = 0; i < API_MAX_CLIENTS; i++) if (clients[i].pcb) n++;
    return n;
}
