/* Host tests for ws.c: the handshake's accept key, server frame headers, reading client frames. */
#include "ws.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) != 0) { fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); return 1; } } while (0)

static int accept_is(const char *key, size_t n, const char *want)
{
    char out[WS_ACCEPT_LEN + 1];
    ws_accept(key, n, out);
    CHECK_STR(out, want);
    return 0;
}

static int test_accept(void)
{
    /* RFC 6455 section 1.3; the others (from Python's hashlib) put the SHA-1 padding on both
     * sides of a block boundary: key + GUID is 60, 56, 55, 100 and 36 bytes */
    char k[65];
    if (accept_is("dGhlIHNhbXBsZSBub25jZQ==", 24, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")) return 1;
    memset(k, 'x', 20); if (accept_is(k, 20, "7jD6MP55bPPoLSNhT1mD/AE8sjc=")) return 1;
    memset(k, 'y', 19); if (accept_is(k, 19, "G23tZbeaeFHaWqrd3yXcedbG4z8=")) return 1;
    memset(k, 'z', 64); if (accept_is(k, 64, "aa/jsF5GsRZxr9rCbs74JMSYlG4=")) return 1;
    if (accept_is("", 0, "Kfh9QIsMVZcl6xEPYxPHzW8SZ8w=")) return 1;
    return 0;
}

static int test_header(void)
{
    uint8_t h[WS_HDR_MAX];
    CHECK(ws_text_header(5, h) == 2 && h[0] == 0x81 && h[1] == 5);
    CHECK(ws_text_header(125, h) == 2 && h[1] == 125);
    CHECK(ws_text_header(126, h) == 4 && h[1] == 126 && h[2] == 0 && h[3] == 126);
    CHECK(ws_text_header(4621, h) == 4 && h[1] == 126 && h[2] == 0x12 && h[3] == 0x0D);
    CHECK(ws_text_header(65535, h) == 4 && h[2] == 0xFF && h[3] == 0xFF);
    return 0;
}

static int test_parse(void)
{
    ws_frame_t f;
    /* a browser's close with code 1000, masked */
    const uint8_t close_[] = { 0x88, 0x82, 0x11, 0x22, 0x33, 0x44, 0x03 ^ 0x11, 0xE8 ^ 0x22 };
    for (size_t n = 0; n < sizeof close_; n++) CHECK(ws_parse(close_, n, &f) == WS_NEED_MORE);
    CHECK(ws_parse(close_, sizeof close_, &f) == WS_FRAME);
    CHECK(f.opcode == 0x8 && f.len == 2 && f.payload[0] == 0x03 && f.payload[1] == 0xE8 && f.frame_len == 8);
    /* ping "hi" followed by more bytes: only the first frame is used */
    const uint8_t ping[] = { 0x89, 0x82, 1, 2, 3, 4, 'h' ^ 1, 'i' ^ 2, 0x8A, 0x80 };
    CHECK(ws_parse(ping, sizeof ping, &f) == WS_FRAME);
    CHECK(f.opcode == 0x9 && f.len == 2 && !memcmp(f.payload, "hi", 2) && f.frame_len == 8);
    /* empty masked frame */
    const uint8_t empty[] = { 0x8A, 0x80, 9, 9, 9, 9 };
    CHECK(ws_parse(empty, sizeof empty, &f) == WS_FRAME && f.opcode == 0xA && f.len == 0 && f.frame_len == 6);
    /* unmasked, or longer than a control frame: refused */
    const uint8_t unmasked[] = { 0x81, 0x01, 'a' };
    CHECK(ws_parse(unmasked, sizeof unmasked, &f) == WS_BAD);
    const uint8_t big[] = { 0x81, 0xFE, 0x01, 0x00 };
    CHECK(ws_parse(big, sizeof big, &f) == WS_BAD);
    return 0;
}

int main(int argc, char **argv)
{
    const char *t = argc > 1 ? argv[1] : "";
    if (!strcmp(t, "accept")) return test_accept();
    if (!strcmp(t, "header")) return test_header();
    if (!strcmp(t, "parse")) return test_parse();
    fprintf(stderr, "usage: test_ws accept|header|parse\n");
    return 2;
}
