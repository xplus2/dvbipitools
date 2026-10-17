/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/helper/xml_util.h"
#include "dipisds/input.h"
#include "dipisds/input/input_int.h"

const char *suffix(const char *path) {
  const char *dot = strrchr(path, '.');
  const char *slash = strrchr(path, '/');
  if (!dot || (slash && dot < slash)) return "";
  return dot;
}

int read_whole_file(const char *path, unsigned char **out, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  long sz;
  unsigned char *buf;
  if (!f) return -1;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return -1;
  }
  sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return -1;
  }
  rewind(f);
  buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    fclose(f);
    free(buf);
    return -1;
  }
  buf[sz] = '\0';
  fclose(f);
  *out = buf;
  *out_len = (size_t)sz;
  return 0;
}

/* rtp://[[src]@]<addr>:<port> or udp://..., [addr6] for v6. fills address/source/family/port */
int parse_mcast_uri(const char *uri, sds_service_t *s) {
  const char *p;
  const char *at;
  char addr[SDS_MAX_ADDR];
  size_t alen;
  char *end;
  unsigned long v;
  if (!strncmp(uri, "rtp://", 6)) s->rtp = 1;
  else if (!strncmp(uri, "udp://", 6)) s->rtp = 0;
  else return -1;
  p = uri + 6;
  at = strchr(p, '@');
  if (at) {
    size_t slen = (size_t)(at - p);
    if (slen >= sizeof s->source) return -1;
    if (slen > 2 && p[0] == '[' && at[-1] == ']') {
      p++;
      slen -= 2;
    }
    memcpy(s->source, p, slen);
    s->source[slen] = '\0';
    p = at + 1;
  }
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close) return -1;
    alen = (size_t)(close - (p + 1));
    if (alen == 0 || alen >= sizeof addr) return -1;
    memcpy(addr, p + 1, alen);
    addr[alen] = '\0';
    if (close[1] != ':') return -1;
    p = close + 2;
    s->family = AF_INET6;
  } else {
    const char *colon = strchr(p, ':');
    if (!colon) return -1;
    alen = (size_t)(colon - p);
    if (alen == 0 || alen >= sizeof addr) return -1;
    memcpy(addr, p, alen);
    addr[alen] = '\0';
    p = colon + 1;
    s->family = AF_INET;
  }
  v = strtoul(p, &end, 10);
  if (*end != '\0' || v == 0 || v > 65535) return -1;
  s->port = (unsigned)v;
  {
    unsigned char b[16];
    if (inet_pton(s->family, addr, b) != 1) return -1;
    if (s->family == AF_INET ? (b[0] & 0xF0) != 0xE0 : b[0] != 0xFF) return -1;
  }
  bufcpy(s->address, sizeof s->address, addr);
  if (s->source[0]) {
    unsigned char b[16];
    if (inet_pton(s->family, s->source, b) != 1) return -1;
  }
  return 0;
}

int dvb_id(const char *name, const char *v, unsigned *out) {
  unsigned long n;
  if (parse_ulong(v, 65535, &n)) {
    log_code("AE400215", "bad %s (0..65535): %s", name, v);
    return -1;
  }
  *out = (unsigned)n;
  return 0;
}
