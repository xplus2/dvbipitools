/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_METRICS_RENDER_RENDER_INT_H
#define LIB_METRICS_RENDER_RENDER_INT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/sys/ioutil.h"
#include "lib/metrics/render.h"

typedef enum { M_GAUGE, M_COUNTER, M_INFO, M_GAUGE_SIGNED } metric_kind_t;
typedef enum { TS_LABEL_NONE, TS_LABEL_STREAM, TS_LABEL_TABLE, TS_LABEL_PID, TS_LABEL_SERVICE } ts_label_kind_t;
#define DEF_ID_MAX 300 /* comfortably above highest metrics_id_t value */

typedef struct {
  metrics_id_t id;
  const char *name;
  metric_kind_t kind;
  const char *help;
  const char *label_name;     /* NULL = no metric-specific label */
  int composite_input_reason; /* label on wire is "input" SEP "reason" */
  ts_label_kind_t ts_label;
} metric_def_t;
typedef struct {
  const store_slot_t *slot;
  const char *label;
  size_t label_len;
  uint64_t value;
} entry_ref_t;

const char *metric_kind_name(metric_kind_t k);
void add_family_name(dstrbuf_t *sb, const char *name, const char *type, int openmetrics);
void escape_label(const char *in, char *out, size_t out_cap);
void append_base_labels(dstrbuf_t *sb, const store_slot_t *slot);
void append_composite_input_reason(dstrbuf_t *sb, const char *label);
void append_ts_labels(dstrbuf_t *sb, const char *label, ts_label_kind_t kind);
void count_entries(const store_t *st, const int *def_idx, size_t *count, size_t *total);
void fill_entry_refs(const store_t *st, const int *def_idx, entry_ref_t *refs, size_t *cursor);
void close_labels(dstrbuf_t *sb, size_t mark, int instance_labels);
void add_head(dstrbuf_t *sb, const char *name, const char *type, const char *help, int openmetrics);
void add_sample(dstrbuf_t *sb, const char *series, uint64_t value);

#endif
