/* Host tests for peer.c: frame encode/decode, bounds, peer table, verdict. */
#include "peer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const uint8_t A[6] = {0x02, 0x50, 0x43, 0x00, 0x00, 0x01};
static const uint8_t B[6] = {0x02, 0x50, 0x43, 0x00, 0x00, 0x02};
static const uint8_t BC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static peer_msg_t hello_from(const uint8_t src[6], const char *chassis, uint16_t pvid)
{
    peer_msg_t m = {0};
    m.type = PEER_HELLO;
    memcpy(m.dst, BC, 6);
    memcpy(m.src, src, 6);
    m.seq = 7;
    m.uptime_s = 1234;
    m.speed = 100;
    m.full = true;
    m.pvid = pvid;
    snprintf(m.chassis, sizeof m.chassis, "%s", chassis);
    snprintf(m.port, sizeof m.port, "mgi2");
    snprintf(m.fw, sizeof m.fw, "0.3.0");
    m.probe = PEER_PROBE_NONE;
    return m;
}

static void t_roundtrip(void)
{
    uint8_t buf[128];
    peer_msg_t in = hello_from(A, "64:9d:99:32:7e:90", 109), out;
    size_t n = peer_encode(&in, buf, sizeof buf);
    CHECK(n >= 60);
    CHECK(peer_is_peer(buf, n));
    CHECK(peer_parse(buf, n, &out));
    CHECK(out.type == PEER_HELLO && out.seq == 7 && out.uptime_s == 1234 && out.speed == 100 && out.full && out.pvid == 109);
    CHECK(!strcmp(out.chassis, "64:9d:99:32:7e:90") && !strcmp(out.port, "mgi2") && !strcmp(out.fw, "0.3.0"));
    CHECK(!memcmp(out.src, A, 6) && !memcmp(out.dst, BC, 6));
    CHECK(out.probe == PEER_PROBE_NONE);
    in.probe = 0;
    n = peer_encode(&in, buf, sizeof buf);
    CHECK(peer_parse(buf, n, &out) && out.probe == 0);
    in.probe = 9;
    n = peer_encode(&in, buf, sizeof buf);
    CHECK(peer_parse(buf, n, &out) && out.probe == 9);
    /* a HELLO from older firmware ends at fw; the zero padding after it is "none" */
    in.probe = PEER_PROBE_NONE; snprintf(in.chassis, sizeof in.chassis, "x");
    n = peer_encode(&in, buf, sizeof buf);
    CHECK(n == 60 && peer_parse(buf, n, &out) && out.probe == PEER_PROBE_NONE);

    peer_msg_t e = {0};
    e.type = PEER_ECHO_REQ; memcpy(e.dst, B, 6); memcpy(e.src, A, 6); e.t_ms = 0xdeadbeef;
    n = peer_encode(&e, buf, sizeof buf);
    CHECK(n == 60);
    CHECK(peer_parse(buf, n, &out) && out.type == PEER_ECHO_REQ && out.t_ms == 0xdeadbeef);

    peer_msg_t r = {0};
    r.type = PEER_BLINK_REQ; memcpy(r.dst, B, 6); memcpy(r.src, A, 6); r.token = 0x1234; r.off_ms = 3000;
    n = peer_encode(&r, buf, sizeof buf);
    CHECK(peer_parse(buf, n, &out) && out.type == PEER_BLINK_REQ && out.token == 0x1234 && out.off_ms == 3000);
    r.type = PEER_BLINK_ACK;
    n = peer_encode(&r, buf, sizeof buf);
    CHECK(peer_parse(buf, n, &out) && out.type == PEER_BLINK_ACK && out.token == 0x1234);

    /* over-long strings are cut, not overflowed */
    peer_msg_t big = hello_from(A, "", 0);
    memset(big.chassis, 'x', sizeof big.chassis - 1); big.chassis[sizeof big.chassis - 1] = 0;
    n = peer_encode(&big, buf, sizeof buf);
    CHECK(n > 0 && peer_parse(buf, n, &out) && strlen(out.chassis) == sizeof out.chassis - 1);
    CHECK(peer_encode(&in, buf, 40) == 0);
    CHECK(!strcmp(peer_type_name(PEER_HELLO), "hello") && !strcmp(peer_type_name(99), "unknown"));
}

