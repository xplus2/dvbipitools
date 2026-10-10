/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* network peers for integration scripts. loopback only.
   usage: itest_helper httpd <port> <root>
          itest_helper jitter-src <http_port> <group> <mport> <out>
          itest_helper rtp-send <clip> <group> <port>
          itest_helper quic-probe <port> <initial|badtoken|version|short>
          itest_helper udp-relay <relay_port> <server_port> <stop_file> <out> */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TS_PKT 188
#define TS_PER_DGRAM 7
#define MAX_STAMPS 65536

static double now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sleep_s(double s) {
  struct timespec ts;
  if (s <= 0) return;
  ts.tv_sec = (time_t)s;
  ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
  nanosleep(&ts, NULL);
}

static void addr_lo(struct sockaddr_in *a, uint16_t port) {
  memset(a, 0, sizeof *a);
  a->sin_family = AF_INET;
  a->sin_port = htons(port);
  a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static int parse_port(const char *s) {
  char *end;
  long v = strtol(s, &end, 10);
  if (*s == '\0' || *end != '\0' || v < 1 || v > 65535) {
    fprintf(stderr, "bad port: %s\n", s);
    exit(2);
  }
  return (int)v;
}

static int udp_bind_lo(uint16_t port) {
  struct sockaddr_in a;
  int one = 1;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  addr_lo(&a, port);
  if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int send_all(int fd, const void *buf, size_t n) {
  const unsigned char *p = buf;
  while (n > 0) {
    ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
    if (w < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

static const char *ctype_of(const char *path) {
  const char *dot = strrchr(path, '.');
  if (!dot) return "application/octet-stream";
  if (!strcmp(dot, ".mp3")) return "audio/mpeg";
  if (!strcmp(dot, ".ts")) return "video/mp2t";
  if (!strcmp(dot, ".m3u8")) return "application/vnd.apple.mpegurl";
  return "application/octet-stream";
}

static void http_reply(int c, int code, const char *text, const char *ctype, const void *body, size_t len) {
  char hdr[256];
  int n = snprintf(hdr, sizeof hdr,"HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", code, text, ctype, len);
  if (send_all(c, hdr, (size_t)n) == 0 && len > 0) send_all(c, body, len);
}

static void http_serve_one(int c, const char *root) {
  char req[4096];
  char path[1024];
  char *sp;
  size_t got = 0;
  struct stat st;
  int fd;
  unsigned char *body;
  size_t off = 0;

  while (got < sizeof req - 1) {
    ssize_t r = recv(c, req + got, sizeof req - 1 - got, 0);
    if (r <= 0) return;
    got += (size_t)r;
    req[got] = '\0';
    if (strstr(req, "\r\n\r\n")) break;
  }
  if (strncmp(req, "GET /", 5) != 0) {
    fprintf(stderr, "bad request\n");
    http_reply(c, 405, "Method Not Allowed", "text/plain", "", 0);
    return;
  }
  sp = strchr(req + 5, ' ');
  if (!sp) return;
  *sp = '\0';
  if (strstr(req + 5, "..") || snprintf(path, sizeof path, "%s/%s", root, req + 5) >= (int)sizeof path) {
    fprintf(stderr, "GET /%s 400\n", req + 5);
    http_reply(c, 400, "Bad Request", "text/plain", "", 0);
    return;
  }
  fd = open(path, O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
    if (fd >= 0) close(fd);
    fprintf(stderr, "GET /%s 404\n", req + 5);
    http_reply(c, 404, "Not Found", "text/plain", "", 0);
    return;
  }
  body = malloc((size_t)st.st_size + 1);
  if (!body) {
    close(fd);
    return;
  }
  while (off < (size_t)st.st_size) {
    ssize_t r = read(fd, body + off, (size_t)st.st_size - off);
    if (r <= 0) break;
    off += (size_t)r;
  }
  close(fd);
  fprintf(stderr, "GET /%s 200\n", req + 5);
  http_reply(c, 200, "OK", ctype_of(path), body, off);
  free(body);
}

static int cmd_httpd(int argc, char **argv) {
  struct sockaddr_in a;
  int one = 1;
  int s;

  if (argc != 4) return 2;
  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  s = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  addr_lo(&a, (uint16_t)parse_port(argv[2]));
  if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 16) < 0) {
    perror("httpd");
    return 1;
  }
  fprintf(stderr, "Serving HTTP on 127.0.0.1 port %s\n", argv[2]);
  for (;;) {
    int c = accept(s, NULL, NULL);
    pid_t pid;
    if (c < 0) {
      if (errno == EINTR) continue;
      perror("accept");
      return 1;
    }
    pid = fork();
    if (pid == 0) {
      close(s);
      http_serve_one(c, argv[3]);
      _exit(0);
    }
    close(c);
  }
}

static int wait_readable(int fd, double timeout_s) {
  struct pollfd p;
  int ms = timeout_s <= 0 ? 0 : (int)(timeout_s * 1000.0) + 1;
  p.fd = fd;
  p.events = POLLIN;
  return poll(&p, 1, ms);
}

static void stamp_audio_dgram(const unsigned char *d, size_t n, double *stamps, size_t *cnt) {
  size_t i;
  if (n != (size_t)TS_PER_DGRAM * TS_PKT || *cnt >= MAX_STAMPS) return;
  for (i = 0; i < n; i += TS_PKT) {
    if (((((unsigned)d[i + 1] & 0x1Fu) << 8) | d[i + 2]) == 0x0101u) {
      stamps[(*cnt)++] = now_s();
      return;
    }
  }
}

/* paced mp3 over http with a 1 s burst and one 2 s stall. measures the longest
   gap between full audio datagrams on mc output within 8 s */
static int cmd_jitter_src(int argc, char **argv) {
  enum { NFRAMES = 330, STALL_AT = 130, FRAME_LEN = 417 };
  const double dt = 1152.0 / 44100.0;
  const double stall_s = 2.0;
  struct sockaddr_in a;
  struct ip_mreq mr;
  unsigned char frame[FRAME_LEN];
  unsigned char dg[2048];
  static double stamps[MAX_STAMPS];
  size_t cnt = 0;
  double extra = 0.0;
  double t0;
  double gap = 999.0;
  size_t i;
  size_t nwin = 0;
  int burst = (int)(1.0 / dt);
  int one = 1;
  int ls;
  int u;
  int c;
  char req[4096];
  FILE *out;
  const char *ok = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\n";

  if (argc != 6) return 2;
  signal(SIGPIPE, SIG_IGN);
  u = socket(AF_INET, SOCK_DGRAM, 0);
  setsockopt(u, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)parse_port(argv[4]));
  if (u < 0 || bind(u, (struct sockaddr *)&a, sizeof a) < 0) {
    perror("jitter-src udp bind");
    return 1;
  }
  mr.imr_multiaddr.s_addr = inet_addr(argv[3]);
  mr.imr_interface.s_addr = htonl(INADDR_LOOPBACK);
  if (setsockopt(u, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr) < 0) {
    perror("jitter-src membership");
    return 1;
  }

  ls = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  addr_lo(&a, (uint16_t)parse_port(argv[2]));
  if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 1) < 0) {
    perror("jitter-src listen");
    return 1;
  }
  if (wait_readable(ls, 10.0) <= 0) {
    fprintf(stderr, "jitter-src: no client within 10 s\n");
    return 1;
  }
  c = accept(ls, NULL, NULL);
  if (c < 0) return 1;
  if (recv(c, req, sizeof req, 0) < 0 || send_all(c, ok, strlen(ok)) < 0) return 1;

  memset(frame, 0, sizeof frame);
  frame[0] = 0xFF;
  frame[1] = 0xFB;
  frame[2] = 0x90;

  t0 = now_s();
  for (i = 0; i < NFRAMES; i++) {
    double due;
    if (i == STALL_AT) extra = stall_s;
    due = t0 + (i > (size_t)burst ? (double)(i - (size_t)burst) * dt : 0.0) + extra;
    for (;;) {
      double left = due - now_s();
      if (left <= 0) break;
      if (wait_readable(u, left) > 0) {
        ssize_t r = recv(u, dg, sizeof dg, 0);
        if (r > 0) stamp_audio_dgram(dg, (size_t)r, stamps, &cnt);
      }
    }
    if (send_all(c, frame, sizeof frame) < 0) {
      fprintf(stderr, "jitter-src: client closed early\n");
      return 1;
    }
  }
  for (t0 = now_s() + 1.0; now_s() < t0;) {
    if (wait_readable(u, t0 - now_s()) > 0) {
      ssize_t r = recv(u, dg, sizeof dg, 0);
      if (r > 0) stamp_audio_dgram(dg, (size_t)r, stamps, &cnt);
    }
  }
  close(c);

  if (cnt > 0) {
    double best = 0.0;
    for (nwin = 0; nwin < cnt && stamps[nwin] - stamps[0] < 8.0; nwin++) {
      if (nwin > 0 && stamps[nwin] - stamps[nwin - 1] > best) best = stamps[nwin] - stamps[nwin - 1];
    }
    if (nwin > 1) gap = best;
  }
  out = fopen(argv[5], "w");
  if (!out) return 1;
  fprintf(out, "%zu %.3f\n", nwin, gap);
  return fclose(out) == 0 ? 0 : 1;
}

