/*
 * http_server.h - minimal HTTP/1.0 server on TCP :80.
 *
 *   GET /             -> embedded viewer page (index.html)
 *   GET /status.json  -> current status object
 * Everything else is 404. One request per connection, then close.
 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdbool.h>

bool http_server_init(void);
/* Executes a deferred /reboot request; call from the main loop. */
void http_server_poll(void);

#endif /* HTTP_SERVER_H */
