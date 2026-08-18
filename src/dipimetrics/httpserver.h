/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_HTTPSERVER_H
#define DIPIMETRICS_HTTPSERVER_H

#include <poll.h>

#include "store.h"

#define HTTP_MAX_CONNS 8 /* concurrent in-flight connections, sized for occasional scrapes not real load */

typedef struct http_server http_server_t;

/* binds+listens (nonblocking fd, SO_REUSEADDR). -1 on failure */
int http_listen(int family, const char *addr, unsigned port);

/* owns HTTP_MAX_CONNS connection slots against listen_fd. NULL on OOM */
http_server_t *http_server_new(int listen_fd);
void http_server_free(http_server_t *hs);

/* append hs's listen fd + every open conn's fd to pfds[*n..cap), advancing *n.
   caller polls combined array, then passes pfds/n to http_server_service() */
void http_server_poll_fds(http_server_t *hs, struct pollfd *pfds, int cap, int *n);

/* services whichever of hs's fds came back ready in pfds. never blocks: each ready fd gets 1 accept/recv/send.
   reaps connections idle past their deadline. GET /metrics renders st, anything else: 404. */
void http_server_service(http_server_t *hs, const struct pollfd *pfds, int n, store_t *st, double now_mono, int verbose);

#endif
