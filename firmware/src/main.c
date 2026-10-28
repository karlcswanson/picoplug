/*
 * main.c - picolldp firmware entry point.
 *
 * Single-threaded main loop, no RTOS:
 *   tud_task -> usb_net_poll (lwIP) -> W5500 poll -> LLDP parse -> neighbor table
 *   -> api_server_poll (NDJSON :9000). HTTP :80 is callback driven.
 */
#include "board_config.h"
#include "lldp.h"
#include "cdp.h"
#include "lldp_print.h"
#include "neighbor_table.h"
#include "w5500_raw.h"
#include "usb_net.h"
#include "dhcpd.h"
#include "api_server.h"
#include "http_server.h"
#include "eth_net.h"
#include "probe.h"
#include "advertise.h"
#include "app.h"
#include "settings.h"
#include "tool.h"
#include "blink.h"
#include "path.h"
#include "stp.h"

#include "tusb.h"

#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include <stdio.h>
#include <string.h>

static uint8_t eth_mac[6] = ETH_MAC_PREFIX;
static uint8_t usb_host_mac[6] = USB_MAC_HOST_PREFIX;
static uint8_t usb_dev_mac[6] = USB_MAC_DEV_PREFIX;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
uint32_t g_lldp_parse_errors;   /* exposed in /status.json */
static stp_state_t g_stp;
static eapol_state_t g_dot1x;
static lacp_state_t g_lacp;
static igmp_state_t g_igmp;
static ptp_state_t g_ptp[2];    /* [0] PTPv1, [1] PTPv2 */
static uint32_t g_link_up_ms;   /* 0 = link down */
const stp_state_t *app_stp_state(void) { return &g_stp; }
const eapol_state_t *app_dot1x_state(void) { return &g_dot1x; }
const lacp_state_t *app_lacp_state(void) { return &g_lacp; }
const uint8_t *app_eth_mac(void) { return eth_mac; }
const igmp_state_t *app_igmp_state(void) { return &g_igmp; }
const ptp_state_t *app_ptp_state(uint8_t version) { return &g_ptp[version == 1 ? 0 : 1]; }

static void mcast_reset(void) { igmp_state_reset(&g_igmp); ptp_state_reset(&g_ptp[0], 1); ptp_state_reset(&g_ptp[1], 2); }

/* IGMP and PTP are read on the way past; lwIP still gets the frame (it answers IGMP queries). */
static void observe_mcast(const uint8_t *frame, size_t len)
{
    if (len < 34 || !(frame[0] & 0x01)) return;      /* multicast IPv4 only */
    pim_msg_t ph;
    if (pim_parse(frame, len, &ph)) {
        bool first = !g_igmp.pim_seen;
        igmp_state_pim(&g_igmp, &ph, now_ms());
        if (first && g_igmp.pim_seen) printf("pim: router %u.%u.%u.%u hold %u s\n", ph.src_ip[0], ph.src_ip[1], ph.src_ip[2], ph.src_ip[3], ph.holdtime_s);
        return;
    }
    igmp_msg_t q;
    if (igmp_parse(frame, len, &q)) {
        bool first = !g_igmp.seen;
        igmp_state_update(&g_igmp, &q, now_ms());
        if (first && g_igmp.seen) printf("igmp: v%u querier %u.%u.%u.%u\n", g_igmp.version, q.src_ip[0], q.src_ip[1], q.src_ip[2], q.src_ip[3]);
        return;
    }
    ptp_msg_t m;
    if (ptp_parse(frame, len, &m)) {
        ptp_state_t *t = &g_ptp[m.version == 1 ? 0 : 1];
        uint32_t g = t->generation;
        ptp_state_update(t, &m, now_ms());
        if (t->generation != g)
            printf("ptp: v%u %s domain %u from %02x:%02x:%02x:%02x:%02x:%02x%s\n", m.version, ptp_msg_name(m.version, m.msg_type), m.domain,
                   m.src_mac[0], m.src_mac[1], m.src_mac[2], m.src_mac[3], m.src_mac[4], m.src_mac[5], m.l2 ? " (layer 2)" : "");
    }
}
static uint8_t last_frame[4][1536];
static uint16_t last_frame_len[4];
static void keep_frame(int kind, const uint8_t *f, size_t len)
{
    if (len > sizeof last_frame[0]) len = sizeof last_frame[0];
    memcpy(last_frame[kind], f, len);
    last_frame_len[kind] = (uint16_t)len;
}
size_t app_last_frame(int kind, const uint8_t **frame)
{
    if (kind < 0 || kind > 3) return 0;
    *frame = last_frame[kind];
    return last_frame_len[kind];
}
uint32_t app_link_up_for_ms(void) { return g_link_up_ms ? now_ms() - g_link_up_ms : 0; }
uint32_t g_cdp_frames;
uint32_t g_wd_resets;           /* watchdog reboots since power-on (scratch reg survives resets) */
void usb_stdio_init(void);
void fault_report_previous(void);

