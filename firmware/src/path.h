/*
 * path.h - two-probe path test (firmware side of peer.h).
 *
 * Every probe with a link broadcasts a HELLO on EtherType 0x88B5 every
 * 5 s and keeps a table of the other probes it hears. Any probe can then
 * run the path test against one of them (GET /tool?path=1[&peer=MAC]):
 *
 *   1. ask the peer to drop its link once (BLINK_REQ, retried until ACK)
 *   2. the peer powers its PHY down for PATH_OFF_MS (blink_pulse)
 *   3. watch our own link and send the peer an echo every 200 ms
 *
 *   our link dropped in step          -> direct (or a link-transparent
 *                                         media converter / extender)
 *   link stayed up, peer went silent  -> switched; time until the peer
 *                                         answers again is reported too
 *                                         (about 30 s means its switch
 *                                         port is not an edge port)
 *
 * Link tracking in main.c stays frozen while a test runs, as for the
 * blinker, so a direct-cable drop does not wipe the page.
 */
#ifndef PATH_H
#define PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "peer.h"

#define PATH_HELLO_MS      5000
#define PATH_PEER_AGE_MS   16000
#define PATH_OFF_MS        3000
#define PATH_SILENT_MS     1000   /* no echo reply this long = peer silent        */
#define PATH_RECOVER_MS    45000  /* how long to wait for a switched peer to return */

typedef enum { PT_IDLE = 0, PT_ASK, PT_WATCH, PT_RECOVER, PT_DONE } path_phase_t;

typedef struct {
    path_phase_t   phase;
    uint8_t        peer[6];
    uint32_t       started_ms;
    uint32_t       acked_ms;           /* 0 = not acknowledged          */
    bool           own_link_dropped;
    uint32_t       own_down_ms;        /* how long our link was down     */
    bool           peer_went_silent;
    bool           peer_came_back;
    uint32_t       peer_back_ms;       /* from ACK to the first echo after the silence */
    path_verdict_t verdict;
    uint32_t       finished_ms;
    uint32_t       generation;
} path_test_t;

void path_init(const uint8_t mac[6]);
/* Returns true if the frame was a peer frame (consumed). Also counts source MACs. */
bool path_handle_frame(const uint8_t *frame, size_t len);
void path_poll(void);
void path_link_changed(bool up);
/* peer NULL = the only peer (error if none or several). */
bool path_start(const uint8_t *peer, char *err, size_t errcap);
bool path_busy(void);            /* a test is running: keep link tracking frozen */
const path_test_t  *path_test(void);
const peer_table_t *path_peers(void);
uint32_t path_generation(void);

#endif /* PATH_H */
