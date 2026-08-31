/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ssdp.h"

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"
#include "lib/net/multicast.h"

#include "../version.h"

#define SSDP_ADDR "239.255.255.250"
#define SSDP_PORT 1900
#define SSDP_RECV_TIMEOUT_MS 1000
#define SSDP_RECV_BUF 2048

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

/* NT list, bare device uuid handled separately */
static const char *const ssdp_types[] = {
    "upnp:rootdevice",
    "urn:schemas-upnp-org:device:MediaServer:1",
    "urn:schemas-upnp-org:service:ContentDirectory:1",
    "urn:schemas-upnp-org:service:ConnectionManager:1",
};
#define SSDP_NTYPES (sizeof ssdp_types / sizeof ssdp_types[0])

static uint64_t fnv1a64(const char *s, const char *salt) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (; *s; s++) {
    h ^= (unsigned char)*s;
    h *= 0x100000001b3ULL;
  }
  for (; *salt; salt++) {
    h ^= (unsigned char)*salt;
    h *= 0x100000001b3ULL;
  }
  return h;
}

static void hex_pad(char *dst, unsigned long long v, int width) {
  static const char digits[] = "0123456789abcdef";
  for (int i = width - 1; i >= 0; i--) {
    dst[i] = digits[v & 0xf];
    v >>= 4;
  }
}

static pthread_once_t g_uuid_once = PTHREAD_ONCE_INIT;
static char g_uuid_cache[37];
static const config_t *g_uuid_cfg;

static void compute_device_uuid(void) {
  const config_t *cfg = g_uuid_cfg;
  uint64_t a = fnv1a64(cfg->dlna_host, "dipixy-dlna-a");
  uint64_t b = fnv1a64(cfg->dlna_host, "dipixy-dlna-b");
  unsigned t1 = (unsigned)(a >> 32);
  unsigned t2 = (unsigned)((a >> 16) & 0xffffU);
  unsigned t3 = (unsigned)(0x4000U | (a & 0x0fffU)); /* version 4 nibble */
  unsigned t4 = (unsigned)(0x8000U | ((b >> 48) & 0x3fffU)); /* variant 10xx */
  unsigned long long t5 = b & 0xffffffffffffULL;
  hex_pad(g_uuid_cache, t1, 8);
  g_uuid_cache[8] = '-';
  hex_pad(g_uuid_cache + 9, t2, 4);
  g_uuid_cache[13] = '-';
  hex_pad(g_uuid_cache + 14, t3, 4);
  g_uuid_cache[18] = '-';
  hex_pad(g_uuid_cache + 19, t4, 4);
  g_uuid_cache[23] = '-';
  hex_pad(g_uuid_cache + 24, t5, 12);
  g_uuid_cache[36] = '\0';
}

/* dlna_host is fixed at arg-parse time: same cfg pointer every call, safe to
   compute once and cache regardless of which caller's cfg arg triggers it */
void ssdp_device_uuid(const config_t *cfg, char out[37]) {
  g_uuid_cfg = cfg;
  pthread_once(&g_uuid_once, compute_device_uuid);
  memcpy(out, g_uuid_cache, sizeof g_uuid_cache);
}

typedef struct {
  const config_t *cfg;
  char uuid[37];
} ssdp_arg_t;

static _Atomic int g_running;
static pthread_t g_thread;
static mcast_t *g_send;
static ssdp_arg_t g_ssdp_arg;

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  int truncated;
} strbuf_t;

static void sb_init(strbuf_t *b, char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  b->truncated = 0;
  buf[0] = '\0';
}

