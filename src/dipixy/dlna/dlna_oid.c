/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "dlna_int.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int parse_uint(const char *s, unsigned *out) {
  char *end;
  unsigned long v;
  if (!*s || !isdigit((unsigned char)*s))
    return -1;
  v = strtoul(s, &end, 10);
  if (*end != '\0')
    return -1;
  *out = (unsigned)v;
  return 0;
}

int parse_object_id(const char *s, oid_t *out) {
  memset(out, 0, sizeof *out);
  if (!strcmp(s, "0")) {
    out->kind = OID_ROOT;
    return 0;
  }
  if (!strcmp(s, "stdin")) {
    out->kind = OID_STDIN;
    return 0;
  }
  if (!strcmp(s, "rist")) {
    out->kind = OID_RIST;
    return 0;
  }
  if (s[0] == 'H') {
    if (parse_uint(s + 1, &out->ord) || out->ord == 0)
      return -1;
    out->kind = OID_HTTP;
    return 0;
  }
  if (s[0] == 'L') {
    const char *isep = strchr(s + 1, 'I');
    if (isep) {
      char ordbuf[16];
      size_t ordlen = (size_t)(isep - (s + 1));
      if (ordlen == 0 || ordlen >= sizeof ordbuf)
        return -1;
      memcpy(ordbuf, s + 1, ordlen);
      ordbuf[ordlen] = '\0';
      if (parse_uint(ordbuf, &out->ord) || out->ord == 0)
        return -1;
      if (parse_uint(isep + 1, &out->item_num) || out->item_num == 0)
        return -1;
      out->kind = OID_ITEM;
      return 0;
    }
    if (parse_uint(s + 1, &out->ord) || out->ord == 0)
      return -1;
    out->kind = OID_LIST;
    return 0;
  }
  return -1;
}

const char *source_kind_str(source_kind_t k) {
  switch (k) {
    case SRC_SDS:  return "sds";
    case SRC_M3U:  return "m3u";
    case SRC_XSPF: return "xspf";
    case SRC_CSV:  return "csv";
    case SRC_XML:  return "xml";
    case SRC_HTTP: return "http";
  }
  return "?";
}

const source_def_t *find_source(const config_t *cfg, unsigned ord) {
  for (int i = 0; i < cfg->n_sources; i++)
    if ((unsigned)cfg->sources[i].ordinal == ord)
      return &cfg->sources[i];
  return NULL;
}

/* uri -> "addr:port" display form, fallback title source */
const char *strip_scheme_at(const char *uri) {
  const char *p = strstr(uri, "://");
  if (!p)
    return uri;
  p += 3;
  if (*p == '@')
    p++;
  return p;
}

static const char *const path_seg_unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
static const char path_seg_hex[] = "0123456789ABCDEF";

/* RFC3986 percent-encode for one URI path segment, e.g. a -n name with spaces */
static void pct_encode_seg(const char *s, char *out, size_t outcap) {
  size_t oi = 0;
  for (; *s && oi + 1 < outcap; s++) {
    if (strchr(path_seg_unreserved, *s)) {
      out[oi++] = *s;
    } else {
      unsigned char c = (unsigned char)*s;
      if (oi + 4 > outcap)
        break;
      out[oi] = '%';
      out[oi + 1] = path_seg_hex[c >> 4];
      out[oi + 2] = path_seg_hex[c & 0xF];
      oi += 3;
    }
  }
  out[oi] = '\0';
}

void sb_init(strbuf_t *b, char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  if (cap)
    buf[0] = '\0';
}

void sb_add_n(strbuf_t *b, const char *s, size_t maxn) {
  size_t n = strlen(s);
  size_t room = b->cap > b->len ? b->cap - b->len - 1 : 0;
  if (n > maxn)
    n = maxn;
  if (n > room)
    n = room;
  memcpy(b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = '\0';
}

void sb_add(strbuf_t *b, const char *s) { sb_add_n(b, s, strlen(s)); }

void sb_add_u64(strbuf_t *b, uint64_t v) {
  char tmp[20], rev[21];
  size_t n = 0;
  if (!v) {
    tmp[n++] = '0';
  } else {
    while (v) {
      tmp[n++] = (char)('0' + v % 10);
      v /= 10;
    }
  }
  for (size_t i = 0; i < n; i++)
    rev[i] = tmp[n - 1 - i];
  rev[n] = '\0';
  sb_add(b, rev);
}

void build_play_path(const config_t *cfg, oid_kind_t kind, unsigned ord, unsigned item_num, media_type_t media_type, char *out, size_t outsz) {
  char name_enc[192];
  const char *fmt = media_type == MEDIA_RADIO ? "rawaudio" : "spts";
  strbuf_t b;
  sb_init(&b, out, outsz);
  switch (kind) {
    case OID_STDIN:
      if (cfg->stdin_name) {
        pct_encode_seg(cfg->stdin_name, name_enc, sizeof name_enc);
        sb_add(&b, "/");
        sb_add(&b, name_enc);
        sb_add(&b, "/");
        sb_add(&b, fmt);
      } else {
        sb_add(&b, "/stdin/");
        sb_add(&b, fmt);
      }
      break;
    case OID_RIST:
      if (cfg->rist_name) {
        pct_encode_seg(cfg->rist_name, name_enc, sizeof name_enc);
        sb_add(&b, "/");
        sb_add(&b, name_enc);
        sb_add(&b, "/");
        sb_add(&b, fmt);
      } else {
        sb_add(&b, "/rist/");
        sb_add(&b, fmt);
      }
      break;
    case OID_HTTP: {
      const source_def_t *src = find_source(cfg, ord);
      if (src && src->name) {
        pct_encode_seg(src->name, name_enc, sizeof name_enc);
        sb_add(&b, "/");
        sb_add(&b, name_enc);
        sb_add(&b, "/item/1/");
        sb_add(&b, fmt);
      } else {
        sb_add(&b, "/list/");
        sb_add_u64(&b, ord);
        sb_add(&b, "/item/1/");
        sb_add(&b, fmt);
      }
      break;
    }
    case OID_ITEM: {
      const source_def_t *src = find_source(cfg, ord);
      if (src && src->name) {
        pct_encode_seg(src->name, name_enc, sizeof name_enc);
        sb_add(&b, "/");
        sb_add(&b, name_enc);
        sb_add(&b, "/item/");
        sb_add_u64(&b, item_num);
        sb_add(&b, "/");
        sb_add(&b, fmt);
      } else {
        sb_add(&b, "/list/");
        sb_add_u64(&b, ord);
        sb_add(&b, "/item/");
        sb_add_u64(&b, item_num);
        sb_add(&b, "/");
        sb_add(&b, fmt);
      }
      break;
    }
    default:
      out[0] = '\0';
  }
}
