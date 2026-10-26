/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/uriparse.h"
#include "lib/sys/ioutil.h"
#include "dlna_int.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int parse_uint(const char *s, unsigned *out) {
  unsigned long v;
  if (parse_ulong(s, UINT_MAX, &v)) return -1;
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
    if (parse_uint(s + 1, &out->ord) || out->ord == 0) return -1;
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
      if (parse_uint(ordbuf, &out->ord) || out->ord == 0) return -1;
      if (parse_uint(isep + 1, &out->item_num) || out->item_num == 0) return -1;
      out->kind = OID_ITEM;
      return 0;
    }
    if (parse_uint(s + 1, &out->ord) || out->ord == 0) return -1;
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
    case SRC_MCAST: return "mcast";
  }
  return "?";
}

const source_def_t *find_source(const config_t *cfg, unsigned ord) {
  for (int i = 0; i < cfg->n_sources; i++) {
    if ((unsigned)cfg->sources[i].ordinal == ord) return &cfg->sources[i];
  }
  return NULL;
}

/* uri -> "addr:port" display form, fallback title source */
const char *strip_scheme_at(const char *uri) {
  const char *p = strstr(uri, "://");
  if (!p) return uri;
  p += 3;
  if (*p == '@') p++;
  return p;
}

static void build_simple_path(sbuf_t *b, const char *name, const char *fallback, char *name_enc, size_t name_enc_sz, const char *fmt) {
  if (name) {
    uriparse_pct_encode_seg(name, name_enc, name_enc_sz);
    sbuf_add(b, "/");
    sbuf_add(b, name_enc);
  } else {
    sbuf_add(b, fallback);
  }
  sbuf_add(b, "/");
  sbuf_add(b, fmt);
}

static void build_item_path(sbuf_t *b, const source_def_t *src, unsigned ord, unsigned item_num, char *name_enc, size_t name_enc_sz, const char *fmt) {
  if (src && src->name) {
    uriparse_pct_encode_seg(src->name, name_enc, name_enc_sz);
    sbuf_add(b, "/");
    sbuf_add(b, name_enc);
  } else {
    sbuf_add(b, "/list/");
    sbuf_add_u64(b, ord);
  }
  sbuf_add(b, "/item/");
  sbuf_add_u64(b, item_num);
  sbuf_add(b, "/");
  sbuf_add(b, fmt);
}

void build_play_path(const config_t *cfg, oid_kind_t kind, unsigned ord, unsigned item_num, media_type_t media_type, char *out, size_t outsz) {
  char name_enc[192];
  const char *fmt = media_type == MEDIA_RADIO ? "rawaudio" : "spts";
  sbuf_t b;
  sbuf_init(&b, out, outsz);
  switch (kind) {
    case OID_STDIN:
      build_simple_path(&b, cfg->stdin_name, "/stdin", name_enc, sizeof name_enc, fmt);
      break;
    case OID_RIST:
      build_simple_path(&b, cfg->rist_name, "/rist", name_enc, sizeof name_enc, fmt);
      break;
    case OID_HTTP:
      build_item_path(&b, find_source(cfg, ord), ord, 1, name_enc, sizeof name_enc, fmt);
      break;
    case OID_ITEM:
      build_item_path(&b, find_source(cfg, ord), ord, item_num, name_enc, sizeof name_enc, fmt);
      break;
    default:
      out[0] = '\0';
  }
}
