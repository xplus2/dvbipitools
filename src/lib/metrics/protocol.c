/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/ioutil.h"

#include "protocol.h"

static void put_be16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

static void put_be64(unsigned char *p, uint64_t v) {
  int i;
  for (i = 0; i < 8; i++)
    p[i] = (unsigned char)(v >> (8 * (7 - i)));
}

static uint16_t get_be16(const unsigned char *p) {
  return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static uint64_t get_be64(const unsigned char *p) {
  uint64_t v = 0;
  int i;
  for (i = 0; i < 8; i++)
    v = (v << 8) | p[i];
  return v;
}

int metrics_writer_begin(metrics_writer_t *w, const metrics_hdr_t *hdr) {
  if (hdr->proto_version != METRICS_PROTO_VERSION || !hdr->metrics_id[0]) {
    w->len = 0;
    return -1;
  }
  memset(w->buf, 0, METRICS_HDR_LEN);
  w->buf[0] = hdr->proto_version;
  w->buf[1] = (unsigned char)hdr->component;
  bufcpy((char *)w->buf + 4, METRICS_ID_MAX, hdr->metrics_id);
  put_be64(w->buf + 4 + METRICS_ID_MAX, hdr->process_start_time);
  put_be64(w->buf + 4 + METRICS_ID_MAX + 8, hdr->sequence);
  put_be64(w->buf + 4 + METRICS_ID_MAX + 16, hdr->snapshot_time);
  w->len = METRICS_HDR_LEN;
  return 0;
}

int metrics_writer_put(metrics_writer_t *w, metrics_id_t id, const char *label, uint64_t value) {
  size_t label_len, need;

  if (w->len == 0)
    return -1;
  label_len = label ? strlen(label) : 0;
  if (label_len > METRICS_LABEL_MAX)
    label_len = METRICS_LABEL_MAX;
  need = 2 + 1 + label_len + 8;
  if (w->len + need > sizeof w->buf) {
    w->len = 0;
    return -1;
  }
  put_be16(w->buf + w->len, (uint16_t)id);
  w->buf[w->len + 2] = (unsigned char)label_len;
  if (label_len)
    memcpy(w->buf + w->len + 3, label, label_len);
  put_be64(w->buf + w->len + 3 + label_len, value);
  w->len += need;
  return 0;
}

size_t metrics_writer_finish(metrics_writer_t *w) {
  return w->len;
}

int metrics_reader_init(metrics_reader_t *r, const unsigned char *buf, size_t len, metrics_hdr_t *hdr) {
  if (len < METRICS_HDR_LEN)
    return -1;
  hdr->proto_version = buf[0];
  if (hdr->proto_version != METRICS_PROTO_VERSION)
    return -1;
  hdr->component = (metrics_component_t)buf[1];
  if (hdr->component < METRICS_COMPONENT_TVHEAD || hdr->component > METRICS_COMPONENT_BCG)
    return -1;
  memcpy(hdr->metrics_id, buf + 4, METRICS_ID_MAX);
  hdr->metrics_id[METRICS_ID_MAX - 1] = '\0';
  if (!hdr->metrics_id[0])
    return -1;
  hdr->process_start_time = get_be64(buf + 4 + METRICS_ID_MAX);
  hdr->sequence = get_be64(buf + 4 + METRICS_ID_MAX + 8);
  hdr->snapshot_time = get_be64(buf + 4 + METRICS_ID_MAX + 16);
  r->buf = buf;
  r->len = len;
  r->pos = METRICS_HDR_LEN;
  return 0;
}

int metrics_reader_next(metrics_reader_t *r, metrics_id_t *id, char *label_out, size_t label_cap, uint64_t *value) {
  size_t label_len;

  if (r->pos == r->len)
    return 0;
  if (r->len - r->pos < 3)
    return -1;
  label_len = r->buf[r->pos + 2];
  if (r->len - (r->pos + 3) < label_len + 8)
    return -1;
  *id = (metrics_id_t)get_be16(r->buf + r->pos);
  if (label_out && label_cap) {
    size_t n = label_len < label_cap - 1 ? label_len : label_cap - 1;
    memcpy(label_out, r->buf + r->pos + 3, n);
    label_out[n] = '\0';
  }
  *value = get_be64(r->buf + r->pos + 3 + label_len);
  r->pos += 3 + label_len + 8;
  return 1;
}
