/*
 * settings.h - user settings: what the port probe pings and resolves, and
 * which protocols we advertise. settings.c is pure (encode/decode/validate,
 * host-tested); settings_store.c keeps a copy in the last flash sector.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_DNS_NAME_MAX 63
#define SETTINGS_DEFAULT_LLDP_ON 1
#define SETTINGS_DEFAULT_CDP_ON  1
#define SETTINGS_DEFAULT_USB_IP  "192.168.7.1"
#define SETTINGS_PROBE_NONE 0xFF  /* no probe number set                  */
#define SETTINGS_FORM_URL_MAX 255
/* {name} placeholders are filled in by the page; see docs/PROTOCOL.md */
/* the picoplug share page; {share} is every port detail as name=value pairs (see the Share link in docs/PROTOCOL.md) */
#define SETTINGS_DEFAULT_FORM_URL "https://clockclub.net/apps/picoplug/#{share}"

typedef struct {
    char ping_target[16];           /* IPv4 dotted, never empty            */
    char dns_name[SETTINGS_DNS_NAME_MAX + 1];
    char dns_server[16];            /* IPv4 dotted, "" = from DHCP         */
    bool lldp_on, cdp_on;           /* advertise at boot                   */
    char usb_ip[16];                /* the device's own address on the USB link; /24, host gets .ip+1 */
    uint8_t probe;                  /* 0-9, colored by the resistor code; SETTINGS_PROBE_NONE unset */
    char form_url[SETTINGS_FORM_URL_MAX + 1];   /* link template behind the Port view's Share button, "" = no link */
    bool has_mac;                   /* eth_mac below picked yet           */
    uint8_t eth_mac[3];             /* low 3 bytes of the port's MAC, random, picked on the first boot:
                                     * the W55RP20's flash ID is the same on every unit */
} settings_t;

#define SETTINGS_BLOB_MAX 512       /* encoded size never exceeds this     */

void   settings_defaults(settings_t *s);
/* Serialize with magic, version and CRC. Returns bytes written, 0 if cap is too small. */
size_t settings_encode(const settings_t *s, uint8_t *buf, size_t cap);
/* Returns false (and leaves *out untouched) unless the blob is intact. */
bool   settings_decode(const uint8_t *buf, size_t len, settings_t *out);
/* Apply "key=value&key=value" (URL-encoded) to *s. Keys: ping_target,
 * dns_name, dns_server, lldp, cdp, usb_ip, probe ("" = none), form_url (http(s) URL or ""),
 * reset (keeps probe, form_url and the MAC). On failure *s is unchanged and
 * err names the problem. Returns true if the query was valid. changed is
 * set when any field differs afterwards. */
bool   settings_apply_query(settings_t *s, const char *query, char *err, size_t errcap, bool *changed);
bool   settings_parse_ip(const char *s, uint8_t out[4]);
/* Host address for the USB peer: usb_ip with the last octet +1 (or -1 at .254). */
void   settings_usb_peer_ip(const settings_t *s, uint8_t out[4]);
/* Name we advertise in LLDP and CDP: "picoplug", or "picoplug-3" with a probe number. */
void   settings_device_name(const settings_t *s, char *out, size_t cap);

/* ---- firmware side (settings_store.c) ---------------------------------- */
void   settings_init(void);                 /* load from flash or defaults  */
const settings_t *settings_get(void);
bool   settings_save(const settings_t *s);  /* write to flash, ~100 ms with IRQs off */
uint32_t settings_generation(void);

#endif /* SETTINGS_H */
