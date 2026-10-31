/*
 * Host test for json.c: writer edge cases, then print one status message
 * per fixture on stdout (check_json.py validates them as real JSON).
 */
#include "json.h"
#include "lldp.h"
#include "neighbor_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

static size_t load_hex(const char *name, uint8_t *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.hex", FIXTURE_DIR, name);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = 0;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        for (char *p = line; *p;) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (!*p) break;
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            if (n < cap) buf[n] = (uint8_t)v;
            n++;
            p += 2;
        }
    }
    fclose(f);
    return n <= cap ? n : 0;
}

static int writer_tests(void)
{
    char buf[256];
    jw_t w;

    jw_init(&w, buf, sizeof buf);
    jw_obj_begin(&w);
    jw_kv_str(&w, "s", "a\"b\\c\n\x01\xe9");
    jw_kv_uint(&w, "u", 4294967295u);
    jw_key(&w, "i"); jw_int(&w, -2147483647 - 1);
    jw_key(&w, "f"); jw_fixed(&w, 154, 1);
    jw_key(&w, "f0"); jw_fixed(&w, 7, 0);
    jw_key(&w, "f2"); jw_fixed(&w, 5, 2);
    jw_key(&w, "arr"); jw_arr_begin(&w); jw_uint(&w, 1); jw_bool(&w, false); jw_null(&w);
    jw_obj_begin(&w); jw_obj_end(&w); jw_arr_end(&w);
    jw_key(&w, "e"); jw_obj_begin(&w); jw_obj_end(&w);
    jw_obj_end(&w);
    CHECK(!jw_overflow(&w));
    CHECK(strcmp(buf, "{\"s\":\"a\\\"b\\\\c\\n\\u0001\\u00e9\",\"u\":4294967295,\"i\":-2147483648,"
                      "\"f\":15.4,\"f0\":7,\"f2\":0.05,\"arr\":[1,false,null,{}],\"e\":{}}") == 0);
    CHECK(jw_len(&w) == strlen(buf));

    /* overflow: NUL-terminated, truncated, flagged, length = needed */
    char small[10];
    memset(small, 'Z', sizeof small);
    jw_init(&w, small, sizeof small);
    jw_obj_begin(&w);
    jw_kv_str(&w, "hello", "world");
    jw_obj_end(&w);
    CHECK(jw_overflow(&w));
    CHECK(strlen(small) == 9);
    CHECK(strncmp(small, "{\"hello\":", 9) == 0);
    CHECK(jw_len(&w) == strlen("{\"hello\":\"world\"}"));

    /* zero-capacity buffer must not be written */
    jw_init(&w, small, 0);
    jw_obj_begin(&w); jw_kv_str(&w, "a", "b"); jw_obj_end(&w);
    CHECK(jw_overflow(&w));

    /* depth beyond JW_MAX_DEPTH does not crash */
    char big[1024];
    jw_init(&w, big, sizeof big);
    for (int i = 0; i < 40; i++) jw_arr_begin(&w);
    for (int i = 0; i < 40; i++) jw_arr_end(&w);
    CHECK(!jw_overflow(&w));
    return 0;
}

/* A buffer too small for every neighbor: the message still closes cleanly,
 * keeps the neighbors that fit and counts the rest. */
