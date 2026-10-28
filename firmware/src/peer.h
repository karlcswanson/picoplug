/*
 * peer.h - picolldp-to-picolldp protocol: find other probes on the wire and
 * tell whether the run between two of them is a direct cable or a switched
 * path. Pure (encode/decode, peer table, verdict), host-tested; the
 * firmware side that sends, times and blinks is path.c.
 *
 * Frames use the IEEE local experimental EtherType 0x88B5, so no IP is
 * needed and the test works on a bare cable with no DHCP. HELLO is
 * broadcast; everything else is unicast to the peer.
 *
 * Path test: A asks B to power its PHY down once (BLINK_REQ), B confirms
 * (BLINK_ACK) and drops its link for off_ms. A watches its own link and
 * whether B keeps answering echoes. A's link dropping in step means a
 * direct (or link-transparent) run; A's link staying up while B goes
 * silent means a switch is in between.
 */
#ifndef PEER_H
#define PEER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PEER_ETHERTYPE   0x88B5
#define PEER_VERSION     1
#define PEER_MAX         8
#define PEER_MACS_MAX    32
#define PEER_PROBE_NONE  0xFF

enum { PEER_HELLO = 1, PEER_ECHO_REQ = 2, PEER_ECHO_REP = 3, PEER_BLINK_REQ = 4, PEER_BLINK_ACK = 5 };

typedef struct {
    uint8_t  type;
    uint8_t  dst[6], src[6];
    uint16_t seq;
    /* HELLO */
    uint32_t uptime_s;
    uint8_t  speed;              /* 10 / 100, 0 unknown                    */
    bool     full;
    uint16_t pvid;               /* from the probe's LLDP neighbor, 0 none  */
    char     chassis[33];        /* its neighbor's chassis ID, "" none      */
    char     port[25];           /* its neighbor's port ID                  */
    char     fw[9];
    uint8_t  probe;              /* its probe number 0-9, PEER_PROBE_NONE unset */
    /* ECHO */
    uint32_t t_ms;               /* sender's clock, echoed back             */
    /* BLINK */
    uint16_t token;
    uint16_t off_ms;
} peer_msg_t;

bool   peer_is_peer(const uint8_t *frame, size_t len);
bool   peer_parse(const uint8_t *frame, size_t len, peer_msg_t *out);
/* Whole Ethernet frame, padded to 60 bytes. Returns its length, 0 if cap is too small. */
size_t peer_encode(const peer_msg_t *m, uint8_t *buf, size_t cap);
const char *peer_type_name(uint8_t t);

typedef struct {
    bool       used;
    uint8_t    mac[6];
    uint32_t   first_ms, last_ms;
    uint32_t   hellos;
    peer_msg_t info;             /* last HELLO                              */
    uint32_t   echoes;           /* echo replies received                   */
    uint32_t   rtt_last_ms, rtt_min_ms;
} peer_t;

typedef struct {
    peer_t   p[PEER_MAX];
    uint8_t  macs[PEER_MACS_MAX][6];     /* distinct source MACs since link-up */
    uint8_t  nmacs;
    bool     macs_full;
    uint32_t generation;                 /* bumps when a peer appears, leaves or changes info */
} peer_table_t;

void    peer_table_reset(peer_table_t *t);
void    peer_census_reset(peer_table_t *t);
/* Count a source MAC (skip ours). */
void    peer_census(peer_table_t *t, const uint8_t mac[6], const uint8_t self[6]);
peer_t *peer_find(peer_table_t *t, const uint8_t mac[6]);
/* Add or refresh from a HELLO. Returns the entry, NULL if the table is full. */
peer_t *peer_hello(peer_table_t *t, const peer_msg_t *m, uint32_t now_ms);
void    peer_echo_reply(peer_table_t *t, const peer_msg_t *m, uint32_t now_ms);
/* Drop peers not heard for max_age_ms. */
void    peer_expire(peer_table_t *t, uint32_t now_ms, uint32_t max_age_ms);
int     peer_count(const peer_table_t *t);

typedef enum {
    PATH_NONE = 0,
    PATH_DIRECT,                 /* our link dropped with the peer's        */
    PATH_SWITCHED,               /* our link stayed up, peer went silent and came back */
    PATH_SWITCHED_PEER_LOST,     /* as above, but the peer never came back  */
    PATH_NO_ANSWER,              /* peer never confirmed the blink request  */
    PATH_INCONCLUSIVE,           /* peer confirmed but kept answering       */
} path_verdict_t;

path_verdict_t path_verdict(bool acked, bool own_link_dropped, bool peer_went_silent, bool peer_came_back);
const char *path_verdict_name(path_verdict_t v);   /* "direct" "switched" ... */

#endif /* PEER_H */
