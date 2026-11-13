/*
 * http_server.c - see http_server.h.
 */
#include "http_server.h"
#include "board_config.h"
#include "status.h"
#include "usb_net.h"
#include "tool.h"
#include "blink.h"
#include "remote.h"
#include "advertise.h"
#include "eth_net.h"
#include "probe.h"
#include "neighbor_table.h"
#include "api_server.h"
#include "index_html.h"     /* generated from web/dist/index.html */
#include "icon_png.h"       /* generated from src/web/icon.png */
#include "manifest_json.h"  /* generated from src/web/manifest.json */

#include "lwip/tcp.h"
#include "pico/time.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define REQ_MAX 3072        /* a hex frame body for POST /inject */

typedef struct {
    struct tcp_pcb *pcb;
    char     req[REQ_MAX];
    uint16_t req_len;
    bool     responding;
    bool     lan;                  /* arrived on the Ethernet side: read-only paths only */
    /* response: header (RAM, small) then body (flash or this client's buffer) */
    char     hdr[256];
    const char *hdr_ptr; size_t hdr_left;
    const char *body_ptr; size_t body_left;
    bool     body_is_ram;          /* body lives in json[] -> must copy */
    size_t   unacked;
    char     json[STATUS_JSON_MAX];
    absolute_time_t deadline;
    uint32_t age_ms;               /* UINT32_MAX = no X-Age-Ms header */
} conn_t;

#include "app.h"
#include "settings.h"
#include "query.h"
#include "usb_net.h"
#include "probe.h"

static conn_t conns[HTTP_MAX_CLIENTS];

static struct tcp_pcb *listen_pcb;
static uint32_t seq;
static absolute_time_t reboot_at;
static bool reboot_pending, reboot_bootsel;

static void conn_close(conn_t *c)
{
    if (!c->pcb) return;
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_sent(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_poll(c->pcb, NULL, 0);
    if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    c->pcb = NULL;
}

/* Push as much of the pending response as lwIP will take right now. */
static void pump(conn_t *c)
{
    if (!c->pcb) return;
    while (c->hdr_left || c->body_left) {
        const char **ptr = c->hdr_left ? &c->hdr_ptr : &c->body_ptr;
        size_t *left = c->hdr_left ? &c->hdr_left : &c->body_left;
        u8_t flags = (c->hdr_left || c->body_is_ram) ? TCP_WRITE_FLAG_COPY : 0;
        u16_t room = tcp_sndbuf(c->pcb);
        if (room == 0) break;
        u16_t n = *left > room ? room : (u16_t)*left;
        err_t e = tcp_write(c->pcb, *ptr, n, flags | (*left > n ? TCP_WRITE_FLAG_MORE : 0));
        if (e == ERR_MEM) break;
        if (e != ERR_OK) { conn_close(c); return; }
        *ptr += n;
        *left -= n;
        c->unacked += n;
    }
    tcp_output(c->pcb);
    if (!c->hdr_left && !c->body_left && c->unacked == 0) conn_close(c);
}

static void respond(conn_t *c, const char *status, const char *ctype,
                    const char *body, size_t body_len, bool body_is_ram);


/* Case-insensitive header lookup; returns pointer to value, sets len. */
static const char *find_header(const char *req, const char *name, size_t *len)
{
    size_t nl = strlen(name);
    const char *p = req;
    /* req may start inside the request line (possibly right at a NUL that
     * replaced a space); skip to the end of that line first. */
    p = req;
    if (!*p) p++;                             /* called at the NUL that replaced the space */
    while ((p = strchr(p, '\n')) != NULL) {
        p++;
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            const char *v = p + nl + 1;
            while (*v == ' ') v++;
            const char *e = v;
            while (*e && *e != '\r' && *e != '\n') e++;
            *len = (size_t)(e - v);
            return v;
        }
    }
    return NULL;
}


