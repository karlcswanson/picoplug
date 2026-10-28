/*
 * status.h - build the status JSON from live firmware state.
 */
#ifndef STATUS_H
#define STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "json.h"   /* STATUS_JSON_MAX */

/* Render one status object into buf. Returns its length (without NUL), or 0
 * if it did not fit. `seq` is stamped into the message. */
size_t status_render(char *buf, size_t cap, uint32_t seq);

/* Cheap fingerprint of everything that can change the message except the
 * counters (neighbor table generation, eth link, usb state). Servers push
 * when this changes. */
uint32_t status_fingerprint(void);

#endif /* STATUS_H */
