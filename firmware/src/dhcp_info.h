/*
 * dhcp_info.h - everything the DHCP server told us, decoded from the wire.
 *
 * lwIP's client only keeps the handful of options it needs. This module
 * watches the raw frames (both directions) that eth_net.c already sees,
 * decodes OFFER/ACK/NAK addressed to our MAC, and keeps counters: how
 * many servers answered, how long the lease took, whether a relay was in
 * the path. Pure code with its own state; host-tested.
 */
#ifndef DHCP_INFO_H
#define DHCP_INFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DHCP_INFO_MAX_IPS   3
#define DHCP_INFO_MAX_OPTS  32
#define DHCP_INFO_STR       64
#define DHCP_INFO_VENDOR    32
#define DHCP_INFO_RAW       224     /* raw code/len/value bytes kept for the page */

typedef struct {
    bool     valid;
    uint8_t  msg_type;                      /* 2 OFFER, 5 ACK, 6 NAK            */
    uint8_t  server_mac[6];                 /* Ethernet source of the reply     */
    uint8_t  server_id[4];                  /* option 54                        */
    uint8_t  yiaddr[4], siaddr[4], giaddr[4];
    bool     relayed;                       /* giaddr != 0                      */
    uint32_t lease_s, t1_s, t2_s;           /* 51, 58, 59                       */
    uint8_t  routers[DHCP_INFO_MAX_IPS][4]; uint8_t n_routers;   /* 3   */
    uint8_t  dns[DHCP_INFO_MAX_IPS][4];     uint8_t n_dns;       /* 6   */
    uint8_t  ntp[DHCP_INFO_MAX_IPS][4];     uint8_t n_ntp;       /* 42  */
    uint8_t  wins[2][4];                    uint8_t n_wins;      /* 44  */
    uint8_t  tftp_ips[2][4];                uint8_t n_tftp_ips;  /* 150 */
    bool     has_broadcast; uint8_t broadcast[4];                /* 28  */
    char     domain[DHCP_INFO_STR + 1];     /* 15                               */
    char     search[DHCP_INFO_STR * 2 + 1]; /* 119, space separated             */
    char     tftp_name[DHCP_INFO_STR + 1];  /* 66, else BOOTP sname             */
    char     bootfile[DHCP_INFO_STR + 1];   /* 67, else BOOTP file              */
    char     wpad[DHCP_INFO_STR * 2 + 1];   /* 252                              */
    uint8_t  vendor[DHCP_INFO_VENDOR]; uint8_t vendor_len; uint16_t vendor_total;   /* 43 */
    uint8_t  opts[DHCP_INFO_MAX_OPTS]; uint8_t n_opts;           /* option codes present, in order */
    uint8_t  raw[DHCP_INFO_RAW]; uint16_t raw_len; bool raw_truncated;   /* options as received, pads and end removed */
    bool     truncated;
} dhcp_reply_t;

typedef struct {
    dhcp_reply_t ack;                       /* last ACK (or OFFER until an ACK arrives) */
    uint32_t discovers, offers, acks, naks; /* since reset                      */
    uint8_t  servers[4][4]; uint8_t n_servers;   /* distinct server IDs that answered */
    uint32_t discover_ms;                   /* 0 = none pending                 */
    uint32_t acquire_ms;                    /* DISCOVER -> ACK, 0 = unknown     */
    uint32_t generation;
} dhcp_info_state_t;

/* Decode one DHCP reply frame addressed to our_mac. Returns false if the
 * frame is not such a reply. */
bool dhcp_info_parse_reply(const uint8_t *frame, size_t len, const uint8_t our_mac[6], dhcp_reply_t *out);

void dhcp_info_init(const uint8_t our_mac[6]);
void dhcp_info_reset(void);                 /* link-up / DHCP restart            */
void dhcp_info_observe(const uint8_t *frame, size_t len, bool tx, uint32_t now_ms);
const dhcp_info_state_t *dhcp_info_get(void);

#endif /* DHCP_INFO_H */