static void respond(conn_t *c, const char *status, const char *ctype,
                    const char *body, size_t body_len, bool body_is_ram)
{
    char age[32] = "";
    if (c->age_ms != UINT32_MAX) snprintf(age, sizeof age, "X-Age-Ms: %lu\r\n", (unsigned long)c->age_ms);
    int n = snprintf(c->hdr, sizeof c->hdr,
                     "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                     "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n%s"
                     "Connection: close\r\n\r\n",
                     status, ctype, (unsigned)body_len, age);
    if (n < 0 || (size_t)n >= sizeof c->hdr) n = (int)sizeof c->hdr - 1;   /* cannot happen: longest is ~185 */
    c->hdr_ptr = c->hdr;
    c->hdr_left = (size_t)n;
    c->body_ptr = body;
    c->body_left = body_len;
    c->body_is_ram = body_is_ram;
    c->unacked = 0;
    c->responding = true;
    pump(c);
}

/* "GET /ws" or "GET /ws?..." */
static bool is_ws_path(const char *req)
{
    return strncmp(req, "GET /ws", 7) == 0 && (req[7] == ' ' || req[7] == '?');
}

/* GET /ws with an Upgrade: websocket header: hand the connection to the status stream
 * (api_server.c), which pushes every status as a text frame. Read-only, so both sides get it. */
static void ws_upgrade(conn_t *c)
{
    size_t ul = 0, kl = 0;
    const char *up = find_header(c->req, "Upgrade", &ul);
    const char *key = find_header(c->req, "Sec-WebSocket-Key", &kl);
    if (!up || ul != 9 || strncasecmp(up, "websocket", 9) != 0 || !key || kl == 0 || kl > 64) {
        static const char b[] = "websocket upgrade expected\n";
        respond(c, "400 Bad Request", "text/plain", b, sizeof b - 1, false);
        return;
    }
    struct tcp_pcb *pcb = c->pcb;
    if (!api_server_ws_upgrade(pcb, key, kl)) {
        static const char b[] = "too many websocket clients\n";
        respond(c, "503 Service Unavailable", "text/plain", b, sizeof b - 1, false);
        return;
    }
    c->pcb = NULL;                  /* api_server owns it now; this slot is free */
}

