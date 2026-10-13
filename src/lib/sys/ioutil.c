/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
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

size_t bufcpy(char *dst, size_t dstsz, const char *src) {
  size_t len = strlen(src);
  if (dstsz) {
    size_t n = len < dstsz - 1 ? len : dstsz - 1;
    if (n < len) {
      size_t back = 0;
      while (n > 0 && back < 3 && ((unsigned char)src[n] & 0xC0) == 0x80) { /* no cut inside UTF-8 sequence */
        n--;
        back++;
      }
      if (back == 3 && ((unsigned char)src[n] & 0xC0) == 0x80) n += back;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return len;
}

size_t uint_to_str_pad(char *dst, unsigned val, unsigned min_width) {
  char tmp[10]; /* UINT_MAX (32-bit) is 10 digits */
  unsigned n = 0;
  unsigned pad;
  if (val == 0)
    tmp[n++] = '0';
  else while (val) {
    tmp[n++] = (char)('0' + val % 10);
    val /= 10;
  }
  pad = n < min_width ? min_width - n : 0;
  for (unsigned i = 0; i < pad; i++) dst[i] = '0';
  for (unsigned i = 0; i < n; i++) dst[pad + i] = tmp[n - 1 - i];
  dst[pad + n] = '\0';
  return pad + n;
}

size_t uint_to_str(char *dst, unsigned val) { return uint_to_str_pad(dst, val, 0); }

size_t u64_to_dec(char *dst, uint64_t val) {
  char tmp[20];
  unsigned n = 0;
  if (val == 0) {
    tmp[n++] = '0';
  } else while (val) {
    tmp[n++] = (char)('0' + val % 10);
    val /= 10;
  }
  for (unsigned i = 0; i < n; i++) dst[i] = tmp[n - 1 - i];
  dst[n] = '\0';
  return n;
}

void *array_grow(void *arr, int *cap, int need, size_t elemsz) {
  int newcap;
  void *p;
  if (need <= *cap) return arr;
  newcap = *cap ? *cap * 2 : 16;
  if (newcap < need) newcap = need;
  p = realloc(arr, (size_t)newcap * elemsz);
  if (!p) return NULL;
  *cap = newcap;
  return p;
}

int growbuf_reserve(void **buf, size_t *cap, size_t elem_size, size_t need_elems, size_t initial_elems) {
  size_t ncap;
  void *np;
  if (*cap >= need_elems) return 0;
  ncap = *cap ? *cap : initial_elems;
  while (ncap < need_elems) ncap *= 2;
  np = realloc(*buf, ncap * elem_size);
  if (!np) return -1;
  *buf = np;
  *cap = ncap;
  return 0;
}

int all_digits(const char *s, int n) {
  for (int i = 0; i < n; i++) {
    if (!isdigit((unsigned char)s[i])) return 0;
  }
  return 1;
}

int parse_ulong(const char *s, unsigned long max, unsigned long *out) {
  unsigned long v = 0;
  if (!*s) return -1;
  for (; *s; s++) {
    unsigned d = (unsigned)(*s - '0');
    if (d > 9 || v > (ULONG_MAX - d) / 10) return -1;
    v = v * 10 + d;
  }
  if (v > max) return -1;
  *out = v;
  return 0;
}

void chomp(char *line) {
  size_t l = strlen(line);
  while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
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

size_t csv_split(char *line, char **fields, size_t max_fields) {
  size_t nf = 0;
  char *p = line;
  while (nf < max_fields) {
    fields[nf++] = p;
    p = strchr(p, ',');
    if (!p) break;
    *p = '\0';
    p++;
  }
  return nf;
}

int iso8601_split(const char *in, iso8601_t *out) {
  size_t l = strlen(in);
  const char *tail;
  if (l < 19 || in[4] != '-' || in[7] != '-' || in[10] != 'T' || in[13] != ':' || in[16] != ':')
    return -1;
  if (!all_digits(in, 4) || !all_digits(in + 5, 2) || !all_digits(in + 8, 2) || !all_digits(in + 11, 2) || !all_digits(in + 14, 2) || !all_digits(in + 17, 2))
    return -1;
  out->y = (in[0] - '0') * 1000 + (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
  out->mo = (in[5] - '0') * 10 + (in[6] - '0');
  out->d = (in[8] - '0') * 10 + (in[9] - '0');
  out->h = (in[11] - '0') * 10 + (in[12] - '0');
  out->mi = (in[14] - '0') * 10 + (in[15] - '0');
  out->s = (in[17] - '0') * 10 + (in[18] - '0');
  out->off_min = 0;
  tail = in + 19;
  if (*tail == '.') {
    const char *fe = tail + 1;
    while (isdigit((unsigned char)*fe)) fe++;
    if (fe == tail + 1) return -1;
    tail = fe;
  }
  if (*tail == 'Z') {
    out->offset_kind = ISO8601_OFF_Z;
    return 0;
  }
  if (*tail == '\0') {
    out->offset_kind = ISO8601_OFF_NONE;
    return 0;
  }
  if ((*tail == '+' || *tail == '-') && strlen(tail) >= 6 && tail[3] == ':' && all_digits(tail + 1, 2) && all_digits(tail + 4, 2)) {
    int oh = (tail[1] - '0') * 10 + (tail[2] - '0');
    int om = (tail[4] - '0') * 10 + (tail[5] - '0');
    out->off_min = (oh * 60 + om) * (tail[0] == '-' ? -1 : 1);
    out->offset_kind = ISO8601_OFF_NUMERIC;
    return 0;
  }
  return -1;
}

long date_to_mjd(int y, int mo, int d) {
  int yy = mo <= 2 ? y - 1 : y;
  int mm = mo <= 2 ? mo + 12 : mo;
  return 14956L + d + (long)((yy - 1900) * 365.25) + (long)((mm + 1) * 30.6001);
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

void sbuf_init(sbuf_t *b, char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  b->truncated = 0;
  if (cap) buf[0] = '\0';
}

void sbuf_add_n(sbuf_t *b, const char *s, size_t maxn) {
  size_t n = strlen(s);
  size_t room = b->cap > b->len ? b->cap - b->len - 1 : 0;
  if (!b->cap) {
    if (n && maxn) b->truncated = 1;
    return;
  }
  if (n > maxn) n = maxn;
  if (n > room) {
    n = room;
    b->truncated = 1;
  }
  memcpy(b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = '\0';
}

void sbuf_add(sbuf_t *b, const char *s) { sbuf_add_n(b, s, (size_t)-1); }

void sbuf_add_u64(sbuf_t *b, uint64_t v) {
  char tmp[21];
  u64_to_dec(tmp, v);
  sbuf_add(b, tmp);
}

void sbuf_add_uint(sbuf_t *b, unsigned v) {
  char tmp[16];
  uint_to_str(tmp, v);
  sbuf_add(b, tmp);
}

void sbuf_add_hex2(sbuf_t *b, unsigned v) {
  static const char digits[] = "0123456789abcdef";
  char tmp[3];
  tmp[0] = digits[(v >> 4) & 0xf];
  tmp[1] = digits[v & 0xf];
  tmp[2] = '\0';
  sbuf_add(b, tmp);
}

void sbuf_add_hex(sbuf_t *b, unsigned v) {
  static const char digits[] = "0123456789abcdef";
  char tmp[9];
  unsigned n = 0;
  do {
    tmp[n++] = digits[v & 0xf];
    v >>= 4;
  } while (v);
  for (unsigned i = 0; i < n / 2; i++) {
    char t = tmp[i];
    tmp[i] = tmp[n - 1 - i];
    tmp[n - 1 - i] = t;
  }
  tmp[n] = '\0';
  sbuf_add(b, tmp);
}

void dstrbuf_init(dstrbuf_t *sb) {
  sb->cap = 8192;
  sb->len = 0;
  sb->buf = malloc(sb->cap);
  if (!sb->buf) {
    sb->cap = 0;
    return;
  }
  sb->buf[0] = '\0';
}

void dstrbuf_add_n(dstrbuf_t *sb, const char *s, size_t n) {
  if (!sb->buf) return;
  if (sb->cap - sb->len <= n) {
    size_t ncap = sb->cap;
    char *p;
    while (ncap - sb->len <= n) ncap *= 2;
    p = realloc(sb->buf, ncap);
    if (!p) {
      free(sb->buf);
      sb->buf = NULL;
      sb->len = 0;
      sb->cap = 0;
      return;
    }
    sb->buf = p;
    sb->cap = ncap;
  }
  memcpy(sb->buf + sb->len, s, n);
  sb->len += n;
  sb->buf[sb->len] = '\0';
}

void dstrbuf_add(dstrbuf_t *sb, const char *s) { dstrbuf_add_n(sb, s, strlen(s)); }

void dstrbuf_add_u64(dstrbuf_t *sb, uint64_t v) {
  char tmp[21];
  u64_to_dec(tmp, v);
  dstrbuf_add(sb, tmp);
}

void dstrbuf_add_i64(dstrbuf_t *sb, int64_t v) {
  if (v < 0) dstrbuf_add(sb, "-");
  dstrbuf_add_u64(sb, v < 0 ? 0 - (uint64_t)v : (uint64_t)v);
}

void dstrbuf_appendf(dstrbuf_t *sb, const char *fmt, ...) {
  if (!sb->buf) return;
  for (;;) {
    va_list ap;
    int n;
    char *p;
    va_start(ap, fmt);
    n = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sb->cap - sb->len) {
      sb->len += (size_t)n;
      return;
    }
    while (sb->cap - sb->len <= (size_t)n) sb->cap *= 2;
    p = realloc(sb->buf, sb->cap);
    if (!p) {
      free(sb->buf);
      sb->buf = NULL;
      sb->len = 0;
      sb->cap = 0;
      return;
    }
    sb->buf = p;
  }
}

void pct_encode_seg(const char *s, char *out, size_t outcap) {
  static const char unreserved[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  static const char hex[] = "0123456789ABCDEF";
  size_t oi = 0;
  if (!outcap) return;
  for (; *s && oi + 1 < outcap; s++) {
    if (strchr(unreserved, *s)) {
      out[oi++] = *s;
    } else {
      unsigned char c = (unsigned char)*s;
      if (oi + 4 > outcap) break;
      out[oi] = '%';
      out[oi + 1] = hex[c >> 4];
      out[oi + 2] = hex[c & 0xF];
      oi += 3;
    }
  }
  out[oi] = '\0';
}
