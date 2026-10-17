/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/metrics/render.h"
#include "lib/metrics/render/render_int.h"

/* backslash/quote/newline escaping per OpenMetrics/Prometheus text label-value grammar.
   invalid UTF-8 becomes U+FFFD, scrape would be rejected otherwise */
void escape_label(const char *in, char *out, size_t out_cap) {
  size_t oi = 0;
  while (*in && oi + 4 < out_cap) {
    size_t n = metrics_utf8_seq_len((const unsigned char *)in, strlen(in));
    if (!n) {
      memcpy(out + oi, "\xEF\xBF\xBD", 3);
      oi += 3;
      in++;
    } else if (*in == '\\' || *in == '"') {
      out[oi++] = '\\';
      out[oi++] = *in++;
    } else if (*in == '\n') {
      out[oi++] = '\\';
      out[oi++] = 'n';
      in++;
    } else {
      memcpy(out + oi, in, n);
      oi += n;
      in += n;
    }
  }
  out[oi] = '\0';
}

/* label named headend_id, not instance: Prometheus already assigns instance
   per scrape target, colliding names get renamed exported_instance */
void append_base_labels(dstrbuf_t *sb, const store_slot_t *slot) {
  char esc_headend_id[2 * METRICS_ID_MAX + 2];
  escape_label(slot->metrics_id, esc_headend_id, sizeof esc_headend_id);
  dstrbuf_add(sb, "component=\"");
  dstrbuf_add(sb, metrics_component_name(slot->component));
  dstrbuf_add(sb, "\",headend_id=\"");
  dstrbuf_add(sb, esc_headend_id);
  dstrbuf_add(sb, "\"");
}

void append_composite_input_reason(dstrbuf_t *sb, const char *label) {
  char input_part[METRICS_LABEL_MAX + 1], reason_part[METRICS_LABEL_MAX + 1];
  char esc_input[2 * METRICS_LABEL_MAX + 2], esc_reason[2 * METRICS_LABEL_MAX + 2];
  const char *sep = strchr(label, METRICS_LABEL_SEP);
  if (sep) {
    size_t ilen = (size_t)(sep - label);
    if (ilen >= sizeof input_part)
      ilen = sizeof input_part - 1;
    memcpy(input_part, label, ilen);
    input_part[ilen] = '\0';
    bufcpy(reason_part, sizeof reason_part, sep + 1);
  } else {
    bufcpy(input_part, sizeof input_part, label);
    reason_part[0] = '\0';
  }
  escape_label(input_part, esc_input, sizeof esc_input);
  escape_label(reason_part, esc_reason, sizeof esc_reason);
  dstrbuf_add(sb, ",input=\"");
  dstrbuf_add(sb, esc_input);
  dstrbuf_add(sb, "\",reason=\"");
  dstrbuf_add(sb, esc_reason);
  dstrbuf_add(sb, "\"");
}

void append_ts_labels(dstrbuf_t *sb, const char *label, ts_label_kind_t kind) {
  char stream[METRICS_LABEL_MAX + 1];
  char esc[2 * METRICS_LABEL_MAX + 2];
  const char *sep = kind >= TS_LABEL_TABLE ? strchr(label, METRICS_LABEL_SEP) : NULL;
  size_t n = sep ? (size_t)(sep - label) : strlen(label);

  if (!label[0]) return;
  memcpy(stream, label, n);
  stream[n] = '\0';
  dstrbuf_add(sb, ",direction=\"");
  dstrbuf_add(sb, strncmp(stream, "in", 2) == 0 ? "input" : "output");
  dstrbuf_add(sb, "\",stream=\"");
  escape_label(stream, esc, sizeof esc);
  dstrbuf_add(sb, esc);
  dstrbuf_add(sb, "\"");
  if (sep) {
    const char *suffix_label;
    switch (kind) {
      case TS_LABEL_SERVICE: suffix_label = ",service=\""; break;
      case TS_LABEL_PID:     suffix_label = ",pid=\""; break;
      default:               suffix_label = ",table=\""; break;
    }
    escape_label(sep + 1, esc, sizeof esc);
    dstrbuf_add(sb, suffix_label);
    dstrbuf_add(sb, esc);
    dstrbuf_add(sb, "\"");
  }
}

void count_entries(const store_t *st, const int *def_idx, size_t *count, size_t *total) {
  for (int si = 0; si < STORE_MAX_INSTANCES; si++) {
    const store_slot_t *slot = &st->slots[si];
    metrics_reader_t r;
    metrics_id_t id;
    const char *label;
    size_t label_len;
    uint64_t value;
    if (!slot->valid) continue;
    metrics_reader_init_body(&r, slot->version, slot->live.data, slot->live.len);
    while (metrics_reader_next_ref(&r, &id, &label, &label_len, &value) == 1) {
      int di = ((unsigned)id < DEF_ID_MAX) ? def_idx[id] : -1;
      if (di >= 0) {
        count[(unsigned)di]++;
        (*total)++;
      }
    }
  }
}

void fill_entry_refs(const store_t *st, const int *def_idx, entry_ref_t *refs, size_t *cursor) {
  for (int si = 0; si < STORE_MAX_INSTANCES; si++) {
    const store_slot_t *slot = &st->slots[si];
    metrics_reader_t r;
    metrics_id_t id;
    const char *label;
    size_t label_len;
    uint64_t value;
    if (!slot->valid) continue;
    metrics_reader_init_body(&r, slot->version, slot->live.data, slot->live.len);
    while (metrics_reader_next_ref(&r, &id, &label, &label_len, &value) == 1) {
      int di = ((unsigned)id < DEF_ID_MAX) ? def_idx[id] : -1;
      if (di >= 0) {
        entry_ref_t *ref = &refs[cursor[(unsigned)di]++];
        ref->slot = slot;
        ref->label = label;
        ref->label_len = label_len;
        ref->value = value;
      }
    }
  }
}

/* closes a label set opened at mark (just after '{'). without base labels the set can be empty or lead with ',' */
void close_labels(dstrbuf_t *sb, size_t mark, int instance_labels) {
  if (!sb->buf) return;
  if (instance_labels) {
    dstrbuf_add(sb, "}");
    return;
  }
  if (sb->len == mark) {
    sb->len = mark - 1;
  } else {
    if (sb->buf[mark] == ',') {
      memmove(sb->buf + mark, sb->buf + mark + 1, sb->len - mark - 1);
      sb->len--;
    }
    dstrbuf_add(sb, "}");
  }
  sb->buf[sb->len] = '\0';
}

void add_head(dstrbuf_t *sb, const char *name, const char *type, const char *help, int openmetrics) {
  dstrbuf_add(sb, "# HELP ");
  add_family_name(sb, name, type, openmetrics);
  dstrbuf_add(sb, " ");
  dstrbuf_add(sb, help);
  dstrbuf_add(sb, "\n# TYPE ");
  add_family_name(sb, name, type, openmetrics);
  dstrbuf_add(sb, " ");
  dstrbuf_add(sb, type);
  dstrbuf_add(sb, "\n");
}