static void handle_request(conn_t *c)
{
    if (is_ws_path(c->req)) { ws_upgrade(c); return; }
    /* "GET <path> HTTP/1.x" */
    char *path = NULL;
    const char *query = "";
    bool is_post = strncmp(c->req, "POST ", 5) == 0;
    if (strncmp(c->req, "GET ", 4) == 0 || is_post) {
        path = c->req + (is_post ? 5 : 4);
        char *sp = strchr(path, ' ');
        if (sp) *sp = '\0';
        char *q = strchr(path, '?');
        if (q) { *q = '\0'; query = q + 1; }
    }
    if (!path) {
        static const char b[] = "method not allowed\n";
        respond(c, "405 Method Not Allowed", "text/plain", b, sizeof b - 1, false);
    } else if (c->lan && !(strcmp(path, "/") == 0 || strcmp(path, "/status.json") == 0 || strcmp(path, "/frame") == 0 ||
                           strcmp(path, "/icon.png") == 0 || strcmp(path, "/apple-touch-icon.png") == 0 ||
                           strcmp(path, "/favicon.ico") == 0 || strcmp(path, "/manifest.json") == 0)) {
        /* Ethernet side is read-only: no settings, tools, tx, inject or reboot */
        static const char b[] = "read-only from the port side; use the USB link\n";
        respond(c, "403 Forbidden", "text/plain", b, sizeof b - 1, false);
    } else if (is_post && strcmp(path, "/inject") == 0) {
        /* Debug: body is a hex dump of one Ethernet frame (whitespace
         * ignored); it is handled exactly as if the W5500 had received it. */
        const char *hdrs = c->req + strlen(c->req);
        const char *body = strstr(hdrs + 1, "\r\n\r\n");
        static uint8_t frame[1536];
        size_t n = 0;
        int hi = -1;
        if (body) {
            for (const char *q = body + 4; *q && n < sizeof frame; q++) {
                int d = (*q >= '0' && *q <= '9') ? *q - '0' : (*q >= 'a' && *q <= 'f') ? *q - 'a' + 10 :
                        (*q >= 'A' && *q <= 'F') ? *q - 'A' + 10 : -1;
                if (d < 0) continue;
                if (hi < 0) hi = d; else { frame[n++] = (uint8_t)(hi << 4 | d); hi = -1; }
            }
        }
        if (n >= 14) app_handle_frame(frame, n);
        int len = snprintf(c->json, sizeof c->json, "{\"injected_bytes\":%u,\"ok\":%s}\n", (unsigned)n, n >= 14 ? "true" : "false");
        respond(c, n >= 14 ? "200 OK" : "400 Bad Request", "application/json", c->json, (size_t)len, true);
    } else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        respond(c, "200 OK", "text/html; charset=utf-8", (const char *)index_html, index_html_len, false);
    } else if (strcmp(path, "/icon.png") == 0 || strcmp(path, "/apple-touch-icon.png") == 0 || strcmp(path, "/favicon.ico") == 0) {
        respond(c, "200 OK", "image/png", (const char *)icon_png, icon_png_len, false);
    } else if (strcmp(path, "/frame") == 0) {
        /* GET /frame?lldp | ?cdp | ?bpdu | ?lacp -> the last such frame as a hex dump (feed to text2pcap/tcpdump) */
        int kind = strcmp(query, "cdp") == 0 ? 1 : strcmp(query, "bpdu") == 0 ? 2 : strcmp(query, "lacp") == 0 ? 3 : 0;
        const uint8_t *f; size_t n = app_last_frame(kind, &f);
        static const char hex[] = "0123456789abcdef";
        size_t o = 0;
        for (size_t i = 0; i < n && o + 4 < sizeof c->json; i++) {
            c->json[o++] = hex[f[i] >> 4]; c->json[o++] = hex[f[i] & 15];
            c->json[o++] = (i % 16 == 15 || i + 1 == n) ? '\n' : ' ';
        }
        respond(c, n ? "200 OK" : "404 Not Found", "text/plain", c->json, o, true);
    } else if (strcmp(path, "/manifest.json") == 0) {
        respond(c, "200 OK", "application/manifest+json", (const char *)manifest_json, manifest_json_len, false);
    } else if (strcmp(path, "/tx") == 0) {
        /* GET /tx?lldp=0|1&cdp=0|1 -> set which protocols we advertise, reply with the tx state
         * GET /tx                   -> read only */
        advertise_state_t a;
        advertise_get_state(&a);
        bool ok = true, lldp = a.lldp_enabled, cdp = a.cdp_enabled, changed = false;
        const char *q = query, *k, *v; size_t kl, vl;
        while (query_next(&q, &k, &kl, &v, &vl)) {
            if (query_key_is(k, kl, "lldp")) { lldp = vl == 1 && v[0] == '1'; changed = true; }
            else if (query_key_is(k, kl, "cdp")) { cdp = vl == 1 && v[0] == '1'; changed = true; }
            else ok = false;
        }
        if (ok && changed) {
            advertise_set_enabled(lldp, cdp);
            settings_t s = *settings_get();
            if (s.lldp_on != lldp || s.cdp_on != cdp) { s.lldp_on = lldp; s.cdp_on = cdp; settings_save(&s); }
        }
        advertise_get_state(&a);
        int n = snprintf(c->json, sizeof c->json,
                         "{\"ok\":%s,\"lldp\":%s,\"cdp\":%s,\"cdp_peer\":%s,\"lldp_frames\":%lu,\"cdp_frames\":%lu}\n",
                         ok ? "true" : "false", a.lldp_enabled ? "true" : "false", a.cdp_enabled ? "true" : "false",
                         a.cdp_peer ? "true" : "false",
                         (unsigned long)a.lldp_frames, (unsigned long)a.cdp_frames);
        respond(c, ok ? "200 OK" : "400 Bad Request", "application/json", c->json, (size_t)n, true);
    } else if (strcmp(path, "/tool") == 0) {
        /* GET /tool?ping=HOST[&count=N] (N 0 = continuous) | ?trace=HOST | ?stop=1 | ?blink=1|0 | (none: state).
         * ?dhcp=1 restarts DHCP; ?relisten=1 clears the neighbor table.
         * Results stream in /status.json "tool" and "blink". */
        char host[TOOL_HOST_MAX + 1] = "", err[80] = "";
        tool_kind_t kind = TOOL_IDLE; bool stop = false, ok = true; unsigned count = 5; int blink = -1;
        bool dhcp_retry = false, relisten = false;
        const char *q = query, *k, *v; size_t kl, vl;
        while (query_next(&q, &k, &kl, &v, &vl)) {
            if (query_key_is(k, kl, "ping") || query_key_is(k, kl, "trace")) {
                kind = kl == 4 ? TOOL_PING : TOOL_TRACE;
                if (!query_decode(v, vl, host, sizeof host)) { ok = false; snprintf(err, sizeof err, "host too long or badly encoded"); }
            } else if (query_key_is(k, kl, "count")) { char cv[8]; count = query_decode(v, vl, cv, sizeof cv) ? (unsigned)atoi(cv) : 999; }
            else if (query_key_is(k, kl, "stop")) stop = true;
            else if (query_key_is(k, kl, "blink")) blink = vl && v[0] == '1';
            else if (query_key_is(k, kl, "dhcp")) dhcp_retry = vl && v[0] == '1';
            else if (query_key_is(k, kl, "relisten")) relisten = vl && v[0] == '1';
            else { ok = false; snprintf(err, sizeof err, "unknown parameter"); }
        }
        if (ok && blink == 1) ok = blink_start(err, sizeof err);
        else if (ok && blink == 0) blink_stop();
        if (ok && dhcp_retry) { eth_net_dhcp_retry(); probe_restart(); }
        if (ok && relisten) { int n = neighbor_table_clear(); printf("lldp: cleared %d neighbor%s on request\n", n, n == 1 ? "" : "s"); }
        if (ok && stop) tool_stop();
        else if (ok && kind != TOOL_IDLE) {
            if (tool_get()->running) { ok = false; snprintf(err, sizeof err, "a tool is already running; stop it first"); }
            else ok = tool_start(kind, host, (uint16_t)count, err, sizeof err);
        }
        const tool_state_t *t = tool_get();
        int n = snprintf(c->json, sizeof c->json, "{\"ok\":%s,\"error\":\"%s\",\"kind\":\"%s\",\"running\":%s,\"blink\":%s}\n",
                         ok ? "true" : "false", err, t->kind == TOOL_PING ? "ping" : t->kind == TOOL_TRACE ? "trace" : "idle", t->running ? "true" : "false",
                         blink_active() ? "true" : "false");
        respond(c, ok ? "200 OK" : "400 Bad Request", "application/json", c->json, (size_t)n, true);
    } else if (strcmp(path, "/settings") == 0) {
        /* GET /settings                       -> current values and defaults
         * GET /settings?ping_target=&dns_name=&dns_server=[&lldp=&cdp=&probe=&form_url=]  -> validate, save to flash, rerun the probe
         * GET /settings?reset=1               -> back to defaults */
        settings_t s = *settings_get(), d;
        settings_defaults(&d);
        uint8_t old_probe = s.probe;
        char err[64] = "";
        bool changed = false, ok = settings_apply_query(&s, query, err, sizeof err, &changed);
        if (ok && changed) {
            ok = settings_save(&s);
            if (!ok) snprintf(err, sizeof err, "flash write failed");
            else {
                advertise_set_enabled(s.lldp_on, s.cdp_on);
                if (s.probe != old_probe) { advertise_name_changed(); remote_name_changed(); }
                probe_restart();
            }
            /* usb_ip applies at the next boot; the page offers a reboot */
        }
        const settings_t *c2 = settings_get();
        char probe_s[5] = "null";
        if (c2->probe <= 9) { probe_s[0] = (char)('0' + c2->probe); probe_s[1] = '\0'; }
        int n = snprintf(c->json, sizeof c->json,
                         "{\"ok\":%s,\"error\":\"%s\",\"saved\":%s,\"ping_target\":\"%s\",\"dns_name\":\"%s\",\"dns_server\":\"%s\","
                         "\"lldp\":%s,\"cdp\":%s,\"probe\":%s,\"usb_ip\":\"%s\",\"running_ip\":\"%s\",\"reboot_needed\":%s,\"form_url\":\"%s\","
                         "\"defaults\":{\"ping_target\":\"%s\",\"dns_name\":\"%s\",\"dns_server\":\"\",\"usb_ip\":\"%s\",\"form_url\":\"%s\"}}\n",
                         ok ? "true" : "false", err, ok && changed ? "true" : "false",
                         c2->ping_target, c2->dns_name, c2->dns_server, c2->lldp_on ? "true" : "false", c2->cdp_on ? "true" : "false", probe_s,
                         c2->usb_ip, usb_net_ip_str(), strcmp(c2->usb_ip, usb_net_ip_str()) ? "true" : "false", c2->form_url,
                         d.ping_target, d.dns_name, d.usb_ip, d.form_url);   /* form_url is validated JSON-safe */
        respond(c, ok ? "200 OK" : "400 Bad Request", "application/json", c->json, (size_t)n, true);
    } else if (strcmp(path, "/reboot") == 0) {
        /* GET /reboot           -> restart firmware
         * GET /reboot?bootsel=1 -> restart into the USB bootloader (RPI-RP2 volume) */
        char bv[4]; reboot_bootsel = query_get(query, "bootsel", bv, sizeof bv) == 1 && bv[0] == '1';
        reboot_pending = true;
        reboot_at = make_timeout_time_ms(300);     /* let the reply go out first */
        int n = snprintf(c->json, sizeof c->json, "{\"rebooting\":true,\"bootsel\":%s}\n", reboot_bootsel ? "true" : "false");
        respond(c, "200 OK", "application/json", c->json, (size_t)n, true);
    } else if (strcmp(path, "/peer") == 0) {
        /* GET /peer?scan=1     -> ask the network for other testers (mDNS, rate limited); they show in "peers"
         * GET /peer?name=NAME  -> that tester's /status.json as last fetched; each call refreshes it.
         *                         202 while the first copy is on its way, 404 for a name not heard,
         *                         502 when it could not be read. X-Age-Ms says how old the copy is. */
        char nm[PEERS_NAME_MAX + 1] = "", sv[4] = "";
        if (query_get(query, "scan", sv, sizeof sv) == 1 && sv[0] == '1') {
            remote_scan();
            static const char b[] = "{\"ok\":true}\n";
            respond(c, "200 OK", "application/json", b, sizeof b - 1, false);
        } else if (query_get(query, "name", nm, sizeof nm) == 1 && nm[0]) {
            size_t len; uint32_t age; char err[48];
            remote_view_t r = remote_view(nm, c->json, sizeof c->json, &len, &age, err, sizeof err);
            if (r == REMOTE_OK) {
                c->age_ms = age;
                respond(c, "200 OK", "application/json", c->json, len, true);
            } else {
                int n = snprintf(c->json, sizeof c->json, "{\"ok\":false,\"pending\":%s,\"error\":\"%s\"}\n",
                                 r == REMOTE_PENDING ? "true" : "false", err);
                respond(c, r == REMOTE_PENDING ? "202 Accepted" : r == REMOTE_UNKNOWN ? "404 Not Found" : "502 Bad Gateway",
                        "application/json", c->json, (size_t)n, true);
            }
        } else {
            static const char b[] = "{\"ok\":false,\"error\":\"scan=1 or name=NAME\"}\n";
            respond(c, "400 Bad Request", "application/json", b, sizeof b - 1, false);
        }
    } else if (strcmp(path, "/status.json") == 0) {
        size_t len = status_render(c->json, sizeof c->json, ++seq);
        if (len) respond(c, "200 OK", "application/json", c->json, len, true);
        else {
            static const char b[] = "status too large\n";
            respond(c, "500 Internal Server Error", "text/plain", b, sizeof b - 1, false);
        }
    } else {
        static const char b[] = "not found\n";
        respond(c, "404 Not Found", "text/plain", b, sizeof b - 1, false);
    }
}