static void sb_add(strbuf_t *b, const char *s) {
  size_t n = strlen(s);
  size_t room = b->cap > b->len + 1 ? b->cap - b->len - 1 : 0;
  if (n > room) {
    n = room;
    b->truncated = 1;
  }
  memcpy(b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = '\0';
}

static void sb_add_uint(strbuf_t *b, unsigned v) {
  char buf[16];
  uint_to_str(buf, v);
  sb_add(b, buf);
}

static void build_usn(const char *uuid, const char *nt /* NULL = bare device uuid */, char *out, size_t outsz) {
  size_t off = bufcpy(out, outsz, "uuid:");
  off += bufcpy(out + off, outsz - off, uuid);
  if (nt) {
    off += bufcpy(out + off, outsz - off, "::");
    bufcpy(out + off, outsz - off, nt);
  }
}

static void send_notify_one(const config_t *cfg, const char *uuid, const char *nt, int alive) {
  char usn[192], pkt[768];
  strbuf_t b;

  build_usn(uuid, nt, usn, sizeof usn);
  sb_init(&b, pkt, sizeof pkt);
  sb_add(&b, "NOTIFY * HTTP/1.1\r\nHOST: " SSDP_ADDR ":" STRINGIFY(SSDP_PORT) "\r\n");
  if (alive) {
    sb_add(&b, "CACHE-CONTROL: max-age=");
    sb_add_uint(&b, cfg->ssdp_max_age_s);
    sb_add(&b, "\r\nLOCATION: http://");
    sb_add(&b, cfg->dlna_host);
    sb_add(&b, "/dlna/desc.xml\r\nSERVER: dipixy/");
    sb_add(&b, TOOL_VERSION);
    sb_add(&b, " UPnP/1.0 DLNA/1.0\r\nNT: ");
    sb_add(&b, nt ? nt : usn);
    sb_add(&b, "\r\nNTS: ssdp:alive\r\nUSN: ");
    sb_add(&b, usn);
    sb_add(&b, "\r\n\r\n");
  } else {
    sb_add(&b, "NT: ");
    sb_add(&b, nt ? nt : usn);
    sb_add(&b, "\r\nNTS: ssdp:byebye\r\nUSN: ");
    sb_add(&b, usn);
    sb_add(&b, "\r\n\r\n");
  }
  if (!b.truncated)
    mcast_send(g_send, pkt, b.len);
}

static void send_notify_all(const config_t *cfg, const char *uuid, int alive) {
  size_t i;
  send_notify_one(cfg, uuid, NULL, alive);
  for (i = 0; i < SSDP_NTYPES; i++)
    send_notify_one(cfg, uuid, ssdp_types[i], alive);
}

/* headers starts past request line. 1 found, 0 not. exposed for fuzzing */
int ssdp_msearch_header(const char *headers, const char *name, char *out, size_t outsz) {
  size_t namelen = strlen(name);
  const char *line = headers;
  while (line && *line && strncmp(line, "\r\n", 2) != 0) {
    const char *eol = strstr(line, "\r\n");
    if (!eol)
      break;
    if ((size_t)(eol - line) > namelen && !strncasecmp(line, name, namelen) && line[namelen] == ':') {
      const char *v = line + namelen + 1;
      size_t vlen;
      while (*v == ' ')
        v++;
      vlen = (size_t)(eol - v);
      if (vlen >= outsz)
        vlen = outsz - 1;
      memcpy(out, v, vlen);
      out[vlen] = '\0';
      return 1;
    }
    line = eol + 2;
  }
  return 0;
}

static void send_msearch_reply_one(int fd, const struct sockaddr *peer, socklen_t peerlen, const config_t *cfg,
                                    const char *uuid, const char *nt /* NULL = bare device uuid */) {
  char usn[192], pkt[768];
  strbuf_t b;

  build_usn(uuid, nt, usn, sizeof usn);
  sb_init(&b, pkt, sizeof pkt);
  sb_add(&b, "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=");
  sb_add_uint(&b, cfg->ssdp_max_age_s);
  sb_add(&b, "\r\nEXT:\r\nLOCATION: http://");
  sb_add(&b, cfg->dlna_host);
  sb_add(&b, "/dlna/desc.xml\r\nSERVER: dipixy/");
  sb_add(&b, TOOL_VERSION);
  sb_add(&b, " UPnP/1.0 DLNA/1.0\r\nST: ");
  sb_add(&b, nt ? nt : usn);
  sb_add(&b, "\r\nUSN: ");
  sb_add(&b, usn);
  sb_add(&b, "\r\n\r\n");
  if (!b.truncated)
    sendto(fd, pkt, b.len, 0, peer, peerlen);
}

static void handle_msearch(int fd, const char *buf, const struct sockaddr *peer, socklen_t peerlen,
                            const config_t *cfg, const char *uuid) {
  const char *line_end;
  char st[192];
  size_t i;

  if (strncmp(buf, "M-SEARCH", 8) != 0)
    return;
  line_end = strstr(buf, "\r\n");
  if (!line_end || !ssdp_msearch_header(line_end + 2, "ST", st, sizeof st))
    return;

  if (!strcmp(st, "ssdp:all")) {
    send_msearch_reply_one(fd, peer, peerlen, cfg, uuid, NULL);
    for (i = 0; i < SSDP_NTYPES; i++)
      send_msearch_reply_one(fd, peer, peerlen, cfg, uuid, ssdp_types[i]);
    return;
  }
  {
    char want_uuid[192];
    build_usn(uuid, NULL, want_uuid, sizeof want_uuid);
    if (!strcmp(st, want_uuid)) {
      send_msearch_reply_one(fd, peer, peerlen, cfg, uuid, NULL);
      return;
    }
  }
  for (i = 0; i < SSDP_NTYPES; i++)
    if (!strcmp(st, ssdp_types[i])) {
      send_msearch_reply_one(fd, peer, peerlen, cfg, uuid, ssdp_types[i]);
      return;
    }
}

static void *ssdp_thread_fn(void *arg) {
  ssdp_arg_t *a = arg;
  mcast_t *recv_m;
  int fd;
  double next_announce;

  recv_m = mcast_open(AF_INET, SSDP_ADDR, SSDP_PORT, a->cfg->ssdp_iface, SSDP_RECV_TIMEOUT_MS);
  if (!recv_m) {
    log_line(TOOL_NAME ": ssdp: cannot join " SSDP_ADDR ":%d, DLNA discovery disabled", SSDP_PORT);
    free(a);
    return NULL;
  }
  fd = mcast_fd(recv_m);

  next_announce = 0.0;
  send_notify_all(a->cfg, a->uuid, 1);
  log_line(TOOL_NAME ": ssdp: announcing uuid:%s at http://%s/dlna/desc.xml", a->uuid, a->cfg->dlna_host);

  while (g_running) {
    char buf[SSDP_RECV_BUF];
    struct sockaddr_storage peer;
    socklen_t peerlen = sizeof peer;
    ssize_t n;
    double now = mono_seconds();

    if (now >= next_announce) {
      send_notify_all(a->cfg, a->uuid, 1);
      next_announce = now + a->cfg->ssdp_interval_s;
    }

    n = recvfrom(fd, buf, sizeof buf - 1, 0, (struct sockaddr *)&peer, &peerlen);
    if (n > 0) {
      buf[n] = '\0';
      handle_msearch(fd, buf, (struct sockaddr *)&peer, peerlen, a->cfg, a->uuid);
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      log_line(TOOL_NAME ": ssdp: recv error: %s", strerror(errno));
      break;
    }
  }

  send_notify_all(a->cfg, a->uuid, 0);
  mcast_close(recv_m);
  return NULL;
}

void ssdp_start(const config_t *cfg) {
  if (!cfg->enable_dlna)
    return;
  g_send = mcast_open_send(AF_INET, SSDP_ADDR, SSDP_PORT, cfg->ssdp_iface, cfg->ssdp_ttl);
  if (!g_send) {
    log_line(TOOL_NAME ": ssdp: cannot open send socket, DLNA discovery disabled");
    return;
  }
  g_ssdp_arg.cfg = cfg;
  ssdp_device_uuid(cfg, g_ssdp_arg.uuid);
  g_running = 1;
  if (pthread_create(&g_thread, NULL, ssdp_thread_fn, &g_ssdp_arg) != 0) {
    log_line(TOOL_NAME ": ssdp: cannot start thread, DLNA discovery disabled");
    g_running = 0;
    mcast_close(g_send);
    g_send = NULL;
  }
}

void ssdp_stop(void) {
  if (!g_running)
    return;
  g_running = 0;
  pthread_join(g_thread, NULL);
  if (g_send) {
    mcast_close(g_send);
    g_send = NULL;
  }
}
