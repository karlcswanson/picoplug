/*
 * Decode raw Ethernet frames the way the firmware does and print what the
 * page would get. Reads one frame per line on stdin as hex; prints one JSON
 * line per frame: {"kind":"lldp"|"cdp"|"other","result":"...","json_bytes":N,
 * "overflow":bool,"neighbor":{...}} where neighbor is the status message's
 * neighbors[0] (absent unless the frame parsed). Used by test_captures.py.
 */
#include "json.h"
#include "lldp.h"
#include "cdp.h"
#include "neighbor_table.h"

#include <stdio.h>
#include <string.h>

static size_t from_hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    unsigned v;
    while (*s && *s != '\n' && *s != '\r' && n < cap && sscanf(s, "%2x", &v) == 1) {
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

int main(void)
{
    static char line[8192];
    static uint8_t frame[4096];
    static char buf[STATUS_JSON_MAX];
    static lldp_neighbor_t n;
    status_info_t si = {.fw = "test", .uptime_s = 100, .eth_link = true, .eth_speed = 100,
                        .eth_full = true, .usb_connected = true, .seq = 1};

    while (fgets(line, sizeof line, stdin)) {
        size_t len = from_hex(line, frame, sizeof frame);
        const char *kind = "other", *result = "not parsed";
        bool ok = false;
        if (cdp_is_cdp_frame(frame, len)) {
            kind = "cdp";
            cdp_result_t r = cdp_parse(frame, len, &n);
            result = cdp_result_str(r);
            ok = r == CDP_OK;
        } else {
            lldp_result_t r = lldp_parse(frame, len, &n);
            if (r != LLDP_ERR_NOT_LLDP) kind = "lldp";
            result = lldp_result_str(r);
            ok = r == LLDP_OK;
        }
        printf("{\"kind\":\"%s\",\"result\":\"%s\"", kind, result);
        if (ok) {
            neighbor_table_init();
            neighbor_table_update(&n, 90000);
            jw_t w;
            jw_init(&w, buf, sizeof buf);
            status_json_build(&w, &si, 100000);
            printf(",\"json_bytes\":%zu,\"overflow\":%s", jw_len(&w), jw_overflow(&w) ? "true" : "false");
            if (!jw_overflow(&w)) printf(",\"status\":%s", buf);
        }
        printf("}\n");
    }
    return 0;
}
