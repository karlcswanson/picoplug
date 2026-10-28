/*
 * json.h - tiny bounded JSON writer and the status message builder.
 *
 * No heap, no floats, no printf. Writing past the buffer never overruns:
 * output is silently truncated, the buffer stays NUL-terminated, and
 * jw_overflow() reports it so callers can drop the message.
 */
#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stp.h"
#include "dhcp_info.h"
#include "tool.h"
#include "eapol.h"
#include "lacp.h"
#include "mcast.h"
#include "blink.h"
#include "path.h"

#define JW_MAX_DEPTH 12

typedef struct {
    char   *buf;
    size_t  cap;
    size_t  pos;            /* bytes that would have been written */
    uint8_t depth;
    bool    after_key;
    bool    has_item[JW_MAX_DEPTH];
} jw_t;

void   jw_init(jw_t *w, char *buf, size_t cap);
bool   jw_overflow(const jw_t *w);
size_t jw_len(const jw_t *w);

void jw_obj_begin(jw_t *w);
void jw_obj_end(jw_t *w);
void jw_arr_begin(jw_t *w);
void jw_arr_end(jw_t *w);
void jw_key(jw_t *w, const char *key);
void jw_str(jw_t *w, const char *s);
void jw_strn(jw_t *w, const char *s, size_t n);
void jw_int(jw_t *w, int32_t v);
void jw_uint(jw_t *w, uint32_t v);
void jw_bool(jw_t *w, bool v);
void jw_null(jw_t *w);
/* value / 10^scale as a decimal number, e.g. jw_fixed(w, 154, 1) -> 15.4 */
void jw_fixed(jw_t *w, uint32_t value, unsigned scale);
/* Convenience: key + value */
void jw_kv_str(jw_t *w, const char *k, const char *v);
void jw_kv_uint(jw_t *w, const char *k, uint32_t v);
void jw_kv_bool(jw_t *w, const char *k, bool v);

/* ---- status message ---------------------------------------------------- */

/* Ethernet-side port probe results (all strings pre-formatted). */
typedef struct {
    bool     link;
    bool     dhcp_bound;
    const char *dhcp_state;
    const char *ip, *mask, *gateway, *server;
    const char *dns[2];
    uint8_t  n_dns;
    uint32_t lease_s;
    uint32_t bound_for_s;
    /* probe */
    bool     probe_valid, probe_running;
    const char *probe_phase;
    uint32_t probe_age_s;
    uint32_t probe_runs;
    bool     gw_arp_ok;
    const uint8_t *gw_mac;
    uint8_t  gw_sent, gw_replies;   uint16_t gw_rtt_ms;
    const char *inet_target;
    uint8_t  inet_sent, inet_replies; uint16_t inet_rtt_ms;
    bool     dns_done, dns_ok;
    const char *dns_server;   /* resolver used by the probe, NULL = unknown */
    bool     dns_custom;      /* from settings rather than DHCP */
    const char *dns_name, *dns_addr;
    uint16_t dns_ms;
} port_info_t;


typedef struct {
    const char *fw;
    const char *board;             /* NULL = omit "board" */
    bool has_probe; uint8_t probe; /* this unit's probe number 0-9 */
    uint32_t    uptime_s;
    bool        eth_link;
    uint16_t    eth_speed;
    bool        eth_full;
    bool        usb_connected;
    uint32_t    seq;
    uint32_t    wd_resets;
    uint32_t    last_fault_pc, last_fault_lr;
    /* Ethernet chip diagnostics (all optional; emitted under "eth") */
    bool        eth_chip_ok;
    uint8_t     eth_version;
    uint8_t     eth_probe_w5100s;
    uint8_t     eth_phycfgr;
    uint8_t     eth_sn_sr;
    uint32_t    eth_frames;
    uint32_t    eth_resyncs;
    uint32_t    eth_tx_frames, eth_tx_fail, eth_tx_nolink, eth_cr_timeouts;
    uint32_t    eth_init_attempts;
    uint32_t    eth_spi_hz;
    uint32_t    eth_polls;
    uint32_t    eth_bad_headers;
    uint32_t    eth_lldp_frames;
    uint32_t    eth_cdp_frames;
    uint32_t    eth_parse_errors;
    uint16_t    eth_last_rsr;
    uint16_t    eth_last_hdr;
    const uint8_t *eth_last_dst;   /* 6 bytes or NULL */
    const port_info_t *port;       /* NULL = omit "port" */
    const dhcp_info_state_t *dhcp_detail;   /* NULL = omit the extra dhcp fields */
    const stp_state_t *stp;        /* NULL = omit "stp" */
    const tool_state_t *tool;      /* NULL = omit "tool" */
    const eapol_state_t *dot1x;    /* NULL = omit "dot1x" */
    const lacp_state_t *lacp;      /* NULL = omit "lacp" */
    const igmp_state_t *igmp;      /* NULL = omit "igmp" */
    const ptp_state_t *ptp1, *ptp2; /* NULL = omit "ptp" (both or neither) */
    const blink_state_t *blink;    /* NULL = omit "blink" */
    uint32_t    blink_remaining_s;
    const peer_table_t *peers;     /* NULL = omit "peers" */
    const path_test_t  *path;      /* NULL = omit "path" */
    const uint8_t      *self_mac;  /* ours, for spotting a peer that sees us as its neighbor */
    uint32_t    link_up_for_s;     /* for "stp": seconds since link-up, 0 = down */
    /* our own advertisements; tx_present = emit the "tx" object */
    bool        tx_present;
    bool        tx_lldp_on, tx_cdp_on;
    uint32_t    tx_lldp_frames, tx_cdp_frames;
    uint32_t    tx_lldp_age_s, tx_cdp_age_s;   /* UINT32_MAX = never sent */
} status_info_t;

/* Suggested buffer size for one status message with NEIGHBOR_MAX entries. */
#define STATUS_JSON_MAX 6144

/*
 * Write one {"type":"status",...} object (no trailing newline) describing
 * `si` and every entry in the neighbor table. See docs/PROTOCOL.md.
 */
void status_json_build(jw_t *w, const status_info_t *si, uint32_t now_ms);

#endif /* JSON_H */
