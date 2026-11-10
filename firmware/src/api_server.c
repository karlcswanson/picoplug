/*
 * api_server.c - see api_server.h. lwIP raw TCP API, no RTOS.
 *
 * Two kinds of client get the same status pushes: raw TCP on :9000 (NDJSON, one message per
 * line, USB side only) and WebSocket clients the HTTP server hands over after the upgrade at
 * /ws (one text frame per message, both sides; the stream is read-only).
 */
#include "api_server.h"
#include "board_config.h"
#include "status.h"
#include "usb_net.h"
#include "ws.h"

#include "lwip/tcp.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

#define MAX_CLIENTS (API_MAX_CLIENTS + WS_MAX_CLIENTS)

typedef struct {
    struct tcp_pcb *pcb;
    bool     ws;
    bool     pending;          /* a message is owed but did not fit yet */
    uint32_t pending_since;
    uint8_t  rx[2 + 4 + 125];  /* ws: one client frame being read */
    uint8_t  rx_len;
} client_t;

static client_t clients[MAX_CLIENTS];
static struct tcp_pcb *listen_pcb;
/* the message, rendered once per push: room for a WebSocket header in front, '\n' behind */
static char buf[WS_HDR_MAX + STATUS_JSON_MAX + 1];
static char *const msg = buf + WS_HDR_MAX;
static size_t msg_len;          /* JSON only, without the '\n' */
static size_t ws_hdr_len;
static uint32_t seq;
static uint32_t last_fingerprint;
static absolute_time_t next_heartbeat, next_allowed;

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
    msg_len = status_render(msg, STATUS_JSON_MAX, seq);
    if (msg_len == 0) {
        printf("api: status did not fit in %u bytes\n", (unsigned)STATUS_JSON_MAX);
        return;
    }
    msg[msg_len] = '\n';
    uint8_t h[WS_HDR_MAX];
    ws_hdr_len = ws_text_header(msg_len, h);
    memcpy(msg - ws_hdr_len, h, ws_hdr_len);
}

/* Try to hand the message to one client; sets pending if it did not fit. */
static void send_to(client_t *c)
{
    if (!c->pcb || msg_len == 0) return;
    const char *p = c->ws ? msg - ws_hdr_len : msg;
    size_t n = c->ws ? ws_hdr_len + msg_len : msg_len + 1;
    if (tcp_sndbuf(c->pcb) < n) {
        if (!c->pending) { c->pending = true; c->pending_since = now_ms(); }
        return;
    }
    err_t e = tcp_write(c->pcb, p, (u16_t)n, TCP_WRITE_FLAG_COPY);
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
    for (int i = 0; i < MAX_CLIENTS; i++) send_to(&clients[i]);
}

/* ws: answer what the browser sends. Close gets a close back, ping a pong; the rest is ignored. */
static void ws_input(client_t *c, struct pbuf *p)
{
    for (u16_t off = 0; off < p->tot_len && c->pcb; ) {
        u16_t room = (u16_t)(sizeof c->rx - c->rx_len);
        u16_t n = (u16_t)(p->tot_len - off) < room ? (u16_t)(p->tot_len - off) : room;
        pbuf_copy_partial(p, c->rx + c->rx_len, n, off);
        c->rx_len = (uint8_t)(c->rx_len + n);
        off = (u16_t)(off + n);
        ws_frame_t f;
        ws_parse_t r;
        while (c->pcb && (r = ws_parse(c->rx, c->rx_len, &f)) == WS_FRAME) {
            if (f.opcode == 0x8 || f.opcode == 0x9) {
                uint8_t out[2 + 125];
                out[0] = (uint8_t)(0x80 | (f.opcode == 0x8 ? 0x8 : 0xA));
                uint8_t plen = f.opcode == 0x8 ? (f.len >= 2 ? 2 : 0) : f.len;   /* close: echo the code only */
                out[1] = plen;
                memcpy(out + 2, f.payload, plen);
                if (tcp_sndbuf(c->pcb) >= 2u + plen && tcp_write(c->pcb, out, (u16_t)(2 + plen), TCP_WRITE_FLAG_COPY) == ERR_OK)
                    tcp_output(c->pcb);
                if (f.opcode == 0x8) { client_close(c); return; }
            }
            memmove(c->rx, c->rx + f.frame_len, c->rx_len - f.frame_len);
            c->rx_len = (uint8_t)(c->rx_len - f.frame_len);
        }
        if (c->pcb && r == WS_BAD) {
            printf("api: bad websocket frame, dropping client\n");
            client_close(c);
            return;
        }
    }
}

