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

typedef struct {
  bitreader_t *br;
  strrepo_reader_t *sr;
  dvb_codec_state_t st;
  int nodes;
} dec_t;

static int decode_value(dec_t *d, const bim_attr_t *a, bim_codec_t codec, char **out) {
  char buf[VALUE_MAX];
  uint64_t v;
  switch (codec) {
    case BIM_STRING: return strrepo_reader_next_dup(d->sr, out);
    case BIM_LOCATOR:
      if (dvb_locator_decode_st(d->br, d->sr, &d->st, buf, sizeof buf)) return -1;
      break;
    case BIM_DATETIME:
      if (dvb_datetime_decode_st(d->br, &d->st, buf, sizeof buf)) return -1;
      break;
    case BIM_DURATION:
      if (bitreader_get(d->br, 1, &v)) return -1;
      if (!v) return strrepo_reader_next_dup(d->sr, out);
      if (bitreader_get(d->br, 11, &v)) return -1;
      snprintf(buf, sizeof buf, "PT%lluH%lluM", (unsigned long long)(v / 60), (unsigned long long)(v % 60));
      break;
    case BIM_TERMREF:
      if (dvb_controlledterm_decode(d->br, d->sr, buf, sizeof buf)) return -1;
      break;
    case BIM_BOOLEAN:
      if (bitreader_get(d->br, 1, &v)) return -1;
      snprintf(buf, sizeof buf, "%s", v ? "true" : "false");
      break;
    case BIM_UVLU8:
      if (bitreader_get_vluimsbf8(d->br, &v)) return -1;
      snprintf(buf, sizeof buf, "%llu", (unsigned long long)v);
      break;
    case BIM_ENUM:
      if (!a || bitreader_get(d->br, enum_bits(a->nenums), &v) || v >= (uint64_t)a->nenums) return -1;
      snprintf(buf, sizeof buf, "%s", a->enums[v]);
      break;
    default: return -1;
  }
  *out = strdup(buf);
  return *out ? 0 : -1;
}

static int decode_body(dec_t *d, const bim_type_t *t, bim_node_t *node);

static int get_bits_n(dec_t *d, int n, uint64_t *v) { return n ? bitreader_get(d->br, n, v) : (*v = 0, 0); }

static int decode_part(dec_t *d, const bim_part_t *p, bim_node_t *node);

static int decode_one(dec_t *d, const bim_part_t *p, bim_node_t *node) {
  if (p->kind == BIM_PART_ELEM) {
    bim_node_t *k;
    if (!p->type) return -1;
    d->nodes++;
    if (d->nodes > MAX_NODES) return -1;
    k = bim_node_new(p->name);
    if (!k) return -1;
    if (decode_body(d, p->type, k) || bim_node_add(node, k)) {
      bim_node_free(k);
      return -1;
    }
    return 0;
  }
  if (p->kind == BIM_PART_SEQ) {
    for (int i = 0; i < p->nkids; i++)
      if (decode_part(d, &p->kids[i], node)) return -1;
    return 0;
  }
  if (p->kind == BIM_PART_CHOICE) {
    uint64_t idx;
    if (get_bits_n(d, enum_bits(p->nkids), &idx) || idx >= (uint64_t)p->nkids) return -1;
    return decode_one(d, &p->kids[idx], node);
  }
  return -1;
}

static int decode_part(dec_t *d, const bim_part_t *p, bim_node_t *node) {
  uint64_t count = 1;
  if (p->max == 1) {
    if (p->min == 0 && bitreader_get(d->br, 1, &count)) return -1;
  } else {
    if (bitreader_get_vluimsbf4(d->br, &count) || count < p->min || (p->max != BIM_UNBOUNDED && count > p->max)) return -1;
  }
  for (uint64_t i = 0; i < count; i++) {
    size_t bits = bitreader_bits_left(d->br);
    size_t spos = d->sr->pos;
    if (p->kind != BIM_PART_ELEM && p->kind != BIM_PART_SEQ && p->kind != BIM_PART_CHOICE) return -1;
    if (decode_one(d, p, node)) return -1;
    if (count > 1 && bits == bitreader_bits_left(d->br) && spos == d->sr->pos) return -1;
  }
  return 0;
}

static int decode_body(dec_t *d, const bim_type_t *t, bim_node_t *node) {
  for (int i = 0; i < t->nattrs; i++) {
    const bim_attr_t *a = &t->attrs[i];
    uint64_t present = 1;
    char *v = NULL;
    if (!a->required && bitreader_get(d->br, 1, &present)) return -1;
    if (!present) continue;
    if (decode_value(d, a, a->codec, &v)) return -1;
    if (node_put_attr(node, a->name, v)) return -1;
  }
  if (t->content == BIM_CONTENT_SIMPLE) {
    char *v = NULL;
    if (decode_value(d, NULL, t->codec, &v)) return -1;
    free(node->text);
    node->text = v;
    return 0;
  }
  if (t->content == BIM_CONTENT_SEQ) return decode_part(d, t->top, node);
  return 0;
}

int bim_decode_payload(const bim_type_t *t, const char *rootname, bitreader_t *br, strrepo_reader_t *sr, bim_node_t **out) {
  dec_t d;
  uint64_t modes;
  bim_node_t *root;
  memset(&d, 0, sizeof d);
  d.br = br;
  d.sr = sr;
  if (bitreader_get(br, 8, &modes) || (modes & 0xF0)) return -1;
  root = bim_node_new(rootname);
  if (!root) return -1;
  if (decode_body(&d, t, root)) {
    bim_node_free(root);
    return -1;
  }
  *out = root;
  return 0;
}