static void got_neighbor(const lldp_neighbor_t *n)
{
    bool changed = neighbor_table_update(n, now_ms());
    if (changed) {
        printf("%s: neighbor %s (%d in table)\n", n->proto == NEIGHBOR_PROTO_CDP ? "cdp" : "lldp",
               n->ttl ? "added/updated" : "shutdown", neighbor_table_count());
        if (n->ttl) lldp_print(n);
    }
}

/* Every received Ethernet frame comes through here (W5500 poll, and the
 * /inject debug endpoint). */
void app_handle_frame(const uint8_t *frame, size_t len)
{
    static lldp_neighbor_t n; /* static: ~1.3 KB, keep off the stack */
    if (path_handle_frame(frame, len)) return;   /* probe-to-probe frames; also counts source MACs */
    if (stp_is_bpdu(frame, len)) {
        keep_frame(2, frame, len);
        stp_bpdu_t b;
        if (stp_parse(frame, len, &b)) {
            uint32_t g = g_stp.generation;
            stp_state_update(&g_stp, &b, now_ms());
            if (g_stp.generation != g)
                printf("stp: %s %s%s root %04x/%02x:%02x:%02x:%02x:%02x:%02x cost %lu %s %s%s\n",
                       stp_version_name(b.version), stp_type_name(b.type), b.pvst ? " pvst" : "",
                       b.root_priority, b.root_mac[0], b.root_mac[1], b.root_mac[2], b.root_mac[3], b.root_mac[4], b.root_mac[5],
                       (unsigned long)b.root_cost, stp_role_name(b.flags), stp_state_name(b.flags),
                       (b.flags & STP_FLAG_TC) || b.type == STP_TYPE_TCN ? " TC" : "");
        }
        return;
    }
    if (lacp_is_lacp(frame, len)) {
        keep_frame(3, frame, len);
        lacp_pdu_t lp;
        if (lacp_parse(frame, len, &lp)) {
            uint32_t g = g_lacp.generation;
            lacp_state_update(&g_lacp, &lp, now_ms());
            if (g_lacp.generation != g)
                printf("lacp: switch %02x:%02x:%02x:%02x:%02x:%02x key %u port %u state 0x%02x partner state 0x%02x\n",
                       lp.actor.sys_mac[0], lp.actor.sys_mac[1], lp.actor.sys_mac[2], lp.actor.sys_mac[3], lp.actor.sys_mac[4], lp.actor.sys_mac[5],
                       lp.actor.key, lp.actor.port, lp.actor.state, lp.partner.state);
        }
        return;
    }
    if (eapol_is_eapol(frame, len)) {
        eapol_pkt_t e;
        if (eapol_parse(frame, len, &e)) {
            uint32_t g = g_dot1x.generation;
            eapol_state_update(&g_dot1x, &e, now_ms());
            if (g_dot1x.generation != g && g_dot1x.requests <= 3)
                printf("dot1x: EAP %s%s%s from %02x:%02x:%02x:%02x:%02x:%02x (v%u)\n", eap_code_name(e.eap_code),
                       e.has_eap_type ? "/" : "", e.has_eap_type ? eap_type_name(e.eap_type) : "",
                       e.src_mac[0], e.src_mac[1], e.src_mac[2], e.src_mac[3], e.src_mac[4], e.src_mac[5], e.version);
        }
        return;
    }
    observe_mcast(frame, len);
    eth_net_input(frame, len);          /* ARP/DHCP/ICMP/DNS for the port probe */

    if (cdp_is_cdp_frame(frame, len)) {
        g_cdp_frames++;
        keep_frame(1, frame, len);
        cdp_result_t r = cdp_parse(frame, len, &n);
        if (r != CDP_OK) {
            g_lldp_parse_errors++;
            printf("cdp: dropped frame (%u bytes): %s\n", (unsigned)len, cdp_result_str(r));
            return;
        }
        got_neighbor(&n);
        return;
    }

    lldp_result_t r = lldp_parse(frame, len, &n);
    if (r == LLDP_ERR_NOT_LLDP || r == LLDP_ERR_TOO_SHORT) return;
    keep_frame(0, frame, len);
    if (r != LLDP_OK) {
        g_lldp_parse_errors++;
        printf("lldp: dropped frame (%u bytes): %s\n", (unsigned)len, lldp_result_str(r));
        return;
    }
    got_neighbor(&n);
}