/* ---- lwIP callbacks --------------------------------------------------- */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    client_t *c = arg;
    if (!p) {                       /* remote closed */
        client_close(c);
        return ERR_OK;
    }
    if (err == ERR_OK) tcp_recved(pcb, p->tot_len);
    if (err == ERR_OK && c && c->ws) ws_input(c, p);   /* raw :9000 input is ignored */
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

static int count(bool ws)
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i].pcb && clients[i].ws == ws) n++;
    return n;
}

/* Take over a connection: our callbacks, keepalive, and the first message right away. */
static client_t *adopt(struct tcp_pcb *pcb, bool ws)
{
    if (count(ws) >= (ws ? WS_MAX_CLIENTS : API_MAX_CLIENTS)) return NULL;
    client_t *c = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) if (!clients[i].pcb) { c = &clients[i]; break; }
    if (!c) return NULL;
    c->pcb = pcb;
    c->ws = ws;
    c->pending = false;
    c->rx_len = 0;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    tcp_poll(pcb, on_poll, 4);              /* every 2 s */
    tcp_nagle_disable(pcb);
    ip_set_option(pcb, SOF_KEEPALIVE);
    pcb->keep_idle = 10000;
    pcb->keep_intvl = 5000;
    pcb->keep_cnt = 3;
    return c;
}

static void first_message(client_t *c)
{
    /* First message immediately; restart the heartbeat from now so the
     * (possibly stale) timer does not fire a duplicate right after. */
    build();
    send_to(c);
    last_fingerprint = status_fingerprint();
    next_heartbeat = make_timeout_time_ms(API_HEARTBEAT_MS);
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    client_t *c = adopt(newpcb, false);
    if (!c) {
        printf("api: too many clients, refusing\n");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    printf("api: client connected (%d)\n", api_server_clients());
    first_message(c);
    return ERR_OK;
}

/* ---- public ----------------------------------------------------------- */

bool api_server_ws_upgrade(struct tcp_pcb *pcb, const char *key, size_t key_len)
{
    if (count(true) >= WS_MAX_CLIENTS) return false;
    char accept[WS_ACCEPT_LEN + 1], resp[160];
    ws_accept(key, key_len, accept);
    int n = snprintf(resp, sizeof resp, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (tcp_write(pcb, resp, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    client_t *c = adopt(pcb, true);         /* cannot fail: counted above, and slots are per kind */
    if (!c) return false;
    printf("api: websocket client (%d)\n", count(true));
    first_message(c);
    return true;
}

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
    printf("api: NDJSON on :%d, websocket on :%d/ws\n", API_TCP_PORT, HTTP_TCP_PORT);
    return true;
}

void api_server_poll(void)
{
    if (api_server_clients() == 0) {
        last_fingerprint = status_fingerprint();
        return;
    }
    if (!time_reached(next_allowed)) return;    /* at most one push per API_MIN_GAP_MS */
    uint32_t fp = status_fingerprint();
    bool changed = fp != last_fingerprint;
    if (changed || time_reached(next_heartbeat)) {
        last_fingerprint = fp;
        next_heartbeat = make_timeout_time_ms(API_HEARTBEAT_MS);
        next_allowed = make_timeout_time_ms(API_MIN_GAP_MS);
        push_all();
    }
}

int api_server_clients(void)
{
    return count(false) + count(true);
}