/* rtp over udp multicast, every 5th adjacent pair swapped */
static int cmd_rtp_send(int argc, char **argv) {
  struct sockaddr_in dst;
  struct in_addr ifa;
  unsigned char (*frames)[12 + TS_PER_DGRAM * TS_PKT];
  unsigned char *data;
  struct stat st;
  size_t nfr;
  size_t i;
  int one = 1;
  int fd;
  FILE *f;

  if (argc != 5) return 2;
  f = fopen(argv[2], "rb");
  if (!f || fstat(fileno(f), &st) < 0) {
    perror("rtp-send clip");
    return 1;
  }
  data = malloc((size_t)st.st_size + 1);
  if (!data || fread(data, 1, (size_t)st.st_size, f) != (size_t)st.st_size) {
    fprintf(stderr, "rtp-send: read failed\n");
    free(data);
    return 1;
  }
  fclose(f);
  nfr = (size_t)st.st_size / (TS_PER_DGRAM * TS_PKT);
  frames = malloc(nfr ? nfr * sizeof *frames : 1);
  if (!frames) {
    free(data);
    return 1;
  }
  for (i = 0; i < nfr; i++) {
    uint16_t seq = (uint16_t)((1000 + i) & 0xFFFF);
    uint32_t ts = (uint32_t)(i * 900);
    unsigned char *h = frames[i];
    h[0] = 0x80;
    h[1] = 33;
    h[2] = (unsigned char)(seq >> 8);
    h[3] = (unsigned char)seq;
    h[4] = (unsigned char)(ts >> 24);
    h[5] = (unsigned char)(ts >> 16);
    h[6] = (unsigned char)(ts >> 8);
    h[7] = (unsigned char)ts;
    h[8] = 0;
    h[9] = 0;
    h[10] = 0x12;
    h[11] = 0x34;
    memcpy(h + 12, data + i * TS_PER_DGRAM * TS_PKT, TS_PER_DGRAM * TS_PKT);
  }
  free(data);
  for (i = 0; i + 1 < nfr; i += 5) {
    unsigned char tmp[12 + TS_PER_DGRAM * TS_PKT];
    memcpy(tmp, frames[i], sizeof tmp);
    memcpy(frames[i], frames[i + 1], sizeof tmp);
    memcpy(frames[i + 1], tmp, sizeof tmp);
  }

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    free(frames);
    return 1;
  }
  ifa.s_addr = htonl(INADDR_LOOPBACK);
  setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifa, sizeof ifa);
  setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof one);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((uint16_t)parse_port(argv[4]));
  dst.sin_addr.s_addr = inet_addr(argv[3]);
  for (i = 0; i < nfr; i++) {
    if (sendto(fd, frames[i], sizeof frames[i], 0, (struct sockaddr *)&dst, sizeof dst) < 0) {
      perror("rtp-send");
      free(frames);
      close(fd);
      return 1;
    }
    sleep_s(0.003);
  }
  free(frames);
  close(fd);
  return 0;
}