static void on_frame(const uint8_t *frame, size_t len, void *ctx)
{
    (void)ctx;
    app_handle_frame(frame, len);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(100);
    printf("\npicoplug fw %s (%s)\n", FW_VERSION, PICO_BOARD);

    /* Derive locally administered MACs from the flash unique ID. */
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    memcpy(eth_mac + 3, uid.id + PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3, 3);
    memcpy(usb_host_mac + 3, uid.id + PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3, 3);
    memcpy(usb_dev_mac + 3, uid.id + PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3, 3);

#ifdef STATUS_LED_PIN
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
#endif

    neighbor_table_init();
    mcast_reset();

    /* Watchdog covers everything below, including the W5500 retry loop. */
    /* scratch[1] holds a magic so a cold boot starts the counter at 0 */
    if (watchdog_hw->scratch[1] != 0x50434C44u) { watchdog_hw->scratch[1] = 0x50434C44u; watchdog_hw->scratch[0] = 0; }
    if (watchdog_caused_reboot()) {
        watchdog_hw->scratch[0]++;
        printf("main: previous boot ended in watchdog reset (%lu so far), last loop step %lu\n",
               (unsigned long)watchdog_hw->scratch[0], (unsigned long)watchdog_hw->scratch[2]);
    }
#define STEP(n) (watchdog_hw->scratch[2] = (n))
    g_wd_resets = watchdog_hw->scratch[0];
    fault_report_previous();
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    /* Ethernet side. A missing/unresponsive W5500 must not block USB, so a
     * failed init is retried from the main loop instead of spinning here. */
    absolute_time_t next_w5500_retry = make_timeout_time_ms(1000);
    if (!w5500_init(eth_mac)) printf("main: W5500 init failed, will retry\n");

    /* USB side. Order is deliberate: lwIP and every service that must be
     * ready when the host's first DHCP DISCOVER arrives are started BEFORE
     * the USB device is enabled (see usb_net.h). */
    settings_init();                        /* usb_net and dhcpd read the device address */
    usb_net_init(usb_host_mac, usb_dev_mac);
    eth_net_init(eth_mac);
    advertise_init(eth_mac);
    path_init(eth_mac);
    if (!probe_init()) printf("main: probe init failed\n");
    if (!tool_init()) printf("main: tool init failed\n");
    if (!dhcpd_init()) printf("main: dhcpd init failed\n");
    if (!api_server_init()) printf("main: api server init failed\n");
    if (!http_server_init()) printf("main: http server init failed\n");
    tud_init(BOARD_TUD_RHPORT);
    usb_stdio_init();
    printf("main: usb device enabled (%s%s)\n", USB_NET_USE_ECM ? "CDC-ECM" : "CDC-NCM",
           USB_DEBUG_CDC ? " + CDC debug serial" : "");

    absolute_time_t next_status = make_timeout_time_ms(5000);
    absolute_time_t next_blink = make_timeout_time_ms(500);
    absolute_time_t next_link_check = make_timeout_time_ms(500);
    w5500_link_t last_link = {0};
    bool led = false;

    while (true) {
        watchdog_update();

        STEP(1); tud_task();
        STEP(2); usb_net_poll();

        if (w5500_ready()) {
            STEP(3); w5500_poll(on_frame, NULL);
        } else if (time_reached(next_w5500_retry)) {
            next_w5500_retry = make_timeout_time_ms(1000);
            STEP(4); w5500_init(eth_mac);
        }

        if (neighbor_table_expire(now_ms()))
            printf("lldp: neighbor expired (%d in table)\n", neighbor_table_count());

        /* The port blinker and the path test drop the link on purpose; link tracking stays frozen until they are done. */
        if (time_reached(next_link_check) && !blink_active() && !path_busy()) {
            STEP(5);
            next_link_check = make_timeout_time_ms(500);
            w5500_link_t l;
            w5500_get_link(&l);
            if (l.link && !g_link_up_ms) { g_link_up_ms = now_ms() ? now_ms() : 1; stp_state_reset(&g_stp); eapol_state_reset(&g_dot1x); lacp_state_reset(&g_lacp); mcast_reset(); }
            else if (!l.link && g_link_up_ms) {
                g_link_up_ms = 0;
                stp_state_reset(&g_stp);
                eapol_state_reset(&g_dot1x);
                lacp_state_reset(&g_lacp);
                mcast_reset();
                int dropped = neighbor_table_clear();
                if (dropped) printf("lldp: link down, dropped %d neighbor%s\n", dropped, dropped == 1 ? "" : "s");
            }
            eth_net_set_link(l.link);
            path_link_changed(l.link);
        }
        STEP(6); blink_poll(); path_poll();
        STEP(7); eth_net_poll();
        STEP(8); advertise_poll();
        STEP(9); probe_poll();
        STEP(10); tool_poll();
        STEP(11); api_server_poll();
        STEP(12); http_server_poll();
        STEP(13);

        if (time_reached(next_blink)) {
            next_blink = make_timeout_time_ms(neighbor_table_count() ? 100 : 500);
            led = !led;
#ifdef STATUS_LED_PIN
            gpio_put(STATUS_LED_PIN, led);
#endif
        }

        if (time_reached(next_status)) {
            next_status = make_timeout_time_ms(5000);
            w5500_link_t l;
            w5500_get_link(&l);
            if (memcmp(&l, &last_link, sizeof l) != 0) {
                last_link = l;
                if (l.link) printf("eth: link up, %u Mbit/s %s duplex\n", l.speed, l.full ? "full" : "half");
                else printf("eth: link down\n");
            }
            extern uint32_t g_last_fault_pc, g_last_fault_lr;
            if (g_last_fault_pc) printf("main: NOTE previous boot hard faulted at pc=0x%08lx lr=0x%08lx\n",
                                        (unsigned long)g_last_fault_pc, (unsigned long)g_last_fault_lr);
            const w5500_stats_t *s = w5500_stats();
            const usb_net_stats_t *u = usb_net_stats();
            printf("main: up %lu s, eth rx %lu tx %lu/%lu fail (oversize %lu, resyncs %lu), neighbors %d, "
                   "usb %s rx %lu tx %lu drop %lu/%lu reenum %lu, dhcp leases %lu, api clients %d\n",
                   (unsigned long)(now_ms() / 1000), (unsigned long)s->frames,
                   (unsigned long)s->tx_frames, (unsigned long)s->tx_fail,
                   (unsigned long)s->oversize, (unsigned long)s->resyncs, neighbor_table_count(),
                   usb_net_mounted() ? "up" : "down", (unsigned long)u->rx_frames,
                   (unsigned long)u->tx_frames, (unsigned long)u->rx_dropped,
                   (unsigned long)u->tx_dropped, (unsigned long)u->reenums, (unsigned long)dhcpd_leases_granted(),
                   api_server_clients());
        }
    }
}
