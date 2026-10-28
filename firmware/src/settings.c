/*
 * settings.c - see settings.h.
 */
#include "settings.h"
#include "probe.h"          /* PROBE_INET_TARGET, PROBE_DNS_NAME defaults */

#include <stdio.h>
#include <string.h>
#include "query.h"

#define MAGIC   0x50434C53u     /* "PCLS" */
#define VERSION 6u          /* 1: without usb_ip; 2, 4: without probe; 3: had a trailing lan_page byte (removed 2026-10-25); 5: without form_url */

/* blob: magic(4) version(2) len(2) payload crc(4); payload is the struct
 * laid out field by field so the on-flash format does not depend on
 * compiler padding. */

static uint32_t crc32(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void settings_defaults(settings_t *s)
{
    memset(s, 0, sizeof *s);
    strncpy(s->ping_target, PROBE_INET_TARGET, sizeof s->ping_target - 1);
    strncpy(s->dns_name, PROBE_DNS_NAME, sizeof s->dns_name - 1);
    s->lldp_on = SETTINGS_DEFAULT_LLDP_ON;
    s->cdp_on = SETTINGS_DEFAULT_CDP_ON;
    strncpy(s->usb_ip, SETTINGS_DEFAULT_USB_IP, sizeof s->usb_ip - 1);
    s->probe = SETTINGS_PROBE_NONE;
    strncpy(s->form_url, SETTINGS_DEFAULT_FORM_URL, sizeof s->form_url - 1);
}

#define PAYLOAD_LEN_V1 (16 + (SETTINGS_DNS_NAME_MAX + 1) + 16 + 2)
#define PAYLOAD_LEN_V2 (PAYLOAD_LEN_V1 + 16)
#define PAYLOAD_LEN_V3 (PAYLOAD_LEN_V2 + 1)
#define PAYLOAD_LEN_V5 (PAYLOAD_LEN_V2 + 1)
#define PAYLOAD_LEN (PAYLOAD_LEN_V5 + SETTINGS_FORM_URL_MAX + 1)

size_t settings_encode(const settings_t *s, uint8_t *buf, size_t cap)
{
    size_t total = 8 + PAYLOAD_LEN + 4;
    if (cap < total) return 0;
    uint8_t *p = buf;
    *p++ = MAGIC >> 24; *p++ = (uint8_t)(MAGIC >> 16); *p++ = (uint8_t)(MAGIC >> 8); *p++ = (uint8_t)MAGIC;
    *p++ = VERSION >> 8; *p++ = (uint8_t)VERSION;
    *p++ = PAYLOAD_LEN >> 8; *p++ = (uint8_t)PAYLOAD_LEN;
    memcpy(p, s->ping_target, 16); p += 16;
    memcpy(p, s->dns_name, SETTINGS_DNS_NAME_MAX + 1); p += SETTINGS_DNS_NAME_MAX + 1;
    memcpy(p, s->dns_server, 16); p += 16;
    *p++ = s->lldp_on ? 1 : 0;
    *p++ = s->cdp_on ? 1 : 0;
    memcpy(p, s->usb_ip, 16); p += 16;
    *p++ = s->probe;
    memcpy(p, s->form_url, SETTINGS_FORM_URL_MAX + 1); p += SETTINGS_FORM_URL_MAX + 1;
    uint32_t c = crc32(buf, (size_t)(p - buf));
    *p++ = (uint8_t)(c >> 24); *p++ = (uint8_t)(c >> 16); *p++ = (uint8_t)(c >> 8); *p++ = (uint8_t)c;
    return (size_t)(p - buf);
}

/* "" or an http(s) URL of printable ASCII without spaces, quotes or backslashes, so it can go into JSON as is */
static bool valid_form_url(const char *v)
{
    if (strncmp(v, "http://", 7) && strncmp(v, "https://", 8)) return false;
    for (const char *p = v; *p; p++)
        if (*p <= ' ' || *p > '~' || *p == '"' || *p == '\\') return false;
    return strlen(v) <= SETTINGS_FORM_URL_MAX;
}

bool settings_decode(const uint8_t *buf, size_t len, settings_t *out)
{
    if (!buf || len < 12) return false;
    uint32_t magic = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    if (magic != MAGIC) return false;
    unsigned ver = (unsigned)(buf[4] << 8 | buf[5]);
    size_t plen = (size_t)(buf[6] << 8 | buf[7]);
    if (!((ver == 1 && plen == PAYLOAD_LEN_V1) || (ver == 2 && plen == PAYLOAD_LEN_V2) || (ver == 3 && plen == PAYLOAD_LEN_V3) || (ver == 4 && plen == PAYLOAD_LEN_V2) || (ver == 5 && plen == PAYLOAD_LEN_V5) || (ver == VERSION && plen == PAYLOAD_LEN)) || len < 8 + plen + 4) return false;
    const uint8_t *c = buf + 8 + plen;
    uint32_t want = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) | ((uint32_t)c[2] << 8) | c[3];
    if (crc32(buf, 8 + plen) != want) return false;
    settings_t s;
    memset(&s, 0, sizeof s);
    const uint8_t *p = buf + 8;
    memcpy(s.ping_target, p, 16); p += 16;
    memcpy(s.dns_name, p, SETTINGS_DNS_NAME_MAX + 1); p += SETTINGS_DNS_NAME_MAX + 1;
    memcpy(s.dns_server, p, 16); p += 16;
    s.lldp_on = p[0] != 0; s.cdp_on = p[1] != 0; p += 2;
    if (ver >= 2) { memcpy(s.usb_ip, p, 16); p += 16; } else strncpy(s.usb_ip, SETTINGS_DEFAULT_USB_IP, sizeof s.usb_ip - 1);
    s.usb_ip[15] = '\0';
    s.probe = ver >= 5 && *p <= 9 ? *p : SETTINGS_PROBE_NONE;
    if (ver >= 5) p++;
    if (ver >= 6) { memcpy(s.form_url, p, SETTINGS_FORM_URL_MAX + 1); s.form_url[SETTINGS_FORM_URL_MAX] = '\0'; }
    else strncpy(s.form_url, SETTINGS_DEFAULT_FORM_URL, sizeof s.form_url - 1);
    s.ping_target[15] = s.dns_server[15] = s.dns_name[SETTINGS_DNS_NAME_MAX] = '\0';
    uint8_t ip[4];
    if (!settings_parse_ip(s.ping_target, ip)) return false;
    if (s.dns_server[0] && !settings_parse_ip(s.dns_server, ip)) return false;
    if (!settings_parse_ip(s.usb_ip, ip)) return false;
    if (s.form_url[0] && !valid_form_url(s.form_url)) return false;
    *out = s;
    return true;
}