static void rand_fill(unsigned char *p, size_t n) {
  while (n > 0) {
    ssize_t r = getrandom(p, n, 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      memset(p, 0x5A, n);
      return;
    }
    p += r;
    n -= (size_t)r;
  }
}

static size_t quic_initial(unsigned char *out, const unsigned char *dcid, const unsigned char *scid, const unsigned char *token, size_t toklen, uint32_t version) {
  size_t n = 0;
  size_t rest;

  out[n++] = 0xC3;
  out[n++] = (unsigned char)(version >> 24);
  out[n++] = (unsigned char)(version >> 16);
  out[n++] = (unsigned char)(version >> 8);
  out[n++] = (unsigned char)version;
  out[n++] = 8;
  memcpy(out + n, dcid, 8);
  n += 8;
  out[n++] = 8;
  memcpy(out + n, scid, 8);
  n += 8;
  out[n++] = (unsigned char)toklen;
  memcpy(out + n, token, toklen);
  n += toklen;
  rest = 1200 - n - 2;
  out[n++] = (unsigned char)(0x40 | (rest >> 8));
  out[n++] = (unsigned char)rest;
  rand_fill(out + n, rest);
  return n + rest;
}

/* header-only QUIC v1 probes. prints server's reaction class */
static int cmd_quic_probe(int argc, char **argv) {
  unsigned char dcid[8];
  unsigned char scid[8];
  unsigned char tok[32] = {0};
  unsigned char data[1200];
  unsigned char resp[2048];
  struct sockaddr_in dst;
  struct timeval tv = {1, 0};
  size_t dlen;
  ssize_t rn;
  int fd;
  const char *what;

  if (argc != 4) return 2;
  what = argv[3];
  rand_fill(dcid, sizeof dcid);
  rand_fill(scid, sizeof scid);
  if (!strcmp(what, "initial")) {
    dlen = quic_initial(data, dcid, scid, tok, 0, 1);
  } else if (!strcmp(what, "badtoken")) {
    tok[0] = 0xB7;
    rand_fill(tok + 1, sizeof tok - 1);
    dlen = quic_initial(data, dcid, scid, tok, sizeof tok, 1);
  } else if (!strcmp(what, "version")) {
    dlen = quic_initial(data, dcid, scid, tok, 0, 0x0a0a0a0aU);
  } else if (!strcmp(what, "short")) {
    data[0] = 0x40;
    rand_fill(data + 1, sizeof data - 1);
    dlen = sizeof data;
  } else {
    fprintf(stderr, "bad probe\n");
    return 2;
  }

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return 1;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  addr_lo(&dst, (uint16_t)parse_port(argv[2]));
  if (sendto(fd, data, dlen, 0, (struct sockaddr *)&dst, sizeof dst) < 0) {
    perror("quic-probe send");
    return 1;
  }
  rn = recv(fd, resp, sizeof resp, 0);
  if (rn < 0) {
    puts("none");
    return 0;
  }
  if (rn >= 7 && (resp[0] & 0x80)) {
    uint32_t ver = ((uint32_t)resp[1] << 24) | ((uint32_t)resp[2] << 16) | ((uint32_t)resp[3] << 8) | resp[4];
    size_t dl = resp[5];
    int echoed = (size_t)rn >= 6 + dl && dl == sizeof scid && !memcmp(resp + 6, scid, sizeof scid);
    if (ver == 0) {
      puts(echoed ? "version-negotiation" : "bad-vn");
    } else if ((resp[0] & 0x30) == 0x30) {
      puts(echoed && rn < 1200 ? "retry" : "bad-retry");
    } else if ((resp[0] & 0x30) == 0x00) {
      puts(echoed ? "initial-close" : "bad-close");
    } else {
      puts("other");
    }
  } else {
    puts((size_t)rn < dlen ? "reset" : "bad-reset");
  }
  return 0;
}