/* ---- lwIP callbacks --------------------------------------------------- */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    conn_t *c = arg;
    if (!p) { conn_close(c); return ERR_OK; }
    if (err != ERR_OK) { pbuf_free(p); return err; }
    tcp_recved(pcb, p->tot_len);

    if (!c->responding) {
        size_t room = REQ_MAX - 1 - c->req_len;
        size_t n = p->tot_len < room ? p->tot_len : room;
        pbuf_copy_partial(p, c->req + c->req_len, (u16_t)n, 0);
        c->req_len += (uint16_t)n;
        c->req[c->req_len] = '\0';
        bool have_line = strchr(c->req, '\n') != NULL;
        const char *hend = strstr(c->req, "\r\n\r\n");
        bool have_all = hend != NULL || c->req_len >= REQ_MAX - 1;
        /* A POST needs its body, a WebSocket upgrade its headers; everything else can be
         * answered from the request line alone. */
        bool is_post = strncmp(c->req, "POST ", 5) == 0;
        if (is_ws_path(c->req) && !hend && c->req_len < REQ_MAX - 1) have_line = false;
        if (is_post && hend) {
            size_t clen = 0, hl;
            const char *cl = find_header(c->req, "Content-Length", &hl);
            if (cl) clen = (size_t)atoi(cl);
            size_t have = c->req_len - (size_t)(hend + 4 - c->req);
            if (have < clen && c->req_len < REQ_MAX - 1) have_all = false;
        }
        if (have_all || (have_line && !is_post)) handle_request(c);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb;
    conn_t *c = arg;
    if (!c) return ERR_OK;
    c->unacked = len > c->unacked ? 0 : c->unacked - len;
    pump(c);
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    (void)err;
    conn_t *c = arg;
    if (c) c->pcb = NULL;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)pcb;
    conn_t *c = arg;
    if (!c) return ERR_OK;
    if (time_reached(c->deadline)) {
        conn_close(c);            /* slow or idle client */
    } else if (c->responding) {
        pump(c);
    }
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    /* USB or the port: by the address it came in on. The USB side has IPv4 and an IPv6
     * link-local address (iOS reaches picoplug.local over the latter). */
    const struct netif *un = usb_net_netif();
    bool lan = true;
    if (un && IP_IS_V4(&newpcb->local_ip)) lan = !ip_addr_cmp(&newpcb->local_ip, &un->ip_addr);
