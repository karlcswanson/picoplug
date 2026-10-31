/*
 * Host-side tests for eapol.c: hand-built EAPOL frames (EAP Request/Identity,
 * Failure, Start, tagged), truncation, and the tracker.
 */
#include "eapol.h"

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
#define CHECK_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", \
        __FILE__, __LINE__, #a, (a), (b)); return 1; } \
} while (0)

static const uint8_t PAE[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x03};
static const uint8_t SW[6] = {0x64, 0x9D, 0x99, 0x32, 0x7E, 0x90};

/* Ethernet + optional 802.1Q tag + EAPOL header + body */
static size_t frame(uint8_t *f, bool tagged, uint8_t ver, uint8_t type, const uint8_t *body, size_t blen)
{
    size_t n = 0;
    memcpy(f, PAE, 6); memcpy(f + 6, SW, 6); n = 12;
    if (tagged) { f[n++] = 0x81; f[n++] = 0x00; f[n++] = 0x00; f[n++] = 0x0A; }
    f[n++] = 0x88; f[n++] = 0x8E;
    f[n++] = ver; f[n++] = type; f[n++] = (uint8_t)(blen >> 8); f[n++] = (uint8_t)blen;
    if (blen) memcpy(f + n, body, blen);
    n += blen;
    return n;
}

static int test_parse(void)
{
    uint8_t f[128];
    eapol_pkt_t p;
    /* EAP Request id 1, len 5, type Identity */
    const uint8_t req_id[] = {EAP_CODE_REQUEST, 1, 0, 5, 1};
    size_t n = frame(f, false, 2, EAPOL_TYPE_EAP, req_id, sizeof req_id);
    CHECK(eapol_is_eapol(f, n));
    CHECK(eapol_parse(f, n, &p));
    CHECK_EQ(p.version, 2); CHECK_EQ(p.type, EAPOL_TYPE_EAP); CHECK_EQ(p.body_len, 5);
    CHECK_EQ(p.eap_code, EAP_CODE_REQUEST); CHECK_EQ(p.eap_id, 1); CHECK(p.has_eap_type); CHECK_EQ(p.eap_type, 1);
    CHECK(memcmp(p.src_mac, SW, 6) == 0);
    CHECK_STR(eap_type_name(p.eap_type), "identity");
    CHECK_STR(eap_code_name(p.eap_code), "request");
    CHECK_STR(eapol_type_name(p.type), "eap");

    /* same, behind a VLAN tag */
    n = frame(f, true, 1, EAPOL_TYPE_EAP, req_id, sizeof req_id);
    CHECK(eapol_is_eapol(f, n));
    CHECK(eapol_parse(f, n, &p));
    CHECK_EQ(p.eap_type, 1); CHECK_EQ(p.version, 1);

    /* EAP Failure: no type byte */
    const uint8_t fail[] = {EAP_CODE_FAILURE, 7, 0, 4};
    n = frame(f, false, 2, EAPOL_TYPE_EAP, fail, sizeof fail);
    CHECK(eapol_parse(f, n, &p));
    CHECK_EQ(p.eap_code, EAP_CODE_FAILURE); CHECK(!p.has_eap_type);

    /* EAPOL-Start: no body */
    n = frame(f, false, 1, EAPOL_TYPE_START, NULL, 0);
    CHECK(eapol_parse(f, n, &p));
    CHECK_EQ(p.type, EAPOL_TYPE_START); CHECK_EQ(p.eap_code, 0);
    CHECK_STR(eapol_type_name(p.type), "start");

    /* Request/PEAP */
    const uint8_t peap[] = {EAP_CODE_REQUEST, 2, 0, 6, 25, 0x20};
    n = frame(f, false, 2, EAPOL_TYPE_EAP, peap, sizeof peap);
    CHECK(eapol_parse(f, n, &p));
    CHECK_STR(eap_type_name(p.eap_type), "peap");

    /* not EAPOL: LLDP ethertype */
    n = frame(f, false, 2, EAPOL_TYPE_EAP, req_id, sizeof req_id);
    f[12] = 0x88; f[13] = 0xCC;
    CHECK(!eapol_is_eapol(f, n));
    CHECK(!eapol_parse(f, n, &p));
    CHECK(!eapol_is_eapol(NULL, 60));
    CHECK(!eapol_parse(f, n, NULL));
    return 0;
}

