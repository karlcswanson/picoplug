/*
 * Host simulation of the path test (path.c) on probe A, with probe B and
 * the wire between them simulated here: direct cable, switch with an
 * edge port, switch with full spanning tree, a peer that never answers,
 * and B's side (answering a request with one link drop, never twice).
 */
#include "path.h"
#include "blink.h"
#include "neighbor_table.h"
#include "lldp.h"
#include "w5500_raw.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

uint64_t sim_us;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- stubs for what path.c calls ---- */
static bool a_link = true;
static uint8_t txq[64][128]; static size_t txlen[64]; static int ntx;
bool w5500_ready(void) { return true; }
bool w5500_send(const uint8_t *f, size_t n) { if (ntx < 64 && a_link) { memcpy(txq[ntx], f, n); txlen[ntx++] = n; } return true; }
void w5500_get_link(w5500_link_t *l) { memset(l, 0, sizeof *l); l->link = a_link; l->speed = a_link ? 100 : 0; l->full = a_link; }
static int pulses; static uint32_t pulse_off; static bool pulsing;
bool blink_pulse(uint32_t off_ms) { pulses++; pulse_off = off_ms; return true; }
bool blink_active(void) { return pulsing; }
int neighbor_table_count(void) { return 0; }
const settings_t *settings_get(void) { static settings_t s = { .probe = 2 }; return &s; }
const neighbor_entry_t *neighbor_table_get(int i) { (void)i; return NULL; }
size_t lldp_format_chassis_id(const lldp_id_t *id, char *b, size_t n) { (void)id; if (n) b[0] = 0; return 0; }
size_t lldp_format_port_id(const lldp_id_t *id, char *b, size_t n) { (void)id; if (n) b[0] = 0; return 0; }

static const uint8_t A[6] = {0x02, 0x50, 0x4c, 0, 0, 0xa};
static const uint8_t B[6] = {0x02, 0x50, 0x4c, 0, 0, 0xb};
static uint32_t now(void) { return (uint32_t)(sim_us / 1000); }

static void from_b(peer_msg_t *m, const uint8_t *dst)
{
    uint8_t buf[128];
    memcpy(m->dst, dst, 6); memcpy(m->src, B, 6);
    size_t n = peer_encode(m, buf, sizeof buf);
    path_handle_frame(buf, n);
}

typedef enum { DIRECT, SWITCH_EDGE, SWITCH_STP, DEAD } topo_t;

