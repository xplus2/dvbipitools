/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ssdp.h"

#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/multicast.h"

#include "../version.h"

#define SSDP_ADDR "239.255.255.250"
#define SSDP_PORT 1900
#define SSDP_RECV_TIMEOUT_MS 1000
#define SSDP_RECV_BUF 2048
#define SSDP_PENDING_MAX 16
#define SSDP_MX_CAP 5u
#define SSDP_MX_MAX_ACCEPT 120u

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
#define SSDP_MASK_ALL ((2u << SSDP_NTYPES) - 1u)

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

static void build_usn(const char *uuid, const char *nt /* NULL = bare device uuid */, char *out, size_t outsz) {
  size_t off = bufcpy(out, outsz, "uuid:");
  off += bufcpy(out + off, outsz - off, uuid);
  if (nt) {
    off += bufcpy(out + off, outsz - off, "::");
    bufcpy(out + off, outsz - off, nt);
  }
}

static void send_notify_one(const config_t *cfg, const char *uuid, const char *nt, int alive) {
  char usn[192];
  char pkt[768];
  sbuf_t b;

  build_usn(uuid, nt, usn, sizeof usn);
  sbuf_init(&b, pkt, sizeof pkt);
  sbuf_add(&b, "NOTIFY * HTTP/1.1\r\nHOST: " SSDP_ADDR ":" STRINGIFY(SSDP_PORT) "\r\n");
  if (alive) {
    sbuf_add(&b, "CACHE-CONTROL: max-age=");
    sbuf_add_uint(&b, cfg->ssdp_max_age_s);
    sbuf_add(&b, "\r\nLOCATION: http://");
    sbuf_add(&b, cfg->dlna_host);
    sbuf_add(&b, "/dlna/desc.xml\r\nSERVER: dipixy/");
    sbuf_add(&b, TOOL_VERSION);
    sbuf_add(&b, " UPnP/1.0 DLNA/1.0\r\nNT: ");
    sbuf_add(&b, nt ? nt : usn);
    sbuf_add(&b, "\r\nNTS: ssdp:alive\r\nUSN: ");
    sbuf_add(&b, usn);
    sbuf_add(&b, "\r\n\r\n");
  } else {
    sbuf_add(&b, "NT: ");
    sbuf_add(&b, nt ? nt : usn);
    sbuf_add(&b, "\r\nNTS: ssdp:byebye\r\nUSN: ");
    sbuf_add(&b, usn);
    sbuf_add(&b, "\r\n\r\n");
  }
  if (!b.truncated)
    mcast_send(g_send, pkt, b.len);
}

static void send_notify_all(const config_t *cfg, const char *uuid, int alive) {
  send_notify_one(cfg, uuid, NULL, alive);
  for (size_t i = 0; i < SSDP_NTYPES; i++)
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
  char usn[192];
  char pkt[768];
  sbuf_t b;

  build_usn(uuid, nt, usn, sizeof usn);
  sbuf_init(&b, pkt, sizeof pkt);
  sbuf_add(&b, "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=");
  sbuf_add_uint(&b, cfg->ssdp_max_age_s);
  sbuf_add(&b, "\r\nEXT:\r\nLOCATION: http://");
  sbuf_add(&b, cfg->dlna_host);
  sbuf_add(&b, "/dlna/desc.xml\r\nSERVER: dipixy/");
  sbuf_add(&b, TOOL_VERSION);
  sbuf_add(&b, " UPnP/1.0 DLNA/1.0\r\nST: ");
  sbuf_add(&b, nt ? nt : usn);
  sbuf_add(&b, "\r\nUSN: ");
  sbuf_add(&b, usn);
  sbuf_add(&b, "\r\n\r\n");
  if (!b.truncated) sendto(fd, pkt, b.len, 0, peer, peerlen);
}

static void trim_end(char *s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

/* digits only, 1..SSDP_MX_MAX_ACCEPT. larger than SSDP_MX_CAP clamps to cap */
static int parse_mx(const char *s, unsigned *out) {
  unsigned v = 0;
  if (!*s) return 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return 0;
    v = v * 10 + (unsigned)(*s - '0');
    if (v > SSDP_MX_MAX_ACCEPT) return 0;
  }
  if (!v) return 0;
  *out = v > SSDP_MX_CAP ? SSDP_MX_CAP : v;
  return 1;
}

