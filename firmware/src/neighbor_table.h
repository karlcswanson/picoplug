/*
 * neighbor_table.h - LLDP neighbors keyed by chassis ID + port ID, aged by TTL.
 */
#ifndef NEIGHBOR_TABLE_H
#define NEIGHBOR_TABLE_H

#include "lldp.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef NEIGHBOR_MAX
#define NEIGHBOR_MAX 4
#endif

typedef struct {
    bool           used;
    lldp_neighbor_t n;
    uint32_t       first_seen_ms;
    uint32_t       last_seen_ms;
    uint32_t       expires_ms;     /* last_seen_ms + ttl * 1000 */
} neighbor_entry_t;

void neighbor_table_init(void);

/*
 * Insert or refresh a neighbor. Returns true if the table changed in a way
 * a client should see: new neighbor, or any decoded field differs from the
 * stored copy. A refresh with identical content only updates timestamps.
 * A TTL of 0 (shutdown LLDPDU) removes the entry.
 */
bool neighbor_table_update(const lldp_neighbor_t *n, uint32_t now_ms);

/* Remove expired entries. Returns true if any were removed. */
bool neighbor_table_expire(uint32_t now_ms);
/* Drop every entry (link went down). Returns the number removed; bumps the generation if any. */
int  neighbor_table_clear(void);

int neighbor_table_count(void);

/* i-th live entry (0 <= i < count), or NULL. Order is stable between calls
 * unless entries are added or removed. */
const neighbor_entry_t *neighbor_table_get(int i);

/* Incremented on every change (add, update, remove). */
uint32_t neighbor_table_generation(void);

/* Seconds of TTL left for an entry at `now_ms` (0 if expired). */
uint32_t neighbor_entry_ttl_remaining_s(const neighbor_entry_t *e, uint32_t now_ms);

#endif /* NEIGHBOR_TABLE_H */
