/*
 * blink.h - port blinker: find the switch port a cable lands on.
 *
 * Toggles the W5500 PHY between power-down and normal every few seconds
 * for about half a minute, so the link LED on the far-end switch port
 * blinks in a pattern no idle port shows. While active, main.c leaves
 * its link tracking alone (neighbor table, STP counters and the DHCP
 * lease survive the flaps), and it keeps doing so until the link has
 * come back after the last cycle. Started from GET /tool?blink=1, state
 * in /status.json "blink".
 */
#ifndef BLINK_H
#define BLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLINK_OFF_MS    1500      /* PHY down: switch LED off                    */
#define BLINK_ON_MS     2500      /* PHY up: time for the link to renegotiate    */
#define BLINK_TOTAL_MS  32000     /* 8 cycles                                    */
#define BLINK_SETTLE_MS 6000      /* after the last cycle: wait for link-up      */

typedef struct {
    bool     active;             /* blinking or settling                    */
    uint32_t started_ms;
    uint32_t cycles;             /* down/up cycles completed                */
    uint32_t link_ups;           /* cycles in which the link came back      */
    uint32_t generation;         /* bumps on start, each cycle, and stop    */
} blink_state_t;

/* Returns false with err filled when the Ethernet chip is not ready. */
bool blink_start(char *err, size_t errcap);
void blink_stop(void);
void blink_poll(void);
bool blink_active(void);
const blink_state_t *blink_get(void);
/* Seconds of blinking left, 0 when idle or settling. */
uint32_t blink_remaining_s(void);

#endif /* BLINK_H */
