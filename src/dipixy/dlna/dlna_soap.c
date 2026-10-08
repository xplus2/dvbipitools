/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE /* fopencookie */

#include "dlna_int.h"

#include "lib/sys/ioutil.h"
#include "lib/helper/xml_util.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dlna_soap_action_name(const char *soapaction_header, char *out, size_t outsz) {
  const char *h, *q2, *hash;
  size_t len;

  if (!soapaction_header) return -1;
  h = soapaction_header;
  if (*h == '"') h++;
  q2 = strrchr(h, '"');
  hash = strrchr(h, '#');
  if (!hash) return -1;
  hash++;
  len = (q2 && q2 > hash) ? (size_t)(q2 - hash) : strlen(hash);
  if (len == 0 || len >= outsz) return -1;
  memcpy(out, hash, len);
  out[len] = '\0';
  return 0;
}

static ssize_t gbuf_write(void *cookie, const char *data, size_t size) {
  gbuf_t *g = cookie;
  if (growbuf_reserve((void **)&g->buf, &g->cap, 1, g->len + size + 1, 4096) < 0)
    return -1;
  memcpy(g->buf + g->len, data, size);
  g->len += size;
  g->buf[g->len] = '\0';
  return (ssize_t)size;
}

#define GBUF_SHRINK_RATIO 2

FILE *gbuf_open(gbuf_t *g) {
  cookie_io_functions_t io = {.read = NULL, .write = gbuf_write, .seek = NULL, .close = NULL};
  size_t need = g->len + 1;
  if (g->buf && g->cap > need * GBUF_SHRINK_RATIO) {
    char *p = realloc(g->buf, need);
    if (p) {
      g->buf = p;
      g->cap = need;
    }
  }
  g->len = 0;
  return fopencookie(g, "w", io);
}

static _Thread_local gbuf_t t_soap_gbuf;

#define GBUF_THREAD_MAX 4

static _Thread_local gbuf_t *t_gbuf_owned[GBUF_THREAD_MAX];
static _Thread_local int t_gbuf_owned_n;
static pthread_key_t g_gbuf_key;
static pthread_once_t g_gbuf_once = PTHREAD_ONCE_INIT;

static void gbuf_release_owned(void *unused) {
  (void)unused;
  for (int i = 0; i < t_gbuf_owned_n; i++) {
    free(t_gbuf_owned[i]->buf);
    t_gbuf_owned[i]->buf = NULL;
    t_gbuf_owned[i]->len = 0;
    t_gbuf_owned[i]->cap = 0;
  }
  t_gbuf_owned_n = 0;
}

static void gbuf_key_init(void) {
  pthread_key_create(&g_gbuf_key, gbuf_release_owned);
}

FILE *gbuf_open_thread(gbuf_t *g) {
  int known = 0;
  pthread_once(&g_gbuf_once, gbuf_key_init);
  for (int i = 0; i < t_gbuf_owned_n; i++) known |= t_gbuf_owned[i] == g;
  if (!known && t_gbuf_owned_n < GBUF_THREAD_MAX) {
    t_gbuf_owned[t_gbuf_owned_n++] = g;
    pthread_setspecific(g_gbuf_key, g);
  }
  return gbuf_open(g);
}

int soap_action_response(char **out, size_t *out_len, const char *service_urn, const char *action_response_tag, const soap_field_t *fields, int nfields) {
  FILE *f = gbuf_open_thread(&t_soap_gbuf);
  if (!f) return 500;
  fprintf(f,
          "<?xml version=\"1.0\"?>\r\n"
          "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
          "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
          "<u:%s xmlns:u=\"%s\">",
          action_response_tag, service_urn);
  for (int i = 0; i < nfields; i++) {
    fprintf(f, "<%s>", fields[i].name);
    xml_escape(f, fields[i].value);
    fprintf(f, "</%s>", fields[i].name);
  }
  fprintf(f, "</u:%s></s:Body></s:Envelope>\r\n", action_response_tag);
  fclose(f);
  *out = t_soap_gbuf.buf;
  *out_len = t_soap_gbuf.len;
  return 200;
}

int soap_fault(char **out, size_t *out_len, int upnp_error_code, const char *desc) {
  FILE *f = gbuf_open_thread(&t_soap_gbuf);
  if (!f) return 500;
  fprintf(f,
          "<?xml version=\"1.0\"?>\r\n"
          "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
          "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><s:Fault>"
          "<faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
          "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>%d</errorCode><errorDescription>",
          upnp_error_code);
  xml_escape(f, desc);
  fputs("</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>\r\n", f);
  fclose(f);
  *out = t_soap_gbuf.buf;
  *out_len = t_soap_gbuf.len;
  return 500;
}