/* udp relay client<->server. switches server-side source port 0.8 s after 1st client datagram.
   out: "switched replies_old replies_new" */
static int cmd_udp_relay(int argc, char **argv) {
  const double switch_after = 0.8;
  struct sockaddr_in server;
  struct sockaddr_in client;
  struct sockaddr_in from;
  struct pollfd p[3];
  unsigned char buf[4096];
  int have_client = 0;
  int switched = 0;
  int active = 0;
  unsigned long old_n = 0;
  unsigned long new_n = 0;
  double first = 0.0;
  double deadline;
  int front;
  int back[2];
  FILE *out;
  size_t k;

  if (argc != 6) return 2;
  front = udp_bind_lo((uint16_t)parse_port(argv[2]));
  back[0] = udp_bind_lo(0);
  back[1] = udp_bind_lo(0);
  if (front < 0 || back[0] < 0 || back[1] < 0) {
    perror("udp-relay bind");
    return 1;
  }
  addr_lo(&server, (uint16_t)parse_port(argv[3]));
  p[0].fd = front;
  p[1].fd = back[0];
  p[2].fd = back[1];
  for (k = 0; k < 3; k++) p[k].events = POLLIN;
  deadline = now_s() + 25.0;

  while (now_s() < deadline && access(argv[4], F_OK) != 0) {
    if (poll(p, 3, 100) <= 0) continue;
    for (k = 0; k < 3; k++) {
      socklen_t fl = sizeof from;
      ssize_t n;
      if (!(p[k].revents & POLLIN)) continue;
      n = recvfrom(p[k].fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
      if (n <= 0) continue;
      if (k == 0) {
        client = from;
        have_client = 1;
        if (first == 0.0) first = now_s();
        if (!switched && now_s() - first >= switch_after) {
          active = 1;
          switched = 1;
        }
        sendto(back[active], buf, (size_t)n, 0, (struct sockaddr *)&server, sizeof server);
      } else if (have_client) {
        if (k == 2) {
          new_n++;
        } else {
          old_n++;
        }
        sendto(front, buf, (size_t)n, 0, (struct sockaddr *)&client, sizeof client);
      }
    }
  }

  out = fopen(argv[5], "w");
  if (!out) return 1;
  fprintf(out, "%d %lu %lu\n", switched, old_n, new_n);
  return fclose(out) == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  int rc = 2;

  if (argc >= 2) {
    if (!strcmp(argv[1], "httpd")) rc = cmd_httpd(argc, argv);
    else if (!strcmp(argv[1], "jitter-src")) rc = cmd_jitter_src(argc, argv);
    else if (!strcmp(argv[1], "rtp-send")) rc = cmd_rtp_send(argc, argv);
    else if (!strcmp(argv[1], "quic-probe")) rc = cmd_quic_probe(argc, argv);
    else if (!strcmp(argv[1], "udp-relay")) rc = cmd_udp_relay(argc, argv);
  }
  if (rc == 2) fprintf(stderr, "usage: itest_helper httpd|jitter-src|rtp-send|quic-probe|udp-relay ...\n");
  return rc;
}
