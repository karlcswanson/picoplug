/*
 * api_server.h - the pushed status stream (see docs/PROTOCOL.md): NDJSON on TCP :9000, and
 * WebSocket on :80/ws once the HTTP server has handed a connection over.
 */
#ifndef API_SERVER_H
#define API_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct tcp_pcb;

bool api_server_init(void);

/* Call every main-loop iteration: pushes on change and every
 * API_HEARTBEAT_MS, retries clients whose send buffer was full. */
void api_server_poll(void);

int api_server_clients(void);

/* Answer a WebSocket upgrade (101) on an HTTP connection and take it over. False when the
 * WebSocket slots are full or the reply did not fit; the caller still owns pcb then. */
bool api_server_ws_upgrade(struct tcp_pcb *pcb, const char *key, size_t key_len);

#endif /* API_SERVER_H */
