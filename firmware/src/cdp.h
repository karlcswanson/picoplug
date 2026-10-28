/*
 * cdp.h - Cisco Discovery Protocol (v1/v2) frame parser.
 *
 * Pure C99, no heap, no hardware dependencies, same rules as lldp.h. Fills
 * the unified neighbor structure (lldp_neighbor_t) so the neighbor table,
 * JSON and page treat CDP and LLDP neighbors alike.
 *
 * Frame: 802.3 length field, LLC AA AA 03, SNAP 00 00 0C 20 00, then
 * version(1) ttl(1) checksum(2) and TLVs of type(2) len(2, incl. header).
 */
#ifndef CDP_H
#define CDP_H

#include "lldp.h"

#define CDP_ETHERTYPE_SNAP_PID  0x2000

typedef enum {
    CDP_OK = 0,
    CDP_ERR_TOO_SHORT,
    CDP_ERR_NOT_CDP,          /* not the CDP multicast / SNAP signature */
    CDP_ERR_BAD_VERSION,      /* version byte not 1 or 2                */
    CDP_ERR_TRUNCATED,        /* a TLV length runs past the frame end   */
    CDP_ERR_NULL,
} cdp_result_t;

/* CDP TLV types */
enum {
    CDP_TLV_DEVICE_ID     = 0x01,
    CDP_TLV_ADDRESSES     = 0x02,
    CDP_TLV_PORT_ID       = 0x03,
    CDP_TLV_CAPABILITIES  = 0x04,
    CDP_TLV_SW_VERSION    = 0x05,
    CDP_TLV_PLATFORM      = 0x06,
    CDP_TLV_IP_PREFIX     = 0x07,
    CDP_TLV_PROTO_HELLO   = 0x08,
    CDP_TLV_VTP_DOMAIN    = 0x09,
    CDP_TLV_NATIVE_VLAN   = 0x0A,
    CDP_TLV_DUPLEX        = 0x0B,
    CDP_TLV_VOIP_VLAN     = 0x0E,
    CDP_TLV_VOIP_QUERY    = 0x0F,
    CDP_TLV_POWER         = 0x10,
    CDP_TLV_MTU           = 0x11,
    CDP_TLV_TRUST_BITMAP  = 0x12,
    CDP_TLV_UNTRUSTED_COS = 0x13,
    CDP_TLV_SYSTEM_NAME   = 0x14,
    CDP_TLV_SYSTEM_OID    = 0x15,
    CDP_TLV_MGMT_ADDR     = 0x16,
    CDP_TLV_LOCATION      = 0x17,
    CDP_TLV_POWER_REQUEST = 0x19,
    CDP_TLV_POWER_AVAIL   = 0x1A,
};

/* CDP capability bits */
enum {
    CDP_CAP_ROUTER      = 0x001,
    CDP_CAP_TB_BRIDGE   = 0x002,
    CDP_CAP_SR_BRIDGE   = 0x004,
    CDP_CAP_SWITCH      = 0x008,
    CDP_CAP_HOST        = 0x010,
    CDP_CAP_IGMP        = 0x020,
    CDP_CAP_REPEATER    = 0x040,
    CDP_CAP_PHONE       = 0x080,
    CDP_CAP_REMOTE_MGMT = 0x100,
    CDP_CAP_CVTA        = 0x200,
    CDP_CAP_MAC_RELAY   = 0x400,
};

/* True if the frame is addressed to the CDP multicast with the CDP SNAP
 * header (cheap pre-check before parsing). */
bool cdp_is_cdp_frame(const uint8_t *frame, size_t len);

/* Cisco's odd-length Internet checksum over a CDP payload (version byte to
 * end). With the checksum field zeroed it returns the value to store; with
 * the field filled in it returns 0 for a valid payload. */
uint16_t cdp_checksum(const uint8_t *d, size_t n);

cdp_result_t cdp_parse(const uint8_t *frame, size_t len, lldp_neighbor_t *out);
const char *cdp_result_str(cdp_result_t r);
const char *cdp_cap_name(unsigned bit);

#endif /* CDP_H */
