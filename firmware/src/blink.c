/*
 * blink.c - see blink.h.
 */
#include "blink.h"
#include "w5500_raw.h"

#include "pico/time.h"

#include <stdio.h>
#include <string.h>

typedef enum { B_IDLE, B_OFF, B_ON, B_SETTLE } phase_t;

static blink_state_t bs;
static phase_t phase;
static absolute_time_t next_edge, end_at, settle_until;
static bool seen_up;             /* link came back in the current ON phase */

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void set_phy(bool on)
{
    w5500_phy_power(on);
    phase = on ? B_ON : B_OFF;
    next_edge = make_timeout_time_ms(on ? BLINK_ON_MS : BLINK_OFF_MS);
    seen_up = false;
}

bool blink_start(char *err, size_t errcap)
{
    if (!w5500_ready()) { snprintf(err, errcap, "Ethernet chip not ready"); return false; }
    uint32_t g = bs.generation;
    memset(&bs, 0, sizeof bs);
    bs.generation = g + 1;
    bs.active = true;
    bs.started_ms = now_ms();
    end_at = make_timeout_time_ms(BLINK_TOTAL_MS);
    set_phy(false);
    printf("blink: start\n");
    return true;
}

bool blink_pulse(uint32_t off_ms)
{
    if (!w5500_ready() || bs.active) return false;
    if (off_ms < 500) off_ms = 500;
    if (off_ms > 5000) off_ms = 5000;
    uint32_t g = bs.generation;
    memset(&bs, 0, sizeof bs);
    bs.generation = g + 1;
    bs.active = true;
    bs.pulse = true;
    bs.started_ms = now_ms();
    end_at = get_absolute_time();
    w5500_phy_power(false);
    phase = B_OFF;
    seen_up = false;
    next_edge = make_timeout_time_ms(off_ms);
    printf("blink: pulse %lu ms for a peer path test\n", (unsigned long)off_ms);
    return true;
}

static void settle(void)
{
    if (phase == B_OFF) w5500_phy_power(true);
    phase = B_SETTLE;
    settle_until = make_timeout_time_ms(BLINK_SETTLE_MS);
    bs.generation++;
}

void blink_stop(void)
{
    if (phase == B_OFF || phase == B_ON) { printf("blink: stop after %lu cycles\n", (unsigned long)bs.cycles); settle(); }
}

void blink_poll(void)
{
    w5500_link_t l;
    switch (phase) {
    case B_IDLE: return;
    case B_OFF:
        if (time_reached(next_edge)) {
            if (bs.pulse) { w5500_phy_power(true); phase = B_ON; bs.cycles = 1; settle(); }
            else set_phy(true);
        }
        return;
    case B_ON:
        if (!seen_up) { w5500_get_link(&l); if (l.link) { seen_up = true; bs.link_ups++; } }
        if (time_reached(next_edge)) {
            bs.cycles++;
            bs.generation++;
            if (time_reached(end_at)) { printf("blink: done, %lu cycles, link back %lu times\n", (unsigned long)bs.cycles, (unsigned long)bs.link_ups); settle(); }
            else set_phy(false);
        }
        return;
    case B_SETTLE:
        w5500_get_link(&l);
        if (l.link && !seen_up) { seen_up = true; if (bs.pulse) bs.link_ups = 1; }
        if (l.link || time_reached(settle_until)) { phase = B_IDLE; bs.active = false; bs.generation++; }
        return;
    }
}

bool blink_active(void) { return bs.active; }
const blink_state_t *blink_get(void) { return &bs; }
uint32_t blink_remaining_s(void)
{
    if (phase != B_OFF && phase != B_ON) return 0;
    int64_t us = absolute_time_diff_us(get_absolute_time(), end_at);
    return us > 0 ? (uint32_t)(us / 1000000) + 1 : 0;
}
