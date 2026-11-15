/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ioutil.h"

uint32_t rand_seed32(void) {
  uint32_t v;
  if (getrandom(&v, sizeof v, 0) == (ssize_t)sizeof v) return v;
  return (uint32_t)(time(NULL) ^ getpid());
}

int read_all(FILE *f, char **out, size_t *out_len) {
  size_t cap = 65536;
  size_t len = 0;
  struct stat st;
  char *buf;
  if (fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && (uintmax_t)st.st_size < SIZE_MAX / 2 && (size_t)st.st_size + 2 > cap)
    cap = (size_t)st.st_size + 2; /* room for NUL and EOF probe */
  buf = malloc(cap);
  if (!buf) return -1;
  for (;;) {
    size_t room;
    size_t n;
    if (len + 1 >= cap) {
      char *p;
      if (cap > SIZE_MAX / 2) {
        free(buf);
        return -1;
      }
      cap *= 2;
      p = realloc(buf, cap);
      if (!p) {
        free(buf);
        return -1;
      }
      buf = p;
    }
    room = cap - len - 1;
    n = fread(buf + len, 1, room, f);
    len += n;
    if (n < room) {
      if (ferror(f)) {
        free(buf);
        return -1;
      }
      break;
    }
  }
  buf[len] = '\0';
  *out = buf;
  *out_len = len;
  return 0;
}

int read_path(const char *path, char **out, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  int rc;
  if (!f) return -1;
  rc = read_all(f, out, out_len) ? -2 : 0;
  fclose(f);
  return rc;
}

int read_line(FILE *f, char *buf, size_t cap) {
  size_t l;
  int c;
  if (!fgets(buf, (int)cap, f)) return 0;
  l = strlen(buf);
  if ((l && buf[l - 1] == '\n') || l + 1 < cap) return 1;
  c = fgetc(f);
  if (c == EOF || c == '\n') return 1;
  while (c != EOF && c != '\n') c = fgetc(f);
  return -1;
}

int pipe_write_all(int fd, const unsigned char *buf, size_t n, const atomic_int *stop) {
  while (n) {
    ssize_t w = write(fd, buf, n);
    if (w > 0) {
      buf += (size_t)w;
      n -= (size_t)w;
      continue;
    }
    if (w < 0 && errno == EINTR) continue;
    if (w < 0 && errno == EAGAIN) {
      struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
      if (poll(&pfd, 1, 100) < 0 && errno != EINTR) return -1;
      if (atomic_load_explicit(stop, memory_order_relaxed)) return -1;
      continue;
    }
    return -1; /* EPIPE: reader gone */
  }
  return 0;
}
