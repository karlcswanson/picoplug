/*
 * status.c - see status.h.
 */
#include "status.h"
#include "board_config.h"
#include "json.h"
#include "neighbor_table.h"
#include "usb_net.h"
#include "w5500_raw.h"
#include "eth_net.h"
#include "probe.h"
#include "advertise.h"
#include "app.h"
#include "settings.h"
#include "dhcp_info.h"
#include "tool.h"
#include "blink.h"
#include "path.h"

#include "pico/time.h"

extern uint32_t g_lldp_parse_errors;   /* main.c */
extern uint32_t g_cdp_frames;          /* main.c */
extern uint32_t g_wd_resets;           /* main.c */
extern uint32_t g_last_fault_pc, g_last_fault_lr;   /* fault.c */

size_t status_render(char *buf, size_t cap, uint32_t seq)
{
    w5500_link_t l;
    w5500_get_link(&l);
    const w5500_stats_t *st = w5500_stats();
    uint32_t now = to_ms_since_boot(get_absolute_time());

    eth_net_info_t ei;
    eth_net_get_info(&ei);
    const probe_result_t *pr = probe_result();
    port_info_t port = {
        .link = ei.link,
        .dhcp_bound = ei.bound,
        .dhcp_state = ei.state,
        .ip = ei.ip, .mask = ei.mask, .gateway = ei.gateway, .server = ei.server,
        .dns = {ei.dns[0], ei.dns[1]},
        .n_dns = ei.n_dns,
        .lease_s = ei.lease_s,
        .bound_for_s = ei.bound_for_s,
        .probe_valid = pr->valid,
        .probe_running = pr->running,
        .probe_phase = pr->phase ? pr->phase : "idle",
        .probe_age_s = pr->valid ? now / 1000u - pr->completed_at_s : 0,
        .probe_runs = pr->runs,
        .gw_arp_ok = pr->gw_arp_ok,
        .gw_mac = pr->gw_mac,
        .gw_sent = pr->gw.sent, .gw_replies = pr->gw.replies, .gw_rtt_ms = pr->gw.best_rtt_ms,
        .inet_target = settings_get()->ping_target,
        .inet_sent = pr->inet.sent, .inet_replies = pr->inet.replies, .inet_rtt_ms = pr->inet.best_rtt_ms,
        .dns_done = pr->dns_done, .dns_ok = pr->dns_ok,
        .dns_name = settings_get()->dns_name, .dns_addr = pr->dns_addr,
        .dns_server = pr->dns_server, .dns_custom = pr->dns_custom,
        .dns_ms = pr->dns_ms,
    };

    advertise_state_t adv;
    advertise_get_state(&adv);

    status_info_t si = {
        .fw = FW_VERSION,
        .board = PICO_BOARD,
        .has_probe = settings_get()->probe <= 9, .probe = settings_get()->probe,
        .uptime_s = now / 1000u,
        .eth_link = l.link,
        .eth_speed = l.speed,
        .eth_full = l.full,
        .usb_connected = usb_net_mounted(),
        .seq = seq,
        .wd_resets = g_wd_resets,
        .last_fault_pc = g_last_fault_pc,
        .last_fault_lr = g_last_fault_lr,
        .eth_chip_ok = w5500_ready(),
        .eth_version = st->version,
        .eth_probe_w5100s = st->probe_w5100s,
        .eth_phycfgr = st->phycfgr,
        .eth_sn_sr = st->sn_sr,
        .eth_frames = st->frames,
        .eth_resyncs = st->resyncs,
        .eth_tx_frames = st->tx_frames,
        .eth_tx_fail = st->tx_fail,
        .eth_tx_nolink = st->tx_nolink,
        .eth_cr_timeouts = st->cr_timeouts,
        .eth_init_attempts = st->init_attempts,
        .eth_spi_hz = st->spi_hz,
        .eth_polls = st->polls,
        .eth_bad_headers = st->bad_headers,
        .eth_lldp_frames = st->lldp_frames,
        .eth_cdp_frames = g_cdp_frames,
        .eth_parse_errors = g_lldp_parse_errors,
        .eth_last_rsr = st->last_rsr,
        .eth_last_hdr = st->last_hdr,
        .eth_last_dst = st->frames ? st->last_dst : NULL,
        .port = &port,
        .dhcp_detail = dhcp_info_get(),
        .stp = app_stp_state(),
        .tool = tool_get(),
        .dot1x = app_dot1x_state(),
        .lacp = app_lacp_state(),
        .igmp = app_igmp_state(),
        .ptp1 = app_ptp_state(1), .ptp2 = app_ptp_state(2),
        .blink = blink_get(),
        .blink_remaining_s = blink_remaining_s(),
        .peers = path_peers(),
        .path = path_test(),
        .self_mac = app_eth_mac(),
        .link_up_for_s = app_link_up_for_ms() / 1000u,
        .tx_present = true,
        .tx_lldp_on = adv.lldp_enabled, .tx_cdp_on = adv.cdp_enabled, .tx_cdp_peer = adv.cdp_peer,
        .tx_lldp_frames = adv.lldp_frames, .tx_cdp_frames = adv.cdp_frames,
        .tx_lldp_age_s = adv.lldp_last_ms ? (now - adv.lldp_last_ms) / 1000u : UINT32_MAX,
        .tx_cdp_age_s = adv.cdp_last_ms ? (now - adv.cdp_last_ms) / 1000u : UINT32_MAX,
    };
    jw_t w;
    jw_init(&w, buf, cap);
    status_json_build(&w, &si, now);
    return jw_overflow(&w) ? 0 : jw_len(&w);
}

uint32_t status_fingerprint(void)
{
    w5500_link_t l;
    w5500_get_link(&l);
    uint32_t fp = neighbor_table_generation() * 16u;
    fp |= l.link ? 1u : 0u;
    fp |= l.speed == 100 ? 2u : 0u;
    fp |= l.full ? 4u : 0u;
    fp |= usb_net_mounted() ? 8u : 0u;
    eth_net_info_t ei;
    eth_net_get_info(&ei);
    const probe_result_t *pr = probe_result();
    fp ^= ei.leases * 0x10000u;
    fp ^= ei.bound ? 0x100u : 0u;
    fp ^= pr->runs * 0x1000000u;
    fp ^= pr->valid ? 0x200u : 0u;
    fp ^= advertise_generation() * 0x400u;
    fp ^= app_stp_state()->generation * 0x800u;
    fp ^= dhcp_info_get()->generation * 0x2000u;
    fp ^= settings_generation() * 0x4000u;
    fp ^= tool_get()->generation * 0x8000u;
    fp ^= app_dot1x_state()->generation * 0x20000u;
    fp ^= blink_get()->generation * 0x80000u;
    fp ^= app_lacp_state()->generation * 0x40000u;
    fp ^= app_igmp_state()->generation * 0x200000u;
    fp ^= app_ptp_state(1)->generation * 0x800000u;
    fp ^= app_ptp_state(2)->generation * 0x2000000u;
    fp ^= path_generation() * 0x4000000u;
    return fp;
}
