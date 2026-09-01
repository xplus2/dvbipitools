/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ws_sources.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/jsonbuf.h"

static const char *source_kind_name(source_kind_t k) {
  switch (k) {
    case SRC_SDS:   return "sds";
    case SRC_M3U:   return "m3u";
    case SRC_XSPF:  return "xspf";
    case SRC_CSV:   return "csv";
    case SRC_XML:   return "xml";
    case SRC_HTTP:  return "http";
  }
  return "?";
}

typedef struct {
  jbuf_t *j;
  int n;
} item_emit_ctx_t;

static void emit_item_json(void *vctx, const channel_item_t *item) {
  item_emit_ctx_t *c = vctx;
  if (c->n++)
    jbuf_str(c->j, ",");
  jbuf_str(c->j, "{");
  jbuf_key(c->j, "name");
  jbuf_json_string(c->j, item->name);
  jbuf_str(c->j, ",");
  jbuf_key(c->j, "uri");
  jbuf_json_string(c->j, item->uri);
  jbuf_str(c->j, "}");
}

/* list_num 0: stdin/rist, no items */
static void emit_source_json(jbuf_t *j, const channels_t *channels, const char *kind, const char *name, unsigned list_num) {
  jbuf_str(j, "{");
  jbuf_key(j, "kind");
  jbuf_json_string(j, kind);
  jbuf_str(j, ",");
  jbuf_key(j, "name");
  if (name)
    jbuf_json_string(j, name);
  else
    jbuf_str(j, "null");
  if (list_num) {
    item_emit_ctx_t ictx = {j, 0};
    jbuf_str(j, ",");
    jbuf_key(j, "list_num");
    {
      char numbuf[11];
      jbuf_raw(j, numbuf, uint_to_str(numbuf, list_num));
    }
    jbuf_str(j, ",");
    jbuf_key(j, "items");
    jbuf_str(j, "[");
    channels_list_for_each(channels, list_num, emit_item_json, &ictx);
    jbuf_str(j, "]");
  }
  jbuf_str(j, "}");
}

int ws_sources_build_snapshot(const config_t *cfg, const channels_t *channels, char **out) {
  static _Thread_local jbuf_t j;
  int si, max_ord;

  jbuf_reset(&j);
  max_ord = cfg->stdin_ordinal;
  if (cfg->rist_ordinal > max_ord)
    max_ord = cfg->rist_ordinal;
  if (cfg->n_sources > 0 && cfg->sources[cfg->n_sources - 1].ordinal > max_ord)
    max_ord = cfg->sources[cfg->n_sources - 1].ordinal;

  jbuf_str(&j, "{");
  jbuf_key(&j, "type");
  jbuf_json_string(&j, "sources.snapshot");
  jbuf_str(&j, ",");
  jbuf_key(&j, "sources");
  jbuf_str(&j, "[");
  si = 0;
  for (int ord = 1; ord <= max_ord; ord++) {
    if (ord > 1 && (ord == cfg->stdin_ordinal || ord == cfg->rist_ordinal || (si < cfg->n_sources && cfg->sources[si].ordinal == ord)))
      jbuf_str(&j, ",");
    if (ord == cfg->stdin_ordinal)
      emit_source_json(&j, channels, "stdin", cfg->stdin_name, 0);
    else if (ord == cfg->rist_ordinal)
      emit_source_json(&j, channels, "rist", cfg->rist_name, 0);
    else if (si < cfg->n_sources && cfg->sources[si].ordinal == ord) {
      emit_source_json(&j, channels, source_kind_name(cfg->sources[si].kind), cfg->sources[si].name, (unsigned)ord);
      si++;
    }
  }
  jbuf_str(&j, "]}");
  if (j.failed)
    return -1;
  *out = j.buf;
  return 0;
}

int ws_sources_build_update(const channels_t *channels, const source_def_t *src, unsigned list_num, char **out) {
  static _Thread_local jbuf_t j;
  jbuf_reset(&j);
  jbuf_str(&j, "{");
  jbuf_key(&j, "type");
  jbuf_json_string(&j, "sources.update");
  jbuf_str(&j, ",");
  jbuf_key(&j, "sources");
  jbuf_str(&j, "[");
  emit_source_json(&j, channels, source_kind_name(src->kind), src->name, list_num);
  jbuf_str(&j, "]}");
  if (j.failed)
    return -1;
  *out = j.buf;
  return 0;
}
