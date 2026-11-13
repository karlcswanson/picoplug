/*
 * peers.h - other picoplug testers on the port's network, found with mDNS.
 *
 * Every tester advertises the DNS-SD service _picoplug._tcp on the Ethernet
 * side (instance name = host name, e.g. picoplug-3), with a TXT record of
 * what it sees: its probe number, its switch and port, the port's PVID and
 * its firmware. A tester only asks (a PTR query for _picoplug._tcp.local)
 * while someone has its page open; answers and announcements land here.
 *
 * Pure: feed it the resource records from mDNS replies, read the table.
 * Host-tested. remote.c does the networking.
 */
#ifndef PEERS_H
#define PEERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PEERS_MAX        8
#define PEERS_NAME_MAX   32
#define PEERS_PROBE_NONE 0xFF
#define PEERS_SERVICE    "_picoplug"

/* DNS record types we read */
#define PEERS_RR_A     1
#define PEERS_RR_PTR   12
#define PEERS_RR_TXT   16
#define PEERS_RR_AAAA  28
#define PEERS_RR_SRV   33

typedef struct {
    bool     used;
    char     name[PEERS_NAME_MAX + 1];   /* mDNS instance and host name */
    uint8_t  probe;                      /* 0-9, PEERS_PROBE_NONE unset */
    char     chassis[33];                /* its switch's chassis ID, as formatted for the page */
    char     sys_name[33];               /* its switch's system name */
    char     port[25];                   /* its switch port */
    bool     has_pvid;
    uint16_t pvid;
    char     fw[12];
    bool     has_ip6, has_ip4;
    uint8_t  ip6[16];                    /* link-local, from AAAA */
    uint8_t  ip4[4];                     /* from A, when it holds a DHCP lease */
    uint16_t http_port;                  /* from SRV, 0 = not heard yet (80) */
    uint32_t last_ms;                    /* last record about it */
} peers_entry_t;

typedef struct {
    peers_entry_t p[PEERS_MAX];
    uint32_t      generation;            /* bumps when an entry appears, leaves or changes */
} peers_table_t;

void peers_reset(peers_table_t *t);
int  peers_count(const peers_table_t *t);
const peers_entry_t *peers_find(const peers_table_t *t, const char *name);

/*
 * One resource record from an mDNS reply. dom is the owner name and, for PTR,
 * rd is the target name, both in DNS label form, uncompressed. self (our own
 * instance name) is ignored. A and AAAA only fill in an entry a PTR, SRV or
 * TXT record already created. TTL 0 (a goodbye) removes the entry.
 * Returns true when the table changed.
 */
bool peers_record(peers_table_t *t, const char *self, uint16_t type, const uint8_t *dom, size_t domlen,
                  const uint8_t *rd, size_t rdlen, uint32_t ttl, uint32_t now_ms);

/* Drop entries not heard for max_age_ms. Returns true when any were dropped. */
bool peers_expire(peers_table_t *t, uint32_t now_ms, uint32_t max_age_ms);

/* Label i (0-based) of a DNS name in label form, as a C string. False when absent or too long. */
bool peers_label(const uint8_t *dom, size_t len, int i, char *out, size_t cap);

#endif /* PEERS_H */
