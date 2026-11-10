/*
 * Host-side tests for settings.c: defaults, encode/decode round trip and
 * corruption, query parsing with URL encoding and validation.
 */
#include "settings.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)
#define CHECK_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", \
        __FILE__, __LINE__, #a, (a), (b)); return 1; } \
} while (0)

static int test_roundtrip(void)
{
    settings_t s, d; settings_defaults(&s);
    CHECK_STR(s.ping_target, "1.1.1.1"); CHECK_STR(s.dns_name, "clockclub.net"); CHECK_STR(s.dns_server, "");
    CHECK(s.lldp_on && s.cdp_on); CHECK_STR(s.usb_ip, "192.168.7.1"); CHECK(s.probe == SETTINGS_PROBE_NONE);
    CHECK_STR(s.form_url, SETTINGS_DEFAULT_FORM_URL);
    strcpy(s.dns_server, "9.9.9.9"); s.cdp_on = false; strcpy(s.usb_ip, "10.42.0.1"); s.probe = 3; strcpy(s.form_url, "https://example.com/f?sw={switch_ip}&p={port}");
    uint8_t buf[SETTINGS_BLOB_MAX];
    size_t n = settings_encode(&s, buf, sizeof buf);
    CHECK(n > 12 && n <= SETTINGS_BLOB_MAX);
    CHECK(settings_encode(&s, buf, n - 1) == 0);
    memset(&d, 0xAA, sizeof d);
    CHECK(settings_decode(buf, n, &d));
    CHECK(memcmp(&s, &d, sizeof s) == 0);
    /* a saved copy of the old default link (MDN's echo page) comes back as the share page */
    {
        settings_t o = s;
        memset(o.form_url, 0, sizeof o.form_url);
        strcpy(o.form_url, "https://mdn.github.io/dom-examples/url-params/?switch={switch}&switch_ip={switch_ip}&port={port}&vlan={vlan}");
        uint8_t ob[SETTINGS_BLOB_MAX];
        size_t on = settings_encode(&o, ob, sizeof ob);
        CHECK(settings_decode(ob, on, &d)); CHECK_STR(d.form_url, SETTINGS_DEFAULT_FORM_URL);
    }
    /* decode with a bigger window (flash is 0xFF after the blob) */
    uint8_t big[SETTINGS_BLOB_MAX + 32]; memset(big, 0xFF, sizeof big); memcpy(big, buf, n);
    CHECK(settings_decode(big, sizeof big, &d));
    /* corruption */
    big[20] ^= 1; CHECK(!settings_decode(big, sizeof big, &d)); big[20] ^= 1;
    big[0] ^= 1; CHECK(!settings_decode(big, sizeof big, &d)); big[0] ^= 1;
    CHECK(!settings_decode(big, n - 1, &d));
    memset(big, 0xFF, sizeof big); CHECK(!settings_decode(big, sizeof big, &d));
    CHECK(!settings_decode(NULL, 100, &d));
    /* a version-1 blob (no usb_ip) still decodes, with the default address */
    {
        uint8_t v1[SETTINGS_BLOB_MAX]; size_t m = settings_encode(&s, v1, sizeof v1);
        size_t plen = (size_t)(v1[6] << 8 | v1[7]) - 17 - (SETTINGS_FORM_URL_MAX + 1);   /* drop usb_ip, probe, form_url */
        v1[5] = 1; v1[6] = (uint8_t)(plen >> 8); v1[7] = (uint8_t)plen;
        /* recompute the CRC the way settings.c does (copy of its table-less loop) */
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < 8 + plen; i++) { c ^= v1[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
        c = ~c; uint8_t *q = v1 + 8 + plen; q[0] = (uint8_t)(c >> 24); q[1] = (uint8_t)(c >> 16); q[2] = (uint8_t)(c >> 8); q[3] = (uint8_t)c;
        (void)m;
        CHECK(settings_decode(v1, sizeof v1, &d));
        CHECK_STR(d.dns_server, "9.9.9.9"); CHECK(!d.cdp_on); CHECK_STR(d.usb_ip, "192.168.7.1"); CHECK(d.probe == SETTINGS_PROBE_NONE);
        CHECK_STR(d.form_url, SETTINGS_DEFAULT_FORM_URL);
    }
    /* a version-3 blob (trailing lan_page byte, firmware 0.1.x) still decodes; the byte is ignored */
    {
        uint8_t v2[SETTINGS_BLOB_MAX]; size_t m3 = settings_encode(&s, v2, sizeof v2);
        size_t plen = (size_t)(v2[6] << 8 | v2[7]) - (SETTINGS_FORM_URL_MAX + 1);   /* v5 length */
        uint32_t c;
        uint8_t *q;
        /* version 5: no form_url */
        v2[5] = 5; v2[6] = (uint8_t)(plen >> 8); v2[7] = (uint8_t)plen;
        c = 0xFFFFFFFFu;
        for (size_t i = 0; i < 8 + plen; i++) { c ^= v2[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
        c = ~c; q = v2 + 8 + plen; q[0] = (uint8_t)(c >> 24); q[1] = (uint8_t)(c >> 16); q[2] = (uint8_t)(c >> 8); q[3] = (uint8_t)c;
        CHECK(settings_decode(v2, sizeof v2, &d));
        CHECK(d.probe == 3); CHECK_STR(d.form_url, SETTINGS_DEFAULT_FORM_URL);
        /* version 3: same length as v5, last byte was lan_page */
        v2[8 + plen - 1] = 1; (void)m3;
        v2[5] = 3; v2[6] = (uint8_t)(plen >> 8); v2[7] = (uint8_t)plen;
        c = 0xFFFFFFFFu;
        for (size_t i = 0; i < 8 + plen; i++) { c ^= v2[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
        c = ~c; q = v2 + 8 + plen; q[0] = (uint8_t)(c >> 24); q[1] = (uint8_t)(c >> 16); q[2] = (uint8_t)(c >> 8); q[3] = (uint8_t)c;
        CHECK(settings_decode(v2, sizeof v2, &d));
        CHECK_STR(d.usb_ip, "10.42.0.1"); CHECK(!d.cdp_on); CHECK(d.probe == SETTINGS_PROBE_NONE);
        /* version 4: v5 without the probe byte */
        plen--; v2[5] = 4; v2[6] = (uint8_t)(plen >> 8); v2[7] = (uint8_t)plen;
        c = 0xFFFFFFFFu;
        for (size_t i = 0; i < 8 + plen; i++) { c ^= v2[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
        c = ~c; q = v2 + 8 + plen; q[0] = (uint8_t)(c >> 24); q[1] = (uint8_t)(c >> 16); q[2] = (uint8_t)(c >> 8); q[3] = (uint8_t)c;
        CHECK(settings_decode(v2, sizeof v2, &d));
        CHECK_STR(d.usb_ip, "10.42.0.1"); CHECK(d.probe == SETTINGS_PROBE_NONE);
    }
    /* a blob with an invalid IP inside is rejected even with a good CRC */
    strcpy(s.ping_target, "999.1.1.1"); n = settings_encode(&s, buf, sizeof buf); CHECK(!settings_decode(buf, n, &d));
    return 0;
}

static int test_query(void)
{
    settings_t s; settings_defaults(&s);
    char err[64]; bool ch = true;
    CHECK(settings_apply_query(&s, "", err, sizeof err, &ch)); CHECK(!ch);
    CHECK(settings_apply_query(&s, NULL, err, sizeof err, &ch)); CHECK(!ch);
    CHECK(settings_apply_query(&s, "ping_target=8.8.8.8&dns_name=www.example.org&dns_server=192.168.1.1", err, sizeof err, &ch));
    CHECK(ch); CHECK_STR(s.ping_target, "8.8.8.8"); CHECK_STR(s.dns_name, "www.example.org"); CHECK_STR(s.dns_server, "192.168.1.1");
    /* URL encoding, blanks, trimming */
    CHECK(settings_apply_query(&s, "dns_server=&dns_name=%20corp-dc1.example.com+", err, sizeof err, &ch));
    CHECK_STR(s.dns_server, ""); CHECK_STR(s.dns_name, "corp-dc1.example.com");
    /* bad values leave everything untouched */
    settings_t before = s;
    CHECK(!settings_apply_query(&s, "ping_target=8.8.8", err, sizeof err, &ch)); CHECK(strstr(err, "ping_target"));
    CHECK(!settings_apply_query(&s, "ping_target=1.1.1.1&dns_name=bad_name!", err, sizeof err, &ch)); CHECK(strstr(err, "dns_name"));
    CHECK(!settings_apply_query(&s, "dns_name=", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "dns_name=.example.com", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "dns_name=a..b", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "dns_server=1.2.3.256", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "colour=red", err, sizeof err, &ch)); CHECK(strstr(err, "unknown"));
    CHECK(!settings_apply_query(&s, "dns_name=%zz", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "dns_name=%2", err, sizeof err, &ch));
    char longq[300] = "dns_name="; memset(longq + 9, 'a', 200);
    CHECK(!settings_apply_query(&s, longq, err, sizeof err, &ch));
    CHECK(memcmp(&before, &s, sizeof s) == 0);
    /* tx flags and reset */
    CHECK(settings_apply_query(&s, "lldp=0&cdp=1", err, sizeof err, &ch)); CHECK(!s.lldp_on && s.cdp_on);
    CHECK(settings_apply_query(&s, "reset=1", err, sizeof err, &ch)); CHECK(ch);
    settings_t d; settings_defaults(&d); CHECK(memcmp(&d, &s, sizeof s) == 0);
    CHECK(settings_apply_query(&s, "reset=1", err, sizeof err, &ch)); CHECK(!ch);
    /* device address */
    CHECK(settings_apply_query(&s, "usb_ip=10.42.0.1", err, sizeof err, &ch)); CHECK_STR(s.usb_ip, "10.42.0.1");
    CHECK(!settings_apply_query(&s, "lan=0", err, sizeof err, &ch));   /* removed 2026-10-25: unknown field */
    uint8_t peer[4]; settings_usb_peer_ip(&s, peer); CHECK(peer[0] == 10 && peer[3] == 2);
    CHECK(settings_apply_query(&s, "usb_ip=192.168.7.254", err, sizeof err, &ch)); settings_usb_peer_ip(&s, peer); CHECK(peer[3] == 253);
    CHECK(!settings_apply_query(&s, "usb_ip=192.168.7.0", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "usb_ip=192.168.7.255", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "usb_ip=127.0.0.1", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "usb_ip=224.0.0.1", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "usb_ip=", err, sizeof err, &ch));
    uint8_t ip[4];
    CHECK(settings_parse_ip("10.0.0.1", ip) && ip[0] == 10 && ip[3] == 1);
    CHECK(!settings_parse_ip("10.0.0.1 ", ip)); CHECK(!settings_parse_ip("", ip)); CHECK(!settings_parse_ip("1.2.3", ip));
    /* probe number */
    char nm[16];
    settings_device_name(&s, nm, sizeof nm); CHECK_STR(nm, "picoplug");
    CHECK(settings_apply_query(&s, "probe=7", err, sizeof err, &ch)); CHECK(ch && s.probe == 7);
    settings_device_name(&s, nm, sizeof nm); CHECK_STR(nm, "picoplug-7");
    CHECK(!settings_apply_query(&s, "probe=10", err, sizeof err, &ch)); CHECK(s.probe == 7);
    CHECK(!settings_apply_query(&s, "probe=x", err, sizeof err, &ch));
    CHECK(settings_apply_query(&s, "reset=1", err, sizeof err, &ch)); CHECK(s.probe == 7);
    CHECK(settings_apply_query(&s, "probe=0", err, sizeof err, &ch)); CHECK(s.probe == 0);
    CHECK(settings_apply_query(&s, "probe=", err, sizeof err, &ch)); CHECK(ch && s.probe == SETTINGS_PROBE_NONE);
    /* form link */
    CHECK(settings_apply_query(&s, "form_url=https%3A%2F%2Fnf.example.net%2Fnoc%2F%3FswitchIp%3D%7Bswitch_ip%7D%26port%3D%7Bport%7D", err, sizeof err, &ch));
    CHECK(ch); CHECK_STR(s.form_url, "https://nf.example.net/noc/?switchIp={switch_ip}&port={port}");
    CHECK(!settings_apply_query(&s, "form_url=ftp%3A%2F%2Fx", err, sizeof err, &ch)); CHECK(strstr(err, "form_url") != NULL);
    CHECK(!settings_apply_query(&s, "form_url=https%3A%2F%2Fa%20b", err, sizeof err, &ch));
    CHECK(!settings_apply_query(&s, "form_url=https%3A%2F%2Fa%22b", err, sizeof err, &ch));
    CHECK_STR(s.form_url, "https://nf.example.net/noc/?switchIp={switch_ip}&port={port}");
    CHECK(settings_apply_query(&s, "reset=1", err, sizeof err, &ch)); CHECK_STR(s.form_url, "https://nf.example.net/noc/?switchIp={switch_ip}&port={port}");
    {   /* 255 characters fit, 256 do not */
        char q[1024] = "form_url=https%3A%2F%2F"; size_t o = strlen(q);
        for (int i = 0; i < SETTINGS_FORM_URL_MAX - 8; i++) q[o++] = 'a';
        q[o] = '\0';
        CHECK(settings_apply_query(&s, q, err, sizeof err, &ch)); CHECK(strlen(s.form_url) == SETTINGS_FORM_URL_MAX);
        q[o++] = 'a'; q[o] = '\0';
        CHECK(!settings_apply_query(&s, q, err, sizeof err, &ch));
    }
    CHECK(settings_apply_query(&s, "form_url=", err, sizeof err, &ch)); CHECK(ch && s.form_url[0] == '\0');
    {   /* a blank link survives the flash roundtrip */
        uint8_t b[SETTINGS_BLOB_MAX]; settings_t d2; size_t bn = settings_encode(&s, b, sizeof b);
        CHECK(bn && settings_decode(b, bn, &d2)); CHECK(d2.form_url[0] == '\0');
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const struct { const char *name; int (*fn)(void); } TESTS[] = {
        {"roundtrip", test_roundtrip}, {"query", test_query},
    };
    if (argc < 2) { fprintf(stderr, "usage: test_settings <name>\n"); return 2; }
    for (size_t i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++)
        if (strcmp(argv[1], TESTS[i].name) == 0) { int rc = TESTS[i].fn(); fprintf(stderr, "settings %s: %s\n", TESTS[i].name, rc ? "FAIL" : "ok"); return rc; }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
