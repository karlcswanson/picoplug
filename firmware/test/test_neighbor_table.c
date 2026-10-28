/*
 * Host-side tests for neighbor_table.c (hardware independent).
 */
#include "neighbor_table.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

static void make(lldp_neighbor_t *n, const char *chassis, const char *port, uint16_t ttl)
{
    memset(n, 0, sizeof *n);
    n->chassis_id.subtype = LLDP_CHASSIS_LOCAL;
    n->chassis_id.len = (uint8_t)strlen(chassis);
    memcpy(n->chassis_id.data, chassis, n->chassis_id.len);
    n->port_id.subtype = LLDP_PORT_LOCAL;
    n->port_id.len = (uint8_t)strlen(port);
    memcpy(n->port_id.data, port, n->port_id.len);
    n->ttl = ttl;
}

int main(void)
{
    lldp_neighbor_t a, b;
    neighbor_table_init();
    CHECK(neighbor_table_count() == 0);
    CHECK(neighbor_table_get(0) == NULL);

    /* add */
    make(&a, "sw1", "1", 120);
    CHECK(neighbor_table_update(&a, 1000) == true);
    CHECK(neighbor_table_count() == 1);
    uint32_t g = neighbor_table_generation();
    CHECK(g == 1);
    const neighbor_entry_t *e = neighbor_table_get(0);
    CHECK(e && e->first_seen_ms == 1000 && e->expires_ms == 121000);
    CHECK(neighbor_entry_ttl_remaining_s(e, 1000) == 120);
    CHECK(neighbor_entry_ttl_remaining_s(e, 61000) == 60);
    CHECK(neighbor_entry_ttl_remaining_s(e, 200000) == 0);

    /* identical refresh: no change, timestamps move */
    CHECK(neighbor_table_update(&a, 31000) == false);
    CHECK(neighbor_table_generation() == g);
    e = neighbor_table_get(0);
    CHECK(e->last_seen_ms == 31000 && e->expires_ms == 151000 && e->first_seen_ms == 1000);

    /* content change on same key: changed */
    a.has_sys_name = true;
    strcpy(a.sys_name, "core");
    CHECK(neighbor_table_update(&a, 32000) == true);
    CHECK(neighbor_table_generation() == g + 1);
    CHECK(strcmp(neighbor_table_get(0)->n.sys_name, "core") == 0);

    /* second key */
    make(&b, "sw1", "2", 10);
    CHECK(neighbor_table_update(&b, 32000) == true);
    CHECK(neighbor_table_count() == 2);

    /* expire b only */
    {   /* clear drops everything at once and bumps the generation exactly once */
        uint32_t gc = neighbor_table_generation();
        int had = neighbor_table_count();
        CHECK(had > 0);
        CHECK(neighbor_table_clear() == had);
        CHECK(neighbor_table_count() == 0);
        CHECK(neighbor_table_generation() == gc + 1);
        CHECK(neighbor_table_clear() == 0);
        CHECK(neighbor_table_generation() == gc + 1);
        neighbor_table_update(&a, 1000);   /* put both back for the expiry checks below */
        neighbor_table_update(&b, 32000);
    }
    CHECK(neighbor_table_expire(41999) == false);
    CHECK(neighbor_table_expire(42000) == true);
    CHECK(neighbor_table_count() == 1);
    CHECK(neighbor_table_get(0)->n.port_id.data[0] == '1');

    /* ttl 0 = shutdown removes */
    a.ttl = 0;
    CHECK(neighbor_table_update(&a, 43000) == true);
    CHECK(neighbor_table_count() == 0);
    CHECK(neighbor_table_update(&a, 43000) == false); /* already gone */

    /* fill table and evict the soonest-expiring */
    char port[8];
    for (int i = 0; i < NEIGHBOR_MAX; i++) {
        snprintf(port, sizeof port, "p%d", i);
        make(&b, "sw2", port, (uint16_t)(100 + i * 10));
        CHECK(neighbor_table_update(&b, 50000) == true);
    }
    CHECK(neighbor_table_count() == NEIGHBOR_MAX);
    make(&b, "sw2", "extra", 120);
    CHECK(neighbor_table_update(&b, 50000) == true);
    CHECK(neighbor_table_count() == NEIGHBOR_MAX);
    bool found_p0 = false, found_extra = false;
    for (int i = 0; i < neighbor_table_count(); i++) {
        const neighbor_entry_t *x = neighbor_table_get(i);
        if (memcmp(x->n.port_id.data, "p0", 2) == 0 && x->n.port_id.len == 2) found_p0 = true;
        if (x->n.port_id.len == 5) found_extra = true;
    }
    CHECK(!found_p0 && found_extra);

    /* wraparound-safe expiry near uint32 max */
    neighbor_table_init();
    make(&a, "sw3", "1", 10);
    CHECK(neighbor_table_update(&a, 0xFFFFF000u) == true);
    CHECK(neighbor_table_expire(0xFFFFFF00u) == false);
    CHECK(neighbor_table_expire(0x00001000u) == false);  /* 4 s past wrap, expires at +10 s */
    CHECK(neighbor_table_expire(0x00002800u) == true);
    CHECK(neighbor_table_count() == 0);

    fprintf(stderr, "neighbor_table: ok\n");
    return 0;
}
