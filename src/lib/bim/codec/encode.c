/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/bim/codec.h"
#include "lib/bim/codec/codec_int.h"

int enum_bits(int n) {
  int b = 0;
  while ((1 << b) < n) b++;
  return b;
}

static int put_bit(bitwriter_t *bw, int v) { return bitwriter_put(bw, v ? 1 : 0, 1); }

static const char *skip_ws(const char *s) {
  while (isspace((unsigned char)*s)) s++;
  return s;
}

static int parse_uvlu8(const char *v, uint64_t *out) {
  char *end;
  unsigned long long n;
  v = skip_ws(v);
  if (*v < '0' || *v > '9') return -1;
  errno = 0;
  n = strtoull(v, &end, 10);
  if (errno || *skip_ws(end)) return -1;
  *out = n;
  return 0;
}

static int parse_boolean(const char *v, int *out) {
  size_t n;
  v = skip_ws(v);
  n = strlen(v);
  while (n && isspace((unsigned char)v[n - 1])) n--;
  if ((n == 4 && !strncmp(v, "true", 4)) || (n == 1 && *v == '1')) *out = 1;
  else if ((n == 5 && !strncmp(v, "false", 5)) || (n == 1 && *v == '0')) *out = 0;
  else return -1;
  return 0;
}

static int encode_value(const bim_attr_t *a, bim_codec_t codec, const char *v, bitwriter_t *bw, strrepo_writer_t *sw) {
  uint64_t u;
  int b;
  switch (codec) {
    case BIM_STRING: return dvb_string_encode(sw, v);
    case BIM_LOCATOR: return dvb_locator_encode(bw, sw, v);
    case BIM_DATETIME: return dvb_datetime_encode(bw, v);
    case BIM_DURATION: return put_bit(bw, 0) || dvb_string_encode(sw, v);
    case BIM_TERMREF: return dvb_controlledterm_encode(bw, sw, v);
    case BIM_BOOLEAN: return parse_boolean(v, &b) ? -1 : put_bit(bw, b);
    case BIM_UVLU8: return parse_uvlu8(v, &u) ? -1 : bitwriter_put_vluimsbf8(bw, u);
    case BIM_ENUM:
      for (int i = 0; a && i < a->nenums; i++)
        if (!strcmp(a->enums[i], v)) return bitwriter_put(bw, (uint64_t)i, enum_bits(a->nenums));
      return -1;
    default: return -1;
  }
}

static int encode_body(const bim_type_t *t, const bim_node_t *node, bitwriter_t *bw, strrepo_writer_t *sw);

static int encode_occurrences(const bim_part_t *p, int count, bitwriter_t *bw) {
  if ((unsigned)count < p->min || (p->max != BIM_UNBOUNDED && (unsigned)count > p->max)) return -1;
  if (p->max == 1) return p->min == 0 ? put_bit(bw, count) : 0;
  return bitwriter_put_vluimsbf4(bw, (uint64_t)count);
}

typedef struct {
  bitwriter_t *bw;
  strrepo_writer_t *sw;
  int dry; /* match only, no output */
} enc_t;

static int walk_part(enc_t *e, const bim_part_t *p, const bim_node_t *node, int *ci);

static int walk_one(enc_t *e, const bim_part_t *p, const bim_node_t *node, int *ci) {
  if (p->kind == BIM_PART_ELEM) {
    if (!p->type || *ci >= node->nkids || strcmp(node->kids[*ci]->name, p->name)) return -1;
    if (!e->dry && encode_body(p->type, node->kids[*ci], e->bw, e->sw)) return -1;
    (*ci)++;
    return 0;
  }
  if (p->kind == BIM_PART_SEQ) {
    for (int i = 0; i < p->nkids; i++)
      if (walk_part(e, &p->kids[i], node, ci)) return -1;
    return 0;
  }
  if (p->kind == BIM_PART_CHOICE) {
    enc_t dry = {NULL, NULL, 1};
    int pick = -1;
    int empty = -1;
    for (int i = 0; i < p->nkids && pick < 0; i++) {
      int c = *ci;
      if (walk_one(&dry, &p->kids[i], node, &c)) continue;
      if (c > *ci) pick = i;
      else if (empty < 0) empty = i;
    }
    if (pick < 0) pick = empty;
    if (pick < 0) return -1;
    if (!e->dry && bitwriter_put(e->bw, (uint64_t)pick, enum_bits(p->nkids))) return -1;
    return walk_one(e, &p->kids[pick], node, ci);
  }
  return -1;
}

static size_t out_mark(const enc_t *e) { return e->bw->len * 8 + (size_t)e->bw->cur_bits + e->sw->len; }

static int walk_part(enc_t *e, const bim_part_t *p, const bim_node_t *node, int *ci) {
  enc_t dry = {NULL, NULL, 1};
  int cur = *ci;
  int count = 0;
  while (p->max == BIM_UNBOUNDED || (unsigned)count < p->max) {
    int c = cur;
    if (walk_one(&dry, p, node, &c)) break;
    if (c == cur) {
      if ((unsigned)count < p->min) count = (int)p->min; /* empty occurrences */
      break;
    }
    cur = c;
    count++;
  }
  if ((unsigned)count < p->min) return -1;
  if (!e->dry && encode_occurrences(p, count, e->bw)) return -1;
  for (int i = 0; i < count; i++) {
    size_t mark = e->dry ? 0 : out_mark(e);
    if (walk_one(e, p, node, ci)) return -1;
    if (!e->dry && count > 1 && mark == out_mark(e)) return -1; /* decoder rejects */
  }
  return 0;
}

static int encode_body(const bim_type_t *t, const bim_node_t *node, bitwriter_t *bw, strrepo_writer_t *sw) {
  int ci = 0;
  for (int i = 0; i < t->nattrs; i++) {
    const bim_attr_t *a = &t->attrs[i];
    const char *v = bim_node_attr(node, a->name);
    if (!a->required && put_bit(bw, v != NULL)) return -1;
    if (!v) {
      if (a->required) return -1;
      continue;
    }
    if (encode_value(a, a->codec, v, bw, sw)) return -1;
  }
  if (t->content == BIM_CONTENT_SIMPLE) return encode_value(NULL, t->codec, node->text ? node->text : "", bw, sw);
  if (t->content == BIM_CONTENT_SEQ) {
    enc_t e = {bw, sw, 0};
    if (walk_part(&e, t->top, node, &ci)) return -1;
    return ci == node->nkids ? 0 : -1;
  }
  return 0;
}

int bim_encode_payload(const bim_type_t *t, const bim_node_t *root, bitwriter_t *bw, strrepo_writer_t *sw) {
  if (bitwriter_put(bw, DECODING_MODES, 8)) return -1;
  return encode_body(t, root, bw, sw);
}