bool settings_parse_ip(const char *s, uint8_t out[4])
{
    if (!s) return false;
    for (int i = 0; i < 4; i++) {
        unsigned v = 0; int digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned)(*s++ - '0'); if (++digits > 3 || v > 255) return false; }
        if (!digits) return false;
        out[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return false; s++; }
    }
    return *s == '\0';
}

/* A unicast host address we can put on the USB link: not 0/127/224+, host part not 0 or 255. */
static bool valid_usb_ip(const char *v)
{
    uint8_t ip[4];
    if (!settings_parse_ip(v, ip)) return false;
    if (ip[0] == 0 || ip[0] == 127 || ip[0] >= 224) return false;
    if (ip[3] == 0 || ip[3] == 255) return false;
    return true;
}

void settings_usb_peer_ip(const settings_t *s, uint8_t out[4])
{
    if (!settings_parse_ip(s->usb_ip, out)) settings_parse_ip(SETTINGS_DEFAULT_USB_IP, out);
    out[3] = (uint8_t)(out[3] < 254 ? out[3] + 1 : out[3] - 1);
}

void settings_device_name(const settings_t *s, char *out, size_t cap)
{
    if (s->probe <= 9) snprintf(out, cap, "picoplug-%u", (unsigned)s->probe);
    else snprintf(out, cap, "picoplug");
}

