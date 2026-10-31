/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "gena.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "lib/net/httpclient/httpclient.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"

#include "dlna_int.h"

#define GENA_MAX_SUBS 32
#define GENA_MAX_PER_PEER 4
#define GENA_TIMEOUT_S 1800
#define GENA_INITIAL_DELAY_MS 300
#define SUB_HOST_MAX 48
#define SUB_PATH_MAX 128
#define EVENT_BODY_MAX 1024

typedef struct {
  int used;
  gena_svc_t svc;
  char sid[64];
  struct in6_addr peer;
  char host[SUB_HOST_MAX];
  unsigned port;
  char path[SUB_PATH_MAX];
  unsigned seq;
  double expires;
} sub_t;

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static sub_t g_subs[GENA_MAX_SUBS];
static _Atomic unsigned g_sid_counter;
static _Atomic unsigned g_update_id;

static size_t hex_min(char *dst, unsigned long v, size_t min_width) {
  char tmp[2 * sizeof(unsigned long)];
  size_t n = 0, pad, i;
  if (!v) {
    tmp[n++] = '0';
  } else {
    while (v) {
      unsigned d = (unsigned)(v & 0xf);
      tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
      v >>= 4;
    }
  }
  pad = n < min_width ? min_width - n : 0;
  for (i = 0; i < pad; i++)
    dst[i] = '0';
  for (i = 0; i < n; i++)
    dst[pad + i] = tmp[n - 1 - i];
  return pad + n;
}

static void make_sid(char out[64]) {
  unsigned n = atomic_fetch_add(&g_sid_counter, 1u);
  size_t off = bufcpy(out, 64, "uuid:dipixy-sub-");
  off += hex_min(out + off, (unsigned long)time(NULL), 8);
  out[off++] = '-';
  off += hex_min(out + off, n & 0xffffu, 4);
  bufcpy(out + off, 64 - off, "-4a11-8a11-000000000000");
}

unsigned gena_system_update_id(void) {
  unsigned v = atomic_load(&g_update_id);
  if (!v) {
    unsigned want = (unsigned)time(NULL);
    if (!want) want = 1;
    if (atomic_compare_exchange_strong(&g_update_id, &v, want)) v = want;
  }
  return v;
}

static void add_escaped(sbuf_t *b, const char *s) {
  for (; *s; s++) {
    switch (*s) {
      case '&': sbuf_add(b, "&amp;"); break;
      case '<': sbuf_add(b, "&lt;"); break;
      case '>': sbuf_add(b, "&gt;"); break;
      default: sbuf_add_n(b, s, 1); break;
    }
  }
}

static void add_property(sbuf_t *b, const char *name, const char *value) {
  sbuf_add(b, "<e:property><");
  sbuf_add(b, name);
  sbuf_add(b, ">");
  add_escaped(b, value);
  sbuf_add(b, "</");
  sbuf_add(b, name);
  sbuf_add(b, "></e:property>");
}