int ssdp_msearch_parse(const char *buf, const char *uuid, ssdp_query_t *q) {
  const char *line_end;
  const char *hdrs;
  char st[192];
  char man[32];
  char mx[16];
  char want_uuid[192];
  size_t i;

  if (strncmp(buf, "M-SEARCH", 8) != 0) return 0;
  line_end = strstr(buf, "\r\n");
  if (!line_end) return 0;
  hdrs = line_end + 2;
  if (!ssdp_msearch_header(hdrs, "MAN", man, sizeof man)) return 0;
  trim_end(man);
  if (strcmp(man, "\"ssdp:discover\"") != 0) return 0;
  if (!ssdp_msearch_header(hdrs, "MX", mx, sizeof mx)) return 0;
  trim_end(mx);
  if (!parse_mx(mx, &q->mx_s)) return 0;
  if (!ssdp_msearch_header(hdrs, "ST", st, sizeof st)) return 0;
  trim_end(st);
  if (!strcmp(st, "ssdp:all")) {
    q->mask = SSDP_MASK_ALL;
    return 1;
  }
  build_usn(uuid, NULL, want_uuid, sizeof want_uuid);
  if (!strcmp(st, want_uuid)) {
    q->mask = 1u;
    return 1;
  }
  for (i = 0; i < SSDP_NTYPES; i++)
    if (!strcmp(st, ssdp_types[i])) {
      q->mask = 2u << i;
      return 1;
    }
  return 0;
}

void ssdp_msearch_reply(int fd, const struct sockaddr *peer, socklen_t peerlen, const config_t *cfg, const char *uuid, unsigned mask) {
  if (mask & 1u) send_msearch_reply_one(fd, peer, peerlen, cfg, uuid, NULL);
  for (size_t i = 0; i < SSDP_NTYPES; i++)
    if (mask & (2u << i)) send_msearch_reply_one(fd, peer, peerlen, cfg, uuid, ssdp_types[i]);
}

typedef struct {
  struct sockaddr_in peer;
  unsigned mask;
  double due;
  int used;
} ssdp_pending_t;

/* 1 dg read. *to_group set when header destination is an mcast addr */
static ssize_t recv_query(int fd, char *buf, size_t cap, struct sockaddr_in *peer, int *to_group) {
  char cbuf[CMSG_SPACE(sizeof(struct in_pktinfo))];
  struct iovec iov = {buf, cap};
  struct msghdr mh;
  ssize_t n;

  memset(&mh, 0, sizeof mh);
  mh.msg_name = peer;
  mh.msg_namelen = sizeof *peer;
  mh.msg_iov = &iov;
  mh.msg_iovlen = 1;
  mh.msg_control = cbuf;
  mh.msg_controllen = sizeof cbuf;
  *to_group = 0;
  n = recvmsg(fd, &mh, MSG_DONTWAIT);
  if (n <= 0) return n;
  for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c)) {
    if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO) {
      struct in_pktinfo pi;
      memcpy(&pi, CMSG_DATA(c), sizeof pi);
      *to_group = IN_MULTICAST(ntohl(pi.ipi_addr.s_addr));
    }
  }
  return n;
}

static int peer_valid(const struct sockaddr_in *p) {
  uint32_t a = ntohl(p->sin_addr.s_addr);
  return p->sin_family == AF_INET && p->sin_port != 0 && a != INADDR_ANY && a != INADDR_BROADCAST && !IN_MULTICAST(a);
}

/* random delay in [0, mx) s. no entropy: no delay */
static double reply_delay(unsigned mx_s) {
  uint32_t r;
  if (getrandom(&r, sizeof r, 0) != (ssize_t)sizeof r) return 0.0;
  return (double)mx_s * ((double)r / 4294967296.0);
}

static void pending_add(ssdp_pending_t *pend, const struct sockaddr_in *peer, const ssdp_query_t *q, double now) {
  ssdp_pending_t *slot = NULL;
  for (size_t i = 0; i < SSDP_PENDING_MAX; i++) {
    if (!pend[i].used) {
      if (!slot) slot = &pend[i];
    } else if (pend[i].peer.sin_addr.s_addr == peer->sin_addr.s_addr) {
      return; /* 1 outstanding answer per src host */
    }
  }
  if (!slot) return;
  slot->peer = *peer;
  slot->mask = q->mask;
  slot->due = now + reply_delay(q->mx_s);
  slot->used = 1;
}

