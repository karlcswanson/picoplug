/*
 * ws.h - the small part of WebSocket (RFC 6455) the status stream needs: the
 * handshake's accept key, the header of a server text frame, and reading the
 * masked frames a browser sends (close and ping; anything else is skipped).
 * No allocation, no lwIP: host-testable.
 */
#ifndef WS_H
#define WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WS_ACCEPT_LEN 28            /* base64 of a 20-byte SHA-1 */
#define WS_HDR_MAX    4             /* text frames up to 65535 bytes */

/* Sec-WebSocket-Accept for the client's Sec-WebSocket-Key; out gets 28 chars + NUL. */
void ws_accept(const char *key, size_t key_len, char out[WS_ACCEPT_LEN + 1]);

/* Header of an unmasked, final text frame carrying len bytes (len < 65536). Returns its size. */
size_t ws_text_header(size_t len, uint8_t out[WS_HDR_MAX]);

typedef enum { WS_NEED_MORE, WS_FRAME, WS_BAD } ws_parse_t;

typedef struct {
    uint8_t opcode;                 /* 0x1 text, 0x2 binary, 0x8 close, 0x9 ping, 0xA pong */
    uint8_t payload[125];           /* unmasked; control frames are at most 125 bytes */
    uint8_t len;
    size_t  frame_len;              /* bytes of buf this frame used */
} ws_frame_t;

/* Reads one client frame from buf. Client frames must be masked, and payloads over 125 bytes
 * are refused (WS_BAD): the page never sends data, only close and ping. */
ws_parse_t ws_parse(const uint8_t *buf, size_t n, ws_frame_t *f);

#endif /* WS_H */
