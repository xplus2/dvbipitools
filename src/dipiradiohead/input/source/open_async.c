/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdlib.h>

#include "lib/ioutil.h"
#include "lib/log.h"

#include "../../version.h"
#include "../playlist.h"
#include "priv.h"

typedef enum { SO_FETCHING, SO_SNIFFING } source_open_phase_t;

struct source_open {
  source_open_phase_t phase;
  char cur_uri[2048];
  int hops;
  int insecure;
  source_meta_cb cb;
  void *ctx;
  http_async_t *ha; /* during SO_FETCHING */
  http_t *h;        /* during SO_SNIFFING */
  unsigned char sniff[SRC_SNIFF_CAP];
  size_t sniff_got;
  source_t *result; /* set on SOURCE_OPEN_DONE */
};

static int so_start_fetch(source_open_t *o, net_err_reason_t *reason_out) {
  http_url_t u;

  if (http_url_parse(o->cur_uri, &u)) {
    log_line("source: invalid uri: %s", o->cur_uri);
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  o->ha = http_async_start(&u, TOOL_NAME "/" TOOL_VERSION, o->insecure, NULL, reason_out);
  if (!o->ha)
    return -1;
  o->phase = SO_FETCHING;
  return 0;
}

source_open_t *source_open_async_start(const char *uri, int insecure, source_meta_cb cb, void *ctx, net_err_reason_t *reason_out) {
  source_open_t *o = calloc(1, sizeof *o);
  if (!o)
    return NULL;
  bufcpy(o->cur_uri, sizeof o->cur_uri, uri);
  o->insecure = insecure;
  o->cb = cb;
  o->ctx = ctx;
  if (so_start_fetch(o, reason_out) != 0) {
    free(o);
    return NULL;
  }
  return o;
}

int source_open_async_poll_fd(const source_open_t *o) {
  if (o->phase == SO_FETCHING)
    return http_async_poll_fd(o->ha);
  return http_fd(o->h);
}

short source_open_async_poll_events(const source_open_t *o) {
  if (o->phase == SO_FETCHING)
    return http_async_poll_events(o->ha);
  return POLLIN;
}

source_open_state_t source_open_async_step(source_open_t *o, net_err_reason_t *reason_out) {
  if (o->phase == SO_FETCHING) {
    http_async_state_t st = http_async_step(o->ha, reason_out);
    if (st == HTTP_ASYNC_PENDING)
      return SOURCE_OPEN_PENDING;
    if (st == HTTP_ASYNC_ERROR) {
      http_async_free(o->ha);
      o->ha = NULL;
      return SOURCE_OPEN_ERROR;
    }
    o->h = http_async_take(o->ha);
    o->ha = NULL;
    o->phase = SO_SNIFFING;
    o->sniff_got = 0;
  }

  for (;;) {
    ssize_t n;
    if (o->sniff_got >= sizeof o->sniff)
      break; /* buffer full: enough to sniff regardless of what else might follow */
    n = http_read(o->h, o->sniff + o->sniff_got, sizeof o->sniff - o->sniff_got, reason_out);
    if (n < 0) {
      if (o->sniff_got > 0)
        break; /* connection close after a full small (e.g. playlist) body is not an error */
      log_line("source: empty response from %s", o->cur_uri);
      http_close(o->h);
      o->h = NULL;
      return SOURCE_OPEN_ERROR;
    }
    if (n == 0)
      return SOURCE_OPEN_PENDING;
    o->sniff_got += (size_t)n;
  }

  {
    char next[2048];
    if (playlist_extract(o->sniff, o->sniff_got, next, sizeof next)) {
      http_close(o->h);
      o->h = NULL;
      o->hops++;
      if (o->hops >= SRC_MAX_HOPS) {
        log_line("source: too many playlist redirects");
        if (reason_out)
          *reason_out = NET_ERR_FORMAT;
        return SOURCE_OPEN_ERROR;
      }
      bufcpy(o->cur_uri, sizeof o->cur_uri, next);
      if (so_start_fetch(o, reason_out) != 0)
        return SOURCE_OPEN_ERROR;
      return SOURCE_OPEN_PENDING;
    }
  }

  {
    http_t *h = o->h;
    o->h = NULL; /* build_source() absorbs h either way: owns it on success, closes it on failure */
    o->result = build_source(h, o->sniff, o->sniff_got, o->cb, o->ctx);
  }
  if (!o->result) {
    if (reason_out)
      *reason_out = NET_ERR_OTHER; /* build_source() only fails on allocation */
    return SOURCE_OPEN_ERROR;
  }
  return SOURCE_OPEN_DONE;
}

source_t *source_open_async_take(source_open_t *o) {
  source_t *s = o->result;
  free(o);
  return s;
}

void source_open_async_free(source_open_t *o) {
  if (!o)
    return;
  if (o->ha)
    http_async_free(o->ha);
  if (o->h)
    http_close(o->h);
  if (o->result)
    source_close(o->result);
  free(o);
}
