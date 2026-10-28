/*
 * path.c - see path.h.
 */
#include "path.h"
#include "blink.h"
#include "neighbor_table.h"
#include "lldp.h"
#include "w5500_raw.h"
#include "board_config.h"
#include "settings.h"

#include "pico/time.h"

#include <stdio.h>
#include <string.h>

static uint8_t self[6];
static peer_table_t tab;
static path_test_t pt;
static uint16_t seq, token;
static bool link_up;
static uint32_t next_hello, next_echo, next_ask, next_link, last_reply_ms, link_down_at;
static int asks;
/* answering a peer's request */
static uint16_t answered_token;
static uint8_t answered_peer[6];
static uint32_t pulse_at, pulse_off_ms, last_pulse_ms;
static bool pulse_pending;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static bool after(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

static const char *macs(const uint8_t m[6])
{
    static char b[18];
    snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return b;
}

static void send_msg(peer_msg_t *m, const uint8_t dst[6])
{
    uint8_t buf[128];
    memcpy(m->dst, dst, 6);
    memcpy(m->src, self, 6);
    m->seq = ++seq;
    size_t n = peer_encode(m, buf, sizeof buf);
    if (n && w5500_ready()) w5500_send(buf, n);
}

static void send_hello(void)
{
    static const uint8_t bc[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    peer_msg_t m = {0};
    m.type = PEER_HELLO;
    m.uptime_s = now_ms() / 1000;
    w5500_link_t l;
    w5500_get_link(&l);
    m.speed = (uint8_t)l.speed;
    m.full = l.full;
    /* the first switch-like neighbor, so each end can say which port the other is on */
    for (int i = 0; i < neighbor_table_count(); i++) {
        const neighbor_entry_t *e = neighbor_table_get(i);
        if (!e) continue;
        lldp_format_chassis_id(&e->n.chassis_id, m.chassis, sizeof m.chassis);
        lldp_format_port_id(&e->n.port_id, m.port, sizeof m.port);
        if (e->n.has_pvid) m.pvid = e->n.pvid;
        break;
    }
    snprintf(m.fw, sizeof m.fw, "%s", FW_VERSION);
    m.probe = settings_get()->probe;
    send_msg(&m, bc);
}

void path_init(const uint8_t mac[6])
{
    memcpy(self, mac, 6);
    peer_table_reset(&tab);
    memset(&pt, 0, sizeof pt);
    next_hello = now_ms() + 1000;
}

const path_test_t  *path_test(void) { return &pt; }
const peer_table_t *path_peers(void) { return &tab; }
uint32_t path_generation(void) { return tab.generation * 3u + pt.generation; }
bool path_busy(void) { return pt.phase == PT_ASK || pt.phase == PT_WATCH || pt.phase == PT_RECOVER; }

static void finish(path_verdict_t v)
{
    pt.verdict = v;
    pt.phase = PT_DONE;
    pt.finished_ms = now_ms();
    pt.generation++;
    printf("path: %s to %s (link dropped %d, peer silent %d, back %d after %lu ms)\n", path_verdict_name(v), macs(pt.peer),
           pt.own_link_dropped, pt.peer_went_silent, pt.peer_came_back, (unsigned long)pt.peer_back_ms);
}

void path_link_changed(bool up)
{
    if (up == link_up) return;
    link_up = up;
    peer_census_reset(&tab);
    if (!up && !path_busy()) {
        uint32_t g = tab.generation;
        peer_table_reset(&tab);
        tab.generation = g + 1;
    }
    if (up) next_hello = now_ms() + 500;
}

bool path_start(const uint8_t *peer, char *err, size_t errcap)
{
    if (path_busy()) { snprintf(err, errcap, "A path test is already running"); return false; }
    if (blink_active()) { snprintf(err, errcap, "The port blinker is running"); return false; }
    if (!link_up) { snprintf(err, errcap, "No Ethernet link"); return false; }
    const peer_t *p = NULL;
    if (peer) {
        p = peer_find(&tab, peer);
        if (!p) { snprintf(err, errcap, "That probe is not in the peer list"); return false; }
    } else {
        int n = peer_count(&tab);
        if (n == 0) { snprintf(err, errcap, "No other probe heard on this port"); return false; }
        if (n > 1) { snprintf(err, errcap, "Several probes heard; pick one"); return false; }
        for (int i = 0; i < PEER_MAX; i++) if (tab.p[i].used) p = &tab.p[i];
    }
    uint32_t g = pt.generation;
    memset(&pt, 0, sizeof pt);
    pt.generation = g + 1;
    memcpy(pt.peer, p->mac, 6);
    pt.phase = PT_ASK;
    pt.started_ms = now_ms();
    token = (uint16_t)(token + 1 + (now_ms() & 0xff));
    asks = 0;
    link_down_at = 0;
    next_ask = now_ms();
    printf("path: test against %s\n", macs(pt.peer));
    return true;
}

static void answer_blink_req(const peer_msg_t *m)
{
    peer_msg_t a = {0};
    a.type = PEER_BLINK_ACK;
    a.token = m->token;
    bool dup = m->token == answered_token && !memcmp(answered_peer, m->src, 6);
    if (dup) { if (pulse_pending) send_msg(&a, m->src); return; }   /* resend the ACK, never pulse twice */
    uint32_t now = now_ms();
    if (path_busy() || blink_active() || pulse_pending || (last_pulse_ms && now - last_pulse_ms < 10000)) {
        printf("path: ignored blink request from %s (busy)\n", macs(m->src));
        return;
    }
    answered_token = m->token;
    memcpy(answered_peer, m->src, 6);
    send_msg(&a, m->src);
    send_msg(&a, m->src);                      /* two copies: a lost ACK costs the whole test */
    pulse_pending = true;
    pulse_off_ms = m->off_ms;
    pulse_at = now + 150;                      /* let the ACK leave before the PHY goes down */
}

bool path_handle_frame(const uint8_t *f, size_t len)
{
    if (len >= 12) peer_census(&tab, f + 6, self);
    if (!peer_is_peer(f, len)) return false;
    peer_msg_t m;
    if (!peer_parse(f, len, &m) || !memcmp(m.src, self, 6)) return true;
    bool to_us = !memcmp(m.dst, self, 6);
    uint32_t now = now_ms();
    switch (m.type) {
    case PEER_HELLO: {
        uint32_t g = tab.generation;
        peer_hello(&tab, &m, now);
        if (tab.generation != g) printf("path: probe %s (%s %s pvid %u)\n", macs(m.src), m.chassis, m.port, m.pvid);
        break;
    }
    case PEER_ECHO_REQ:
        if (to_us) { peer_msg_t r = {0}; r.type = PEER_ECHO_REP; r.t_ms = m.t_ms; send_msg(&r, m.src); }
        break;
    case PEER_ECHO_REP:
        if (!to_us) break;
        peer_echo_reply(&tab, &m, now);
        if (path_busy() && !memcmp(m.src, pt.peer, 6)) {
            if (pt.peer_went_silent && !pt.peer_came_back && pt.acked_ms) {
                pt.peer_came_back = true;
                pt.peer_back_ms = now - pt.acked_ms;
                pt.generation++;
            }
            last_reply_ms = now;
        }
        break;
    case PEER_BLINK_REQ:
        if (to_us) answer_blink_req(&m);
        break;
    case PEER_BLINK_ACK:
        if (to_us && pt.phase == PT_ASK && m.token == token && !memcmp(m.src, pt.peer, 6)) {
            pt.acked_ms = now;
            last_reply_ms = now;
            pt.phase = PT_WATCH;
            pt.generation++;
            next_echo = now;
            next_link = now;
        }
        break;
    }
    return true;
}

static void send_echo(const uint8_t dst[6])
{
    peer_msg_t e = {0};
    e.type = PEER_ECHO_REQ;
    e.t_ms = now_ms();
    send_msg(&e, dst);
}

void path_poll(void)
{
    uint32_t now = now_ms();

    if (pulse_pending && after(now, pulse_at)) {
        pulse_pending = false;
        if (blink_pulse(pulse_off_ms)) last_pulse_ms = now;
    }

    if (link_up && !blink_active() && after(now, next_hello)) {
        next_hello = now + PATH_HELLO_MS;
        send_hello();
        peer_expire(&tab, now, PATH_PEER_AGE_MS);
        /* keep RTT fresh for the page: one echo per peer per hello */
        for (int i = 0; i < PEER_MAX; i++) if (tab.p[i].used && !path_busy()) send_echo(tab.p[i].mac);
    }

    switch (pt.phase) {
    case PT_ASK:
        if (after(now, next_ask)) {
            if (asks >= 6) { finish(PATH_NO_ANSWER); return; }
            peer_msg_t r = {0};
            r.type = PEER_BLINK_REQ;
            r.token = token;
            r.off_ms = PATH_OFF_MS;
            send_msg(&r, pt.peer);
            asks++;
            next_ask = now + 300;
        }
        return;
    case PT_WATCH:
    case PT_RECOVER: {
        if (after(now, next_link)) {
            next_link = now + 50;
            w5500_link_t l;
            w5500_get_link(&l);
            if (!l.link && !link_down_at) {
                link_down_at = now;
                if (!pt.own_link_dropped) { pt.own_link_dropped = true; pt.generation++; }
            } else if (l.link && link_down_at) {
                pt.own_down_ms += now - link_down_at;
                link_down_at = 0;
            }
        }
        if (after(now, next_echo)) { next_echo = now + 200; send_echo(pt.peer); }
        uint32_t since_ack = now - pt.acked_ms;
        if (pt.phase == PT_WATCH) {
            if (!pt.peer_went_silent && since_ack <= PATH_OFF_MS + 1500 && now - last_reply_ms >= PATH_SILENT_MS) {
                pt.peer_went_silent = true;
                pt.generation++;
            }
            if (since_ack >= PATH_OFF_MS + 1500) {
                pt.verdict = path_verdict(true, pt.own_link_dropped, pt.peer_went_silent, false);
                if (pt.verdict == PATH_INCONCLUSIVE) { finish(PATH_INCONCLUSIVE); return; }
                pt.phase = PT_RECOVER;      /* verdict known; wait for the peer (or our link) to come back */
                pt.generation++;
            }
            return;
        }
        /* RECOVER */
        bool back = pt.own_link_dropped ? (!link_down_at && pt.peer_came_back) : pt.peer_came_back;
        if (back) { finish(path_verdict(true, pt.own_link_dropped, pt.peer_went_silent, true)); return; }
        if (since_ack >= PATH_RECOVER_MS) {
            if (link_down_at) { pt.own_down_ms += now - link_down_at; link_down_at = 0; }
            finish(path_verdict(true, pt.own_link_dropped, pt.peer_went_silent, false));
        }
        return;
    }
    default:
        return;
    }
}
