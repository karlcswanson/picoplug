/*
 * neighbor_table.c - see neighbor_table.h.
 */
#include "neighbor_table.h"

#include <string.h>

static neighbor_entry_t table[NEIGHBOR_MAX];
static uint32_t generation;

void neighbor_table_init(void)
{
    memset(table, 0, sizeof table);
    generation = 0;
}

static bool id_equal(const lldp_id_t *a, const lldp_id_t *b)
{
    return a->subtype == b->subtype && a->len == b->len && memcmp(a->data, b->data, a->len) == 0;
}

static int find(const lldp_neighbor_t *n)
{
    for (int i = 0; i < NEIGHBOR_MAX; i++) {
        if (table[i].used && id_equal(&table[i].n.chassis_id, &n->chassis_id) &&
            id_equal(&table[i].n.port_id, &n->port_id))
            return i;
    }
    return -1;
}

/* Compare the decoded content, ignoring nothing: the parser output is a
 * pure function of the frame, so identical frames give identical structs. */
static bool content_equal(const lldp_neighbor_t *a, const lldp_neighbor_t *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

static void remove_at(int i)
{
    /* keep order stable: shift down */
    for (int j = i; j < NEIGHBOR_MAX - 1; j++) table[j] = table[j + 1];
    memset(&table[NEIGHBOR_MAX - 1], 0, sizeof table[0]);
    generation++;
}

bool neighbor_table_update(const lldp_neighbor_t *n, uint32_t now_ms)
{
    int i = find(n);

    if (n->ttl == 0) {
        if (i >= 0) { remove_at(i); return true; }
        return false;
    }

    if (i < 0) {
        /* new: take a free slot, or evict the entry expiring soonest */
        for (i = 0; i < NEIGHBOR_MAX; i++) if (!table[i].used) break;
        if (i == NEIGHBOR_MAX) {
            i = 0;
            for (int j = 1; j < NEIGHBOR_MAX; j++)
                if ((int32_t)(table[j].expires_ms - table[i].expires_ms) < 0) i = j;
        }
        table[i].used = true;
        table[i].n = *n;
        table[i].first_seen_ms = now_ms;
        table[i].last_seen_ms = now_ms;
        table[i].expires_ms = now_ms + (uint32_t)n->ttl * 1000u;
        generation++;
        return true;
    }

    bool changed = !content_equal(&table[i].n, n);
    table[i].n = *n;
    table[i].last_seen_ms = now_ms;
    table[i].expires_ms = now_ms + (uint32_t)n->ttl * 1000u;
    if (changed) generation++;
    return changed;
}

bool neighbor_table_expire(uint32_t now_ms)
{
    bool any = false;
    for (int i = 0; i < NEIGHBOR_MAX; i++) {
        if (table[i].used && (int32_t)(now_ms - table[i].expires_ms) >= 0) {
            remove_at(i);
            any = true;
            i--; /* re-check the entry shifted into this slot */
        }
    }
    return any;
}

int neighbor_table_clear(void)
{
    int n = 0;
    for (int i = 0; i < NEIGHBOR_MAX; i++) if (table[i].used) n++;
    memset(table, 0, sizeof table);
    if (n) generation++;
    return n;
}

int neighbor_table_count(void)
{
    int c = 0;
    for (int i = 0; i < NEIGHBOR_MAX; i++) if (table[i].used) c++;
    return c;
}

const neighbor_entry_t *neighbor_table_get(int i)
{
    int seen = 0;
    for (int k = 0; k < NEIGHBOR_MAX; k++) {
        if (!table[k].used) continue;
        if (seen == i) return &table[k];
        seen++;
    }
    return 0;
}

uint32_t neighbor_table_generation(void) { return generation; }

uint32_t neighbor_entry_ttl_remaining_s(const neighbor_entry_t *e, uint32_t now_ms)
{
    int32_t left = (int32_t)(e->expires_ms - now_ms);
    return left > 0 ? (uint32_t)left / 1000u : 0;
}