/* sends due answers, returns seconds until next or a negative value if none */
static double pending_flush(ssdp_pending_t *pend, int fd, const config_t *cfg, const char *uuid, double now) {
  double next = -1.0;
  for (size_t i = 0; i < SSDP_PENDING_MAX; i++) {
    if (!pend[i].used) continue;
    if (pend[i].due <= now) {
      ssdp_msearch_reply(fd, (struct sockaddr *)&pend[i].peer, sizeof pend[i].peer, cfg, uuid, pend[i].mask);
      pend[i].used = 0;
    } else if (next < 0.0 || pend[i].due - now < next) {
      next = pend[i].due - now;
    }
  }
  return next;
}

static void *ssdp_thread_fn(void *arg) {
  ssdp_arg_t *a = arg;
  ssdp_pending_t pend[SSDP_PENDING_MAX];
  mcast_t *recv_m;
  int fd;
  int on = 1;
  double next_announce;
  recv_m = mcast_open(AF_INET, SSDP_ADDR, SSDP_PORT, a->cfg->ssdp_iface, SSDP_RECV_TIMEOUT_MS);
  if (!recv_m) {
    log_code("YA500203", TOOL_NAME ": ssdp: cannot join " SSDP_ADDR ":%d, DLNA discovery disabled", SSDP_PORT);
    return NULL;
  }
  fd = mcast_fd(recv_m);
  memset(pend, 0, sizeof pend);
  /* no destination info: unicast cannot be told apart */
  if (setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &on, sizeof on) != 0) {
    log_code("YA500235", TOOL_NAME ": ssdp: IP_PKTINFO: %s, not answering searches", strerror(errno));
    on = 0;
  }
  next_announce = 0.0;
  send_notify_all(a->cfg, a->uuid, 1);
  log_line_ansi(TOOL_NAME ": ssdp: announcing uuid:%s at \e[0;34mhttp://%s/dlna/desc.xml\e[0m", a->uuid, a->cfg->dlna_host);

  while (g_running) {
    char buf[SSDP_RECV_BUF];
    struct sockaddr_in peer;
    struct pollfd pfd = {fd, POLLIN, 0};
    ssdp_query_t q;
    ssize_t n;
    int to_group;
    int wait_ms = SSDP_RECV_TIMEOUT_MS;
    double now = mono_seconds();
    double next_due;

    if (now >= next_announce) {
      send_notify_all(a->cfg, a->uuid, 1);
      next_announce = now + a->cfg->ssdp_interval_s;
    }

    next_due = pending_flush(pend, fd, a->cfg, a->uuid, now);
    if (next_due >= 0.0 && next_due * 1000.0 < wait_ms) wait_ms = (int)(next_due * 1000.0) + 1;
    if (poll(&pfd, 1, wait_ms) <= 0) continue;

    n = recv_query(fd, buf, sizeof buf - 1, &peer, &to_group);
    if (n > 0) {
      buf[n] = '\0';
      if (on && to_group && peer_valid(&peer) && ssdp_msearch_parse(buf, a->uuid, &q))
        pending_add(pend, &peer, &q, mono_seconds());
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      log_code("YE500204", TOOL_NAME ": ssdp: recv error: %s", strerror(errno));
      break;
    }
  }

  send_notify_all(a->cfg, a->uuid, 0);
  mcast_close(recv_m);
  return NULL;
}

void ssdp_start(const config_t *cfg) {
  if (!cfg->enable_dlna) return;
  g_send = mcast_open_send(AF_INET, SSDP_ADDR, SSDP_PORT, cfg->ssdp_iface, cfg->ssdp_ttl);
  if (!g_send) {
    log_code("YA500205", TOOL_NAME ": ssdp: cannot open send socket, DLNA discovery disabled");
    return;
  }
  g_ssdp_arg.cfg = cfg;
  ssdp_device_uuid(cfg, g_ssdp_arg.uuid);
  g_running = 1;
  if (pthread_create(&g_thread, NULL, ssdp_thread_fn, &g_ssdp_arg) != 0) {
    log_code("YA500206", TOOL_NAME ": ssdp: cannot start thread, DLNA discovery disabled");
    g_running = 0;
    mcast_close(g_send);
    g_send = NULL;
  }
}

void ssdp_stop(void) {
  if (!g_running) return;
  g_running = 0;
  pthread_join(g_thread, NULL);
  if (g_send) {
    mcast_close(g_send);
    g_send = NULL;
  }
}