static int fit_tests(void)
{
    static const char *const fixtures[] = {"cisco", "aruba", "juniper", "ubiquiti"};
    static char buf[STATUS_JSON_MAX];
    static uint8_t frame[2000];
    static lldp_neighbor_t n;
    status_info_t si = {.fw = "test", .uptime_s = 100, .eth_link = true, .usb_connected = true, .seq = 1};
    jw_t w;

    neighbor_table_init();
    for (size_t i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++) {
        size_t len = load_hex(fixtures[i], frame, sizeof frame);
        CHECK(len > 0 && lldp_parse(frame, len, &n) == LLDP_OK);
        neighbor_table_update(&n, 90000);
    }
    CHECK(neighbor_table_count() == 4);

    jw_init(&w, buf, sizeof buf);                       /* full size: all four, nothing omitted */
    status_json_build(&w, &si, 100000);
    CHECK(!jw_overflow(&w));
    CHECK(!strstr(buf, "neighbors_omitted"));
    size_t full = jw_len(&w);

    jw_init(&w, buf, full - 200);                       /* the last neighbor no longer fits */
    status_json_build(&w, &si, 100000);
    CHECK(!jw_overflow(&w));
    CHECK(strstr(buf, "\"neighbors_omitted\":1}") != NULL);
    CHECK(buf[jw_len(&w) - 1] == '}');

    jw_init(&w, buf, 700);                              /* none fit */
    status_json_build(&w, &si, 100000);
    CHECK(!jw_overflow(&w));
    CHECK(strstr(buf, "\"neighbors\":[],\"neighbors_omitted\":4}") != NULL);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "fit") == 0) {
        int rc = fit_tests();
        fprintf(stderr, "json fit: %s\n", rc ? "FAIL" : "ok");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "writer") == 0) {
        int rc = writer_tests();
        fprintf(stderr, "json writer: %s\n", rc ? "FAIL" : "ok");
        return rc;
    }

    /* Emit status messages: empty table, then each fixture alone, then all. */
    static const char *const fixtures[] = {"cisco", "aruba", "juniper", "ubiquiti", "lldpd"};
    static char buf[STATUS_JSON_MAX];
    static uint8_t frame[2000];
    static lldp_neighbor_t n;
    status_info_t si = {.fw = "test", .uptime_s = 100, .eth_link = true, .eth_speed = 100,
                        .eth_full = true, .usb_connected = true, .seq = 1};
    jw_t w;

    neighbor_table_init();
    jw_init(&w, buf, sizeof buf);
    status_json_build(&w, &si, 100000);
    CHECK(!jw_overflow(&w));
    printf("%s\n", buf);

    for (size_t i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++) {
        size_t len = load_hex(fixtures[i], frame, sizeof frame);
        CHECK(len > 0);
        CHECK(lldp_parse(frame, len, &n) == LLDP_OK);
        neighbor_table_init();
        neighbor_table_update(&n, 90000);
        si.seq++;
        jw_init(&w, buf, sizeof buf);
        status_json_build(&w, &si, 100000);
        CHECK(!jw_overflow(&w));
        printf("%s\n", buf);
    }

    /* all fixtures at once (NEIGHBOR_MAX caps the table) + link down */
    neighbor_table_init();
    for (size_t i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++) {
        size_t len = load_hex(fixtures[i], frame, sizeof frame);
        lldp_parse(frame, len, &n);
        neighbor_table_update(&n, 90000);
    }
    si.eth_link = false;
    si.seq++;
    /* an 802.1X port and a running blinker, so those writers get exercised too */
    eapol_state_t dx; eapol_state_reset(&dx);
    eapol_pkt_t ep = {.src_mac = {0x64, 0x9D, 0x99, 0x32, 0x7E, 0x90}, .version = 2, .type = EAPOL_TYPE_EAP,
                      .eap_code = EAP_CODE_REQUEST, .eap_type = 1, .has_eap_type = true};
    eapol_state_update(&dx, &ep, 40000);
    eapol_state_update(&dx, &ep, 70000);
    blink_state_t bl = {.active = true, .cycles = 3, .link_ups = 2, .generation = 5};
    ptp_state_t p1, p2; ptp_state_reset(&p1, 1); ptp_state_reset(&p2, 2);
    ptp_msg_t pm = {.src_mac = {0x00, 0x1D, 0xC1, 0x50, 0xD9, 0x28}, .src_ip = {192, 168, 109, 129}, .version = 1, .msg_type = 0,
                    .clock_id = {0x00, 0x1D, 0xC1, 0x50, 0xD9, 0x28}, .subdomain = "_DFLT"};
    ptp_state_update(&p1, &pm, 50000); ptp_state_update(&p1, &pm, 50250);
    igmp_state_t ig; igmp_state_reset(&ig);
    igmp_msg_t im = {.src_mac = {0x94, 0x83, 0xC4, 0x58, 0xAD, 0x95}, .src_ip = {192, 168, 109, 1}, .type = 0x11, .version = 3, .max_resp_ds = 100, .qqi_s = 125};
    igmp_state_update(&ig, &im, 1000); igmp_state_update(&ig, &im, 126000);
    si.dot1x = &dx; si.blink = &bl; si.blink_remaining_s = 20; si.ptp1 = &p1; si.ptp2 = &p2; si.igmp = &ig;
    jw_init(&w, buf, sizeof buf);
    status_json_build(&w, &si, 100000);
    CHECK(!jw_overflow(&w));
    fprintf(stderr, "json: full table message is %zu bytes (cap %d)\n", jw_len(&w), STATUS_JSON_MAX);
    printf("%s\n", buf);
    return 0;
}