/* Every prefix of a valid frame must be rejected or parsed without reading past the end.
 * The buffer sits right before a PROT_NONE page so an over-read faults. */
#include <sys/mman.h>
#include <unistd.h>
static int test_bounds(void)
{
    uint8_t full[64];
    const uint8_t req_id[] = {EAP_CODE_REQUEST, 1, 0, 5, 1};
    size_t n = frame(full, true, 2, EAPOL_TYPE_EAP, req_id, sizeof req_id);
    long pg = sysconf(_SC_PAGESIZE);
    uint8_t *region = mmap(NULL, (size_t)pg * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(region != MAP_FAILED);
    CHECK(mprotect(region + pg, (size_t)pg, PROT_NONE) == 0);
    eapol_pkt_t p;
    int parsed = 0;
    for (size_t len = 0; len <= n; len++) {
        uint8_t *f = region + pg - len;
        memcpy(f, full, len);
        if (eapol_parse(f, len, &p)) parsed++;
    }
    CHECK(parsed >= 1);                          /* the full frame and the 4-byte-body prefixes */
    CHECK(!eapol_parse(region + pg - 22, 22, &p)); /* tagged header only, EAP body missing */
    munmap(region, (size_t)pg * 2);
    return 0;
}

static int test_tracker(void)
{
    uint8_t f[128];
    eapol_pkt_t p;
    eapol_state_t s; eapol_state_reset(&s);
    CHECK(!s.seen);
    const uint8_t req_id[] = {EAP_CODE_REQUEST, 1, 0, 5, 1};
    size_t n = frame(f, false, 2, EAPOL_TYPE_EAP, req_id, sizeof req_id);
    CHECK(eapol_parse(f, n, &p));
    eapol_state_update(&s, &p, 1000);
    CHECK(s.seen); CHECK_EQ(s.requests, 1); CHECK_EQ(s.first_ms, 1000); CHECK_EQ(s.last_ms, 1000);
    CHECK_EQ(s.interval_ms, 0); CHECK_EQ(s.generation, 1); CHECK_EQ(s.eap_type, 1); CHECK_EQ(s.version, 2);
    CHECK(memcmp(s.authenticator, SW, 6) == 0);
    eapol_state_update(&s, &p, 31000);
    CHECK_EQ(s.requests, 2); CHECK_EQ(s.interval_ms, 30000); CHECK_EQ(s.generation, 2);
    /* a Start from another client is not the port talking */
    n = frame(f, false, 1, EAPOL_TYPE_START, NULL, 0);
    CHECK(eapol_parse(f, n, &p));
    eapol_state_update(&s, &p, 32000);
    CHECK_EQ(s.requests, 2); CHECK_EQ(s.generation, 2);
    /* a Response (another supplicant) is ignored too */
    const uint8_t resp[] = {EAP_CODE_RESPONSE, 1, 0, 9, 1, 'b', 'o', 'b', 0};
    n = frame(f, false, 1, EAPOL_TYPE_EAP, resp, sizeof resp);
    CHECK(eapol_parse(f, n, &p));
    eapol_state_update(&s, &p, 32500);
    CHECK_EQ(s.requests, 2); CHECK_EQ(s.generation, 2);
    /* failure counts, keeps the request data */
    const uint8_t fail[] = {EAP_CODE_FAILURE, 7, 0, 4};
    n = frame(f, false, 2, EAPOL_TYPE_EAP, fail, sizeof fail);
    CHECK(eapol_parse(f, n, &p));
    eapol_state_update(&s, &p, 61000);
    CHECK_EQ(s.failures, 1); CHECK_EQ(s.requests, 2); CHECK_EQ(s.last_ms, 31000); CHECK_EQ(s.generation, 3);
    eapol_state_reset(&s);
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