/* Run one test from A against simulated B. Returns the finished test. */
static const path_test_t *run(topo_t topo, uint32_t *t_end)
{
    a_link = true; ntx = 0;
    path_link_changed(false); path_link_changed(true);
    peer_msg_t h = {0}; h.type = PEER_HELLO; snprintf(h.fw, sizeof h.fw, "sim");
    from_b(&h, (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    char err[80];
    CHECK(path_start(NULL, err, sizeof err));
    CHECK(path_busy());
    int64_t b_off_at = -1, b_on_at = -1;      /* B's PHY */
    uint32_t back_delay = topo == SWITCH_STP ? 30000 : topo == SWITCH_EDGE ? 2500 : 2000;
    for (int step = 0; step < 6000 && path_busy(); step++) {
        sim_us += 10000;
        uint32_t t = now();
        bool b_phy = !(b_off_at >= 0 && t >= b_off_at && (b_on_at < 0 || t < b_on_at));
        if (b_off_at >= 0 && b_on_at < 0 && t >= b_off_at + 3000) b_on_at = t;
        /* B reachable: its PHY is up and its path to A has recovered */
        bool b_reach = topo != DEAD && b_phy && !(b_on_at >= 0 && t < b_on_at + back_delay);
        if (topo == DIRECT) a_link = b_reach;  /* the cable carries link state */
        path_poll();
        int n = ntx; ntx = 0;
        for (int i = 0; i < n; i++) {
            peer_msg_t m;
            if (!peer_parse(txq[i], txlen[i], &m) || !b_reach) continue;
            if (m.type == PEER_BLINK_REQ && b_off_at < 0) {
                peer_msg_t ack = {0}; ack.type = PEER_BLINK_ACK; ack.token = m.token;
                from_b(&ack, A);
                b_off_at = t + 150;
            } else if (m.type == PEER_ECHO_REQ) {
                peer_msg_t r = {0}; r.type = PEER_ECHO_REP; r.t_ms = m.t_ms;
                from_b(&r, A);
            }
        }
    }
    *t_end = now();
    return path_test();
}

static void t_direct(void)
{
    uint32_t te; uint32_t t0 = now();
    const path_test_t *p = run(DIRECT, &te);
    CHECK(p->phase == PT_DONE && p->verdict == PATH_DIRECT);
    CHECK(p->own_link_dropped && p->own_down_ms >= 4500 && p->own_down_ms <= 5500);
    CHECK(p->peer_came_back && te - t0 < 10000);
    printf("direct: down %u ms, back %u ms, %u ms total\n", p->own_down_ms, p->peer_back_ms, te - t0);
}

static void t_switch(topo_t topo)
{
    uint32_t te; uint32_t t0 = now();
    const path_test_t *p = run(topo, &te);
    CHECK(p->phase == PT_DONE && p->verdict == PATH_SWITCHED);
    CHECK(!p->own_link_dropped && p->peer_went_silent && p->peer_came_back);
    if (topo == SWITCH_STP) CHECK(p->peer_back_ms > 30000 && p->peer_back_ms < 35000);
    else CHECK(p->peer_back_ms > 5000 && p->peer_back_ms < 7000);
    printf("switched: back %u ms, %u ms total\n", p->peer_back_ms, te - t0);
}

static void t_dead(void)
{
    uint32_t te;
    const path_test_t *p = run(DEAD, &te);
    CHECK(p->phase == PT_DONE && p->verdict == PATH_NO_ANSWER && !p->acked_ms);
}

static void t_responder(void)
{
    /* B's side of the protocol, with this module as B: one pulse per request, duplicates only re-ACK */
    a_link = true; ntx = 0; pulses = 0;
    path_link_changed(false); path_link_changed(true);
    uint8_t buf[128];
    peer_msg_t r = {0}; r.type = PEER_BLINK_REQ; r.token = 0x77; r.off_ms = 3000;
    memcpy(r.dst, A, 6); memcpy(r.src, B, 6);     /* here A is the module, B the requester */
    size_t n = peer_encode(&r, buf, sizeof buf);
    path_handle_frame(buf, n);
    path_handle_frame(buf, n);                     /* retry of the same request */
    int acks = 0;
    for (int i = 0; i < ntx; i++) { peer_msg_t m; if (peer_parse(txq[i], txlen[i], &m) && m.type == PEER_BLINK_ACK && m.token == 0x77) acks++; }
    CHECK(acks >= 2);
    CHECK(pulses == 0);                            /* the PHY waits for the ACK to leave */
    for (int i = 0; i < 30; i++) { sim_us += 10000; path_poll(); }
    CHECK(pulses == 1 && pulse_off == 3000);
    path_handle_frame(buf, n);                     /* same token again later: no second pulse */
    r.token = 0x78; n = peer_encode(&r, buf, sizeof buf);
    path_handle_frame(buf, n);                     /* new token inside 10 s: refused */
    for (int i = 0; i < 30; i++) { sim_us += 10000; path_poll(); }
    CHECK(pulses == 1);
    sim_us += 11000000ull;
    r.token = 0x79; n = peer_encode(&r, buf, sizeof buf);
    path_handle_frame(buf, n);
    for (int i = 0; i < 30; i++) { sim_us += 10000; path_poll(); }
    CHECK(pulses == 2);
    /* echo requests are answered */
    ntx = 0;
    peer_msg_t e = {0}; e.type = PEER_ECHO_REQ; e.t_ms = 1234; memcpy(e.dst, A, 6); memcpy(e.src, B, 6);
    n = peer_encode(&e, buf, sizeof buf);
    path_handle_frame(buf, n);
    peer_msg_t m;
    CHECK(ntx == 1 && peer_parse(txq[0], txlen[0], &m) && m.type == PEER_ECHO_REP && m.t_ms == 1234 && !memcmp(m.dst, B, 6));
}

static void t_refusals(void)
{
    char err[80];
    a_link = true;
    path_link_changed(false); path_link_changed(true);
    CHECK(!path_start(NULL, err, sizeof err) && strstr(err, "No other probe"));
    path_link_changed(false);
    CHECK(!path_start(NULL, err, sizeof err) && strstr(err, "No Ethernet"));
}

int main(int argc, char **argv)
{
    sim_us = 1000000;
    path_init(A);
    const char *t = argc > 1 ? argv[1] : "";
    if (!strcmp(t, "direct")) t_direct();
    else if (!strcmp(t, "switch")) { t_switch(SWITCH_EDGE); t_switch(SWITCH_STP); }
    else if (!strcmp(t, "dead")) t_dead();
    else if (!strcmp(t, "responder")) t_responder();
    else if (!strcmp(t, "refusals")) t_refusals();
    else { t_direct(); t_switch(SWITCH_EDGE); t_switch(SWITCH_STP); t_dead(); t_responder(); t_refusals(); }
    if (fails) return 1;
    printf("ok %s\n", t);
    return 0;
}
