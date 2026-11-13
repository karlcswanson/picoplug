/*
 * remote.h - other testers on the network: advertise this one, find the
 * others, and fetch one's status for the page.
 *
 * Advertising: mDNS on the Ethernet side, host and DNS-SD instance name
 * picoplug-N (probe number N) or picoplug, service _picoplug._tcp on port 80,
 * TXT probe= ch= sw= port= pvid= fw=. Reachable on its IPv6 link-local
 * address with no DHCP, and on its DHCP address when it holds one. A name
 * another device already holds gets the last four MAC digits appended.
 * The USB side keeps answering as picoplug.local.
 *
 * Finding: nothing is sent until the page asks (GET /peer?scan=1, at most
 * every 10 s); the search then stays open, so announcements from testers
 * that come up later are heard too. Entries not heard for 150 s drop out.
 *
 * Viewing: GET /peer?name=NAME returns that tester's last /status.json,
 * fetched over HTTP from its link-local address (its DHCP address if it
 * has no AAAA record), refreshed every time the page asks.
 */
#ifndef REMOTE_H
#define REMOTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "peers.h"

#define REMOTE_SCAN_MIN_MS  10000
#define REMOTE_PEER_AGE_MS  150000
#define REMOTE_FETCH_MS     4000

void remote_init(const uint8_t mac[6]);
void remote_poll(void);
void remote_link_changed(bool up);
/* The probe number changed: rename and re-announce. */
void remote_name_changed(void);
/* Send a query (rate limited). */
void remote_scan(void);

typedef enum { REMOTE_OK = 0, REMOTE_PENDING, REMOTE_UNKNOWN, REMOTE_FAILED } remote_view_t;
/*
 * The page wants NAME's status: start a refresh, and copy the newest copy
 * (when there is one for that name) into out. *len gets its length, *age_ms
 * how old it is; err gets the last failure when nothing could be fetched.
 */
remote_view_t remote_view(const char *name, char *out, size_t cap, size_t *len, uint32_t *age_ms,
                          char *err, size_t errcap);

const peers_table_t *remote_peers(void);
const char *remote_name(void);        /* our own mDNS name on the Ethernet side */
uint32_t remote_generation(void);

#endif /* REMOTE_H */
