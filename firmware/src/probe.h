/*
 * probe.h - "does this port go anywhere?" checks on the Ethernet side.
 *
 * Once the DHCP client holds a lease: ARP the gateway, ping it, ping an
 * internet address, resolve a name via the DHCP-supplied DNS. Non-blocking
 * state machine driven from the main loop; repeats every PROBE_PERIOD_S.
 */
#ifndef PROBE_H
#define PROBE_H

#include <stdbool.h>
#include <stdint.h>

#define PROBE_PERIOD_S     30
#define PROBE_PINGS        3
#define PROBE_INET_TARGET  "1.1.1.1"
#define PROBE_DNS_NAME     "clockclub.net"

typedef struct {
    bool     sent_any;
    uint8_t  sent, replies;
    uint16_t best_rtt_ms;
} probe_ping_t;

typedef struct {
    bool     valid;               /* at least one run completed         */
    bool     running;
    const char *phase;            /* "idle", "arp", "ping_gw", ...      */
    uint32_t runs;
    uint32_t completed_at_s;      /* uptime seconds of the last completion */
    bool     gw_arp_ok;
    uint8_t  gw_mac[6];
    probe_ping_t gw, inet;
    bool     dns_done, dns_ok;
    char     dns_addr[16];
    char     dns_server[16];      /* resolver the lookup went to            */
    bool     dns_custom;          /* from settings, not DHCP                */
    uint16_t dns_ms;
} probe_result_t;

bool probe_init(void);
void probe_poll(void);
/* Force a new run as soon as a lease is available. */
void probe_restart(void);
const probe_result_t *probe_result(void);

#endif /* PROBE_H */
