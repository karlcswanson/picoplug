/*
 * advertise.c - see advertise.h.
 */
#include "advertise.h"
#include "settings.h"
#include "board_config.h"
#include "lldp_tx.h"
#include "cdp_tx.h"
#include "w5500_raw.h"
#include "eth_net.h"

#include "pico/time.h"

#include <string.h>

static uint8_t our_mac[6];
static advertise_state_t st;
static uint32_t generation;
static bool link_was_up;
static uint8_t burst_left;              /* fast-start frames still to send */
static absolute_time_t next_lldp, next_cdp;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

/* "a.b.c.d" -> 4 bytes. Hand-written: sscanf costs ~53 KB of flash here. */
static bool parse_ip(const char *s, uint8_t out[4])
{
    for (int i = 0; i < 4; i++) {
        unsigned v = 0; int digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned)(*s++ - '0'); if (++digits > 3 || v > 255) return false; }
        if (!digits) return false;
        out[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return false; s++; }
    }
    return *s == '\0';
}

static void send_lldp(uint16_t ttl)
{
    static uint8_t frame[LLDP_TX_MAX_FRAME];
    char name[16]; settings_device_name(settings_get(), name, sizeof name);
    w5500_link_t l; w5500_get_link(&l);
    eth_net_info_t ei; eth_net_get_info(&ei);
    lldp_tx_cfg_t cfg = {
        .mac = our_mac,
        .port_id = ADV_PORT_ID,
        .port_desc = ADV_PORT_DESC,
        .sys_name = name,
        .sys_desc = ADV_SYS_DESC,
        .ttl_s = ttl,
        .speed = l.link ? l.speed : 0,
        .full = l.full,
        .med_device_type = ADV_MED_DEVICE_TYPE,
    };
    cfg.has_ip = ei.bound && parse_ip(ei.ip, cfg.ip);
    size_t n = lldp_tx_build(frame, sizeof frame, &cfg);
    if (n && w5500_send(frame, n)) { st.lldp_frames++; st.lldp_last_ms = now_ms(); }
}

static void send_cdp(uint8_t ttl)
{
    static uint8_t frame[CDP_TX_MAX_FRAME];
    char name[16]; settings_device_name(settings_get(), name, sizeof name);
    w5500_link_t l; w5500_get_link(&l);
    eth_net_info_t ei; eth_net_get_info(&ei);
    cdp_tx_cfg_t cfg = {
        .mac = our_mac,
        .device_id = name,
        .port_id = ADV_PORT_ID,
        .sw_version = ADV_SYS_DESC,
        .platform = ADV_PLATFORM,
        .ttl_s = ttl,
        .full_duplex = l.full,
    };
    cfg.has_ip = ei.bound && parse_ip(ei.ip, cfg.ip);
    size_t n = cdp_tx_build(frame, sizeof frame, &cfg);
    if (n && w5500_send(frame, n)) { st.cdp_frames++; st.cdp_last_ms = now_ms(); }
}

static void start_burst(void)
{
    burst_left = ADV_FAST_START_COUNT;
    next_lldp = get_absolute_time();
    next_cdp = get_absolute_time();
}

void advertise_init(const uint8_t mac[6])
{
    memcpy(our_mac, mac, 6);
    memset(&st, 0, sizeof st);
    st.lldp_enabled = settings_get()->lldp_on;
    st.cdp_enabled = settings_get()->cdp_on;
    next_lldp = next_cdp = at_the_end_of_time;
}

void advertise_poll(void)
{
    w5500_link_t l; w5500_get_link(&l);
    bool up = l.link && w5500_ready();
    if (up != link_was_up) {
        link_was_up = up;
        if (up) start_burst();          /* a new switch port: introduce ourselves */
        else { next_lldp = next_cdp = at_the_end_of_time; burst_left = 0; }
    }
    if (!up) return;

    if (st.lldp_enabled && time_reached(next_lldp)) {
        send_lldp(ADV_LLDP_TTL_S);
        if (burst_left) burst_left--;
        next_lldp = make_timeout_time_ms(burst_left ? ADV_FAST_START_MS : ADV_LLDP_INTERVAL_S * 1000u);
    }
    if (st.cdp_enabled && time_reached(next_cdp)) {
        send_cdp(ADV_CDP_TTL_S);
        next_cdp = make_timeout_time_ms(ADV_CDP_INTERVAL_S * 1000u);
    }
}

void advertise_set_enabled(bool lldp, bool cdp)
{
    if (st.lldp_enabled && !lldp && link_was_up) send_lldp(0);     /* shutdown LLDPDU */
    if (st.cdp_enabled && !cdp && link_was_up) send_cdp(0);
    bool restart = (!st.lldp_enabled && lldp) || (!st.cdp_enabled && cdp);
    st.lldp_enabled = lldp;
    st.cdp_enabled = cdp;
    generation++;
    if (restart && link_was_up) start_burst();
}

void advertise_name_changed(void)
{
    generation++;
    if (link_was_up) start_burst();
}

void advertise_get_state(advertise_state_t *out) { *out = st; }
uint32_t advertise_generation(void) { return generation; }