#if LWIP_IPV6
    else if (un && IP_IS_V6(&newpcb->local_ip))
        for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
            if (ip6_addr_isvalid(netif_ip6_addr_state(un, i)) && ip6_addr_cmp_zoneless(ip_2_ip6(&newpcb->local_ip), netif_ip6_addr(un, i)))
                lan = false;
#endif
    conn_t *c = NULL;
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) if (!conns[i].pcb) { c = &conns[i]; break; }
    if (!c) { tcp_abort(newpcb); return ERR_ABRT; }
    c->pcb = newpcb;
    c->req_len = 0;
    c->req[0] = '\0';
    c->responding = false;
    c->lan = lan;
    c->hdr_left = c->body_left = c->unacked = 0;
    c->deadline = make_timeout_time_ms(10000);
    c->age_ms = UINT32_MAX;
    tcp_arg(newpcb, c);
    tcp_recv(newpcb, on_recv);
    tcp_sent(newpcb, on_sent);
    tcp_err(newpcb, on_err);
    tcp_poll(newpcb, on_poll, 2);     /* every 1 s */
    tcp_nagle_disable(newpcb);
    return ERR_OK;
}

void http_server_poll(void)
{
    if (reboot_pending && time_reached(reboot_at)) {
        printf("http: rebooting%s\n", reboot_bootsel ? " into BOOTSEL" : "");
        watchdog_hw->scratch[1] = 0;            /* intentional: restart the wd_resets counter */
        if (reboot_bootsel) reset_usb_boot(0, 0);
        watchdog_reboot(0, 0, 0);
        while (1) tight_loop_contents();
    }
}

bool http_server_init(void)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);     /* IPv6 link-local too, for other testers */
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_TCP_PORT) != ERR_OK) { tcp_abort(pcb); return false; }
    /* Listens on both interfaces; handle_request limits Ethernet-side connections to read-only paths. */
    listen_pcb = tcp_listen_with_backlog(pcb, 2);
    if (!listen_pcb) { tcp_abort(pcb); return false; }
    tcp_accept(listen_pcb, on_accept);
    printf("http: viewer on :%d (%u byte page)\n", HTTP_TCP_PORT, (unsigned)index_html_len);
    return true;
}