static bool valid_name(const char *n)
{
    size_t len = strlen(n);
    if (!len || len > SETTINGS_DNS_NAME_MAX || n[0] == '.' || n[len - 1] == '.' || n[0] == '-') return false;
    for (const char *p = n; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
        if (c == '.' && p[1] == '.') return false;
    }
    return true;
}


bool settings_apply_query(settings_t *s, const char *query, char *err, size_t errcap, bool *changed)
{
    settings_t n = *s;
    if (err && errcap) err[0] = '\0';
    const char *q = query ? query : "", *k, *vsrc; size_t klen, vlen;
    while (query_next(&q, &k, &klen, &vsrc, &vlen)) {
        char key[16], val[SETTINGS_FORM_URL_MAX + 1];
        if (klen >= sizeof key) { snprintf(err, errcap, "unknown field"); return false; }
        memcpy(key, k, klen); key[klen] = '\0';
        if (!query_decode(vsrc, vlen, val, sizeof val)) { snprintf(err, errcap, "%s: bad encoding or too long", key); return false; }
        uint8_t ip[4];
        if (strcmp(key, "ping_target") == 0) {
            if (!settings_parse_ip(val, ip)) { snprintf(err, errcap, "ping_target: not an IPv4 address"); return false; }
            memcpy(n.ping_target, val, strlen(val) + 1);      /* validated: at most 15 chars */
        } else if (strcmp(key, "dns_name") == 0) {
            if (!valid_name(val)) { snprintf(err, errcap, "dns_name: 1-63 letters, digits, dots, dashes"); return false; }
            strncpy(n.dns_name, val, sizeof n.dns_name - 1);
        } else if (strcmp(key, "dns_server") == 0) {
            if (val[0] && !settings_parse_ip(val, ip)) { snprintf(err, errcap, "dns_server: not an IPv4 address (blank = DHCP)"); return false; }
            memcpy(n.dns_server, val, strlen(val) + 1);       /* validated or empty */
        } else if (strcmp(key, "usb_ip") == 0) {
            if (!valid_usb_ip(val)) { snprintf(err, errcap, "usb_ip: need a unicast IPv4 host address"); return false; }
            memcpy(n.usb_ip, val, strlen(val) + 1);
        } else if (strcmp(key, "probe") == 0) {
            if (!val[0] || !strcmp(val, "none")) n.probe = SETTINGS_PROBE_NONE;
            else if (val[0] >= '0' && val[0] <= '9' && !val[1]) n.probe = (uint8_t)(val[0] - '0');
            else { snprintf(err, errcap, "probe: 0-9, or blank for none"); return false; }
        } else if (strcmp(key, "form_url") == 0) {
            if (val[0] && !valid_form_url(val)) { snprintf(err, errcap, "form_url: an http:// or https:// link, no spaces or quotes"); return false; }
            memset(n.form_url, 0, sizeof n.form_url); memcpy(n.form_url, val, strlen(val));   /* validated: at most 255 chars */
        } else if (strcmp(key, "lldp") == 0) {
            n.lldp_on = val[0] == '1';
        } else if (strcmp(key, "cdp") == 0) {
            n.cdp_on = val[0] == '1';
        } else if (strcmp(key, "reset") == 0) {
            if (val[0] == '1') {                       /* the unit keeps its number and its form link */
                settings_t keep = n;
                settings_defaults(&n);
                n.probe = keep.probe;
                memcpy(n.form_url, keep.form_url, sizeof n.form_url);
            }
        } else {
            snprintf(err, errcap, "unknown field: %s", key); return false;
        }
    }
    if (changed) *changed = memcmp(&n, s, sizeof n) != 0;
    *s = n;
    return true;
}