static void body_open(sbuf_t *b, char out[EVENT_BODY_MAX]) {
  sbuf_init(b, out, EVENT_BODY_MAX);
  sbuf_add(b, "<?xml version=\"1.0\"?>\r\n<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">");
}

static void build_cd_body(char out[EVENT_BODY_MAX]) {
  sbuf_t b;
  char id[16];
  body_open(&b, out);
  uint_to_str(id, gena_system_update_id());
  add_property(&b, "SystemUpdateID", id);
  sbuf_add(&b, "</e:propertyset>");
}

static void build_cm_body(const config_t *cfg, char out[EVENT_BODY_MAX]) {
  sbuf_t b;
  char source[512];
  body_open(&b, out);
  dlna_source_protocol_info(cfg, source, sizeof source);
  add_property(&b, "SourceProtocolInfo", source);
  add_property(&b, "SinkProtocolInfo", "");
  add_property(&b, "CurrentConnectionIDs", "0");
  sbuf_add(&b, "</e:propertyset>");
}

/* IP literal to v6 form, v4 mapped */
static int ip_parse(const char *text, struct in6_addr *out) {
  struct in_addr a4;
  if (inet_pton(AF_INET6, text, out) == 1) return 0;
  if (inet_pton(AF_INET, text, &a4) != 1) return -1;
  memset(out, 0, sizeof *out);
  out->s6_addr[10] = 0xff;
  out->s6_addr[11] = 0xff;
  memcpy(&out->s6_addr[12], &a4, sizeof a4);
  return 0;
}

static int ip_equal(const struct in6_addr *a, const struct in6_addr *b) {
  return !memcmp(a, b, sizeof *a);
}

/* first <url> of CALLBACK. IP literal equal to subscriber only, no resolve */
static int parse_callback(const char *cb, const struct in6_addr *peer, sub_t *s) {
  http_url_t u;
  char url[sizeof u.host + sizeof u.path + 32];
  struct in6_addr addr;
  const char *lt = strchr(cb, '<');
  const char *gt = lt ? strchr(lt, '>') : NULL;
  size_t n;
  if (!lt || !gt || gt - lt < 8) return -1;
  n = (size_t)(gt - lt - 1);
  if (n >= sizeof url) return -1;
  memcpy(url, lt + 1, n);
  url[n] = '\0';
  if (http_url_parse(url, &u) || u.tls) return -1;
  if (ip_parse(u.host, &addr) || !ip_equal(&addr, peer)) return -1;
  if (strlen(u.host) >= SUB_HOST_MAX || strlen(u.path) >= SUB_PATH_MAX) return -1;
  bufcpy(s->host, sizeof s->host, u.host);
  bufcpy(s->path, sizeof s->path, u.path);
  s->port = u.port;
  return 0;
}

static unsigned parse_timeout(const char *hdr) {
  unsigned long v;
  char *end;
  if (!hdr || strncasecmp(hdr, "Second-", 7)) return GENA_TIMEOUT_S;
  v = strtoul(hdr + 7, &end, 10);
  if (end == hdr + 7 || !v || v > GENA_TIMEOUT_S) return GENA_TIMEOUT_S;
  return (unsigned)v;
}

static int present(const char *s) {
  return s && *s;
}

static void purge_expired(double now) {
  for (int i = 0; i < GENA_MAX_SUBS; i++)
    if (g_subs[i].used && g_subs[i].expires <= now) g_subs[i].used = 0;
}

static sub_t *find_sub(gena_svc_t svc, const char *sid, const struct in6_addr *peer) {
  for (int i = 0; i < GENA_MAX_SUBS; i++)
    if (g_subs[i].used && g_subs[i].svc == svc && ip_equal(&g_subs[i].peer, peer) && !strcmp(g_subs[i].sid, sid)) return &g_subs[i];
  return NULL;
}

static int peer_count(const struct in6_addr *peer) {
  int n = 0;
  for (int i = 0; i < GENA_MAX_SUBS; i++)
    if (g_subs[i].used && ip_equal(&g_subs[i].peer, peer)) n++;
  return n;
}

static sub_t *free_slot(void) {
  for (int i = 0; i < GENA_MAX_SUBS; i++)
    if (!g_subs[i].used) return &g_subs[i];
  return NULL;
}

int gena_subscribe(const config_t *cfg, gena_svc_t svc, const char *peer_ip, const char *sid, const char *callback, const char *nt, const char *timeout, char *out_sid, size_t out_sidsz, unsigned *out_timeout_s) {
  double now = mono_seconds();
  unsigned secs = parse_timeout(timeout);
  sub_t *s;
  sub_t fresh;
  char body[EVENT_BODY_MAX];
  struct in6_addr peer;
  int status = 200;
  if (!present(peer_ip) || ip_parse(peer_ip, &peer)) return 412;
  pthread_mutex_lock(&g_mtx);
  purge_expired(now);
  if (present(sid)) {
    if (present(nt) || present(callback)) {
      status = 400;
    } else if (!(s = find_sub(svc, sid, &peer))) {
      status = 412;
    } else {
      s->expires = now + secs;
      bufcpy(out_sid, out_sidsz, s->sid);
    }
  } else if (!present(nt) || !present(callback) || strcasecmp(nt, "upnp:event")) {
    status = 412;
  } else {
    memset(&fresh, 0, sizeof fresh);
    if (parse_callback(callback, &peer, &fresh)) {
      status = 412;
    } else if (peer_count(&peer) >= GENA_MAX_PER_PEER) {
      status = 503;
    } else if (!(s = free_slot())) {
      status = 503;
    } else {
      fresh.used = 1;
      fresh.peer = peer;
      fresh.svc = svc;
      fresh.expires = now + secs;
      make_sid(fresh.sid);
      if (svc == GENA_SVC_CD) build_cd_body(body);
      else build_cm_body(cfg, body);
      if (gena_notify_enqueue(fresh.host, fresh.port, fresh.path, fresh.sid, 0, body, GENA_INITIAL_DELAY_MS)) {
        status = 503;
      } else {
        fresh.seq = 1;
        *s = fresh;
        bufcpy(out_sid, out_sidsz, fresh.sid);
      }
    }
  }
  pthread_mutex_unlock(&g_mtx);
  if (status == 200) *out_timeout_s = secs;
  return status;
}

int gena_unsubscribe(gena_svc_t svc, const char *peer_ip, const char *sid, const char *callback, const char *nt) {
  sub_t *s;
  struct in6_addr peer;
  int status = 200;
  if (present(nt) || present(callback)) return 400;
  if (!present(sid) || !present(peer_ip) || ip_parse(peer_ip, &peer)) return 412;
  pthread_mutex_lock(&g_mtx);
  purge_expired(mono_seconds());
  if ((s = find_sub(svc, sid, &peer))) s->used = 0;
  else status = 412;
  pthread_mutex_unlock(&g_mtx);
  return status;
}

void gena_notify_system_update(void) {
  char body[EVENT_BODY_MAX];
  gena_system_update_id();
  atomic_fetch_add(&g_update_id, 1u);
  build_cd_body(body);
  pthread_mutex_lock(&g_mtx);
  purge_expired(mono_seconds());
  for (int i = 0; i < GENA_MAX_SUBS; i++) {
    sub_t *s = &g_subs[i];
    if (!s->used || s->svc != GENA_SVC_CD) continue;
    if (gena_notify_enqueue(s->host, s->port, s->path, s->sid, s->seq, body, 0) == 0) s->seq++;
  }
  pthread_mutex_unlock(&g_mtx);
}

void gena_delivery_failed(const char *sid) {
  pthread_mutex_lock(&g_mtx);
  for (int i = 0; i < GENA_MAX_SUBS; i++)
    if (g_subs[i].used && !strcmp(g_subs[i].sid, sid)) g_subs[i].used = 0;
  pthread_mutex_unlock(&g_mtx);
}

void gena_stop(void) {
  gena_notify_stop();
  pthread_mutex_lock(&g_mtx);
  memset(g_subs, 0, sizeof g_subs);
  pthread_mutex_unlock(&g_mtx);
}