static void t_bounds(void)
{
    uint8_t buf[128];
    peer_msg_t in = hello_from(A, "chassis", 1), out;
    size_t n = peer_encode(&in, buf, sizeof buf);
    /* every truncation of the meaningful part must fail cleanly or parse; never read past len */
    size_t real = 14 + 8 + 8 + 1 + 7 + 1 + 4 + 1 + 5;
    for (size_t l = 0; l < real; l++) {
        uint8_t *g = malloc(l ? l : 1);
        memcpy(g, buf, l);
        CHECK(!peer_parse(g, l, &out));
        free(g);
    }
    CHECK(peer_parse(buf, real, &out));
    /* wrong EtherType, magic, version, type */
    uint8_t c[128];
    memcpy(c, buf, n); c[13] ^= 1; CHECK(!peer_is_peer(c, n));
    memcpy(c, buf, n); c[15] = 'X'; CHECK(!peer_is_peer(c, n));
    memcpy(c, buf, n); c[18] = 9;  CHECK(!peer_is_peer(c, n));
    memcpy(c, buf, n); c[19] = 77; CHECK(!peer_parse(c, n, &out));
    /* a string length pointing past the frame */
    memcpy(c, buf, n); c[30] = 200; CHECK(!peer_parse(c, n, &out));
    /* random bytes after a valid header never crash */
    srand(1);
    for (int i = 0; i < 20000; i++) {
        size_t l = (size_t)(rand() % 100);
        uint8_t *g = malloc(l ? l : 1);
        for (size_t j = 0; j < l; j++) g[j] = (uint8_t)rand();
        if (l >= 22) { memcpy(g + 12, buf + 12, 7); g[19] = (uint8_t)(1 + rand() % 5); }
        peer_parse(g, l, &out);
        free(g);
    }
    CHECK(!peer_parse(NULL, 0, &out));
}

static void t_table(void)
{
    static peer_table_t t;
    memset(&t, 0, sizeof t);
    peer_table_reset(&t);
    uint32_t g0 = t.generation;
    peer_msg_t hb = hello_from(B, "sw1", 109);
    CHECK(peer_hello(&t, &hb, 1000) != NULL);
    CHECK(peer_count(&t) == 1 && t.generation == g0 + 1);
    CHECK(peer_hello(&t, &hb, 3000) != NULL);
    CHECK(t.generation == g0 + 1);                 /* same info: no bump */
    peer_t *p = peer_find(&t, B);
    CHECK(p && p->hellos == 2 && p->first_ms == 1000 && p->last_ms == 3000);
    hb.pvid = 110;
    peer_hello(&t, &hb, 4000);
    CHECK(t.generation == g0 + 2);                 /* PVID moved: bump */

    peer_msg_t rep = {0};
    rep.type = PEER_ECHO_REP; memcpy(rep.src, B, 6); rep.t_ms = 4000;
    peer_echo_reply(&t, &rep, 4003);
    rep.t_ms = 5000; peer_echo_reply(&t, &rep, 5001);
    rep.t_ms = 6000; peer_echo_reply(&t, &rep, 6009);
    CHECK(p->echoes == 3 && p->rtt_last_ms == 9 && p->rtt_min_ms == 1);
    rep.src[5] = 9; peer_echo_reply(&t, &rep, 7000);   /* unknown peer: ignored */
    CHECK(peer_count(&t) == 1);

    peer_expire(&t, 6009 + 14000, 15000);
    CHECK(peer_count(&t) == 1);
    peer_expire(&t, 6009 + 16000, 15000);
    CHECK(peer_count(&t) == 0 && t.generation == g0 + 3);

    for (int i = 0; i < PEER_MAX + 2; i++) { peer_msg_t h = hello_from(B, "x", 1); h.src[5] = (uint8_t)(10 + i); peer_t *q = peer_hello(&t, &h, 100); CHECK((q != NULL) == (i < PEER_MAX)); }
    CHECK(peer_count(&t) == PEER_MAX);

    /* census: distinct unicast sources, not ours, capped */
    peer_census_reset(&t);
    uint8_t m[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00};
    peer_census(&t, A, A);
    peer_census(&t, BC, A);
    peer_census(&t, m, A); peer_census(&t, m, A);
    CHECK(t.nmacs == 1 && !t.macs_full);
    for (int i = 1; i < 40; i++) { m[5] = (uint8_t)i; peer_census(&t, m, A); }
    CHECK(t.nmacs == PEER_MACS_MAX && t.macs_full);
}

static void t_verdict(void)
{
    CHECK(path_verdict(false, false, false, false) == PATH_NO_ANSWER);
    CHECK(path_verdict(false, true, true, true) == PATH_NO_ANSWER);
    CHECK(path_verdict(true, true, true, true) == PATH_DIRECT);
    CHECK(path_verdict(true, true, false, false) == PATH_DIRECT);
    CHECK(path_verdict(true, false, true, true) == PATH_SWITCHED);
    CHECK(path_verdict(true, false, true, false) == PATH_SWITCHED_PEER_LOST);
    CHECK(path_verdict(true, false, false, false) == PATH_INCONCLUSIVE);
    CHECK(!strcmp(path_verdict_name(PATH_DIRECT), "direct") && !strcmp(path_verdict_name(PATH_NONE), "none"));
}

int main(int argc, char **argv)
{
    const char *t = argc > 1 ? argv[1] : "";
    if (!strcmp(t, "roundtrip")) t_roundtrip();
    else if (!strcmp(t, "bounds")) t_bounds();
    else if (!strcmp(t, "table")) t_table();
    else if (!strcmp(t, "verdict")) t_verdict();
    else { t_roundtrip(); t_bounds(); t_table(); t_verdict(); }
    if (fails) return 1;
    printf("ok %s\n", t);
    return 0;
}
