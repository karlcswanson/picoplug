/*
 * Host-side tests for lacp.c: a real-shaped LACPDU (fast, active, partner
 * defaulted), tagged, marker subtype rejected, truncation, and the tracker.
 */
#include "lacp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)
#define CHECK_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", \
        __FILE__, __LINE__, #a, _a, #b, _b); return 1; } \
} while (0)

static const uint8_t SLOW[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x02};
static const uint8_t SW[6] = {0x64, 0x9D, 0x99, 0x32, 0x7E, 0x97};

static size_t lacpdu(uint8_t *f, bool tagged, uint8_t subtype, uint8_t actor_state)
{
    size_t n = 0;
    memset(f, 0, 160);
    memcpy(f, SLOW, 6); memcpy(f + 6, SW, 6); n = 12;
    if (tagged) { f[n++] = 0x81; f[n++] = 0x00; f[n++] = 0x00; f[n++] = 0x01; }
    f[n++] = 0x88; f[n++] = 0x09; f[n++] = subtype; f[n++] = 1;
    /* actor */
    f[n++] = 1; f[n++] = 20;
    f[n++] = 0x80; f[n++] = 0x00; memcpy(f + n, SW, 6); n += 6;
    f[n++] = 0x00; f[n++] = 0x02;            /* key 2 */
    f[n++] = 0x80; f[n++] = 0x00;            /* port priority */
    f[n++] = 0x00; f[n++] = 0x07;            /* port 7 */
    f[n++] = actor_state; n += 3;
    /* partner: nothing known */
    f[n++] = 2; f[n++] = 20; n += 14; f[n++] = 0; n += 3;
    /* collector + terminator */
    f[n++] = 3; f[n++] = 16; n += 14;
    f[n++] = 0; f[n++] = 0;
    return n + 50;                            /* reserved padding to 110 B of payload */
}

static int test_parse(void)
{
    uint8_t f[160]; lacp_pdu_t p;
    uint8_t st = LACP_ST_ACTIVE | LACP_ST_FAST | LACP_ST_AGGREGATABLE | LACP_ST_DEFAULTED;
    size_t n = lacpdu(f, false, 1, st);
    CHECK(lacp_is_lacp(f, n));
    CHECK(lacp_parse(f, n, &p));
    CHECK_EQ(p.version, 1);
    CHECK_EQ(p.actor.sys_priority, 32768); CHECK(!memcmp(p.actor.sys_mac, SW, 6));
    CHECK_EQ(p.actor.key, 2); CHECK_EQ(p.actor.port, 7); CHECK_EQ(p.actor.port_priority, 32768);
    CHECK_EQ(p.actor.state, st);
    CHECK_EQ(p.partner.port, 0); CHECK_EQ(p.partner.state, 0);
    n = lacpdu(f, true, 1, st);
    CHECK(lacp_parse(f, n, &p)); CHECK_EQ(p.actor.port, 7);
    n = lacpdu(f, false, 2, st);              /* marker protocol, not LACP */
    CHECK(!lacp_is_lacp(f, n)); CHECK(!lacp_parse(f, n, &p));
    return 0;
}

static int test_bounds(void)
{
    uint8_t f[160]; lacp_pdu_t p;
    size_t n = lacpdu(f, false, 1, LACP_ST_ACTIVE);
    for (size_t cut = 0; cut < 14 + 2 + 40; cut++) {
        uint8_t *g = malloc(cut ? cut : 1);
        memcpy(g, f, cut);
        CHECK(!lacp_parse(g, cut, &p));
        free(g);
    }
    CHECK(lacp_parse(f, 14 + 2 + 40, &p));
    f[16] = 9;                                /* bad actor TLV type */
    CHECK(!lacp_parse(f, n, &p));
    CHECK(!lacp_parse(NULL, 100, &p));
    return 0;
}

static int test_tracker(void)
{
    uint8_t f[160]; lacp_pdu_t p; lacp_state_t s;
    lacp_state_reset(&s);
    CHECK(!s.seen);
    size_t n = lacpdu(f, false, 1, LACP_ST_ACTIVE | LACP_ST_FAST | LACP_ST_AGGREGATABLE | LACP_ST_DEFAULTED);
    CHECK(lacp_parse(f, n, &p));
    lacp_state_update(&s, &p, 1000);
    CHECK(s.seen); CHECK_EQ(s.count, 1); CHECK_EQ(s.generation, 1); CHECK_EQ(s.interval_ms, 0);
    lacp_state_update(&s, &p, 2000);             /* same content: no generation bump */
    CHECK_EQ(s.count, 2); CHECK_EQ(s.generation, 1); CHECK_EQ(s.interval_ms, 1000); CHECK_EQ(s.changes, 0);
    n = lacpdu(f, false, 1, LACP_ST_ACTIVE | LACP_ST_AGGREGATABLE | LACP_ST_DEFAULTED);   /* went slow */
    CHECK(lacp_parse(f, n, &p));
    lacp_state_update(&s, &p, 32000);
    CHECK_EQ(s.generation, 2); CHECK_EQ(s.changes, 1); CHECK_EQ(s.interval_ms, 30000); CHECK_EQ(s.first_ms, 1000);
    lacp_state_reset(&s);
    CHECK(!s.seen); CHECK_EQ(s.generation, 0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s parse|bounds|tracker\n", argv[0]); return 2; }
    if (!strcmp(argv[1], "parse")) return test_parse();
    if (!strcmp(argv[1], "bounds")) return test_bounds();
    if (!strcmp(argv[1], "tracker")) return test_tracker();
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
