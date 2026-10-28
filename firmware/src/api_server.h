/*
 * api_server.h - NDJSON status stream on TCP :9000 (see docs/PROTOCOL.md).
 */
#ifndef API_SERVER_H
#define API_SERVER_H

#include <stdbool.h>
#include <stdint.h>

bool api_server_init(void);

/* Call every main-loop iteration: pushes on change and every
 * API_HEARTBEAT_MS, retries clients whose send buffer was full. */
void api_server_poll(void);

int api_server_clients(void);

#endif /* API_SERVER_H */
