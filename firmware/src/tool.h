/*
 * tool.h - on-demand ping and traceroute from the Ethernet port.
 *
 * One tool runs at a time, started from GET /tool, driven from the main
 * loop, results in /status.json ("tool") so the page just keeps polling.
 * Uses its own raw ICMP pcb next to the probe's; both ignore replies that
 * are not theirs, so they coexist.
 */
#ifndef TOOL_H
#define TOOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TOOL_HOST_MAX    63
#define TOOL_PING_MAX    60             /* finite count limit = size of the RTT window */
#define TOOL_PING_CONT_MAX 900          /* continuous mode stops itself after this many (15 min) */
#define TOOL_TRACE_HOPS  20
#define TOOL_RTT_TIMEOUT 0xFFFF

typedef enum { TOOL_IDLE = 0, TOOL_PING, TOOL_TRACE } tool_kind_t;

typedef struct {
    uint8_t  ttl;
    bool     answered;
    bool     reached;            /* echo reply: this hop is the target     */
    bool     unreachable;        /* ICMP destination unreachable from here */
    uint8_t  addr[4];
    uint16_t ms;
} tool_hop_t;

typedef struct {
    tool_kind_t kind;
    bool     running;
    char     host[TOOL_HOST_MAX + 1];
    char     error[48];          /* "" = none                              */
    bool     resolved;
    uint8_t  addr[4];
    uint32_t started_ms, finished_ms;
    /* ping */
    uint16_t count, sent, recv;     /* count 0 = continuous (until stop or TOOL_PING_CONT_MAX) */
    uint16_t min_ms, max_ms, last_ms;
    uint32_t sum_ms;
    uint16_t rtts[TOOL_PING_MAX];   /* ring of the last TOOL_PING_MAX probes, slot (seq-1) % TOOL_PING_MAX; TOOL_RTT_TIMEOUT = no reply */
    /* trace */
    tool_hop_t hops[TOOL_TRACE_HOPS];
    uint8_t  n_hops;
    bool     reached;
    uint32_t generation;         /* bumps on every visible change          */
} tool_state_t;

bool tool_init(void);
void tool_poll(void);
/* host: IPv4 literal or name (letters, digits, dots, dashes). count: pings, 1..TOOL_PING_MAX, or 0 for continuous.
 * Returns false with err filled if the tool could not start. */
bool tool_start(tool_kind_t kind, const char *host, uint16_t count, char *err, size_t errcap);
void tool_stop(void);
const tool_state_t *tool_get(void);

#endif /* TOOL_H */
