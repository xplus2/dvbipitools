/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "protocol.h"

#define GROUP_MAX_ENTRIES 255

static void put_be16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

static void put_be64(unsigned char *p, uint64_t v) {
  for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * (7 - i)));
}

static uint16_t get_be16(const unsigned char *p) {
  return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static uint64_t get_be64(const unsigned char *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
  return v;
}

static size_t varint_len(uint64_t v) {
  size_t n = 1;
  while (v >= 0x80) {
    v >>= 7;
    n++;
  }
  return n;
}

static size_t put_varint(unsigned char *p, uint64_t v) {
  size_t n = 0;
  while (v >= 0x80) {
    p[n++] = (unsigned char)(v | 0x80);
    v >>= 7;
  }
  p[n++] = (unsigned char)v;
  return n;
}

static size_t get_varint(const unsigned char *p, size_t avail, uint64_t *v) {
  uint64_t acc = 0;
  for (size_t i = 0; i < avail && i < 10; i++) {
    if (i == 9 && p[i] > 1) return 0;
    acc |= (uint64_t)(p[i] & 0x7F) << (7 * i);
    if (!(p[i] & 0x80)) {
      *v = acc;
      return i + 1;
    }
  }
  return 0;
}

int metrics_writer_begin(metrics_writer_t *w, const metrics_hdr_t *hdr) {
  w->len = 0;
  w->grp = 0;
  w->grp_count = 0;
  w->part = 0;
  w->flush = NULL;
  w->flush_ctx = NULL;
  if (hdr->proto_version != METRICS_PROTO_VERSION || !hdr->metrics_id[0]) return -1;
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

static int writer_flush_part(metrics_writer_t *w) {
  int rc;
  if (!w->flush || w->part + 1 >= METRICS_MAX_PARTS) {
    w->len = 0;
    return -1;
  }
  w->buf[2] = (unsigned char)w->part;
  w->buf[3] = 0;
  rc = w->flush(w->flush_ctx, w->buf, w->len);
  if (rc) {
    w->len = 0;
    return -1;
  }
  w->part++;
  w->len = METRICS_HDR_LEN;
  w->grp = 0;
  w->grp_count = 0;
  return 0;
}

int metrics_writer_put(metrics_writer_t *w, metrics_id_t id, const char *label, uint64_t value) {
  size_t label_len, vlen, need;
  int same;
  if (w->len == 0) return -1;
  label_len = label ? strlen(label) : 0;
  if (label_len > METRICS_LABEL_MAX) {
    log_line("metrics: label for id %u truncated to %d bytes: %s", (unsigned)id, METRICS_LABEL_MAX, label);
    label_len = METRICS_LABEL_MAX;
  }
  vlen = varint_len(value);
  same = w->grp && w->grp_count < GROUP_MAX_ENTRIES && w->buf[w->grp] == label_len && (!label_len || !memcmp(w->buf + w->grp + 1, label, label_len));
  need = 2 + vlen + (same ? 0 : 2 + label_len);
  if (w->len + need > sizeof w->buf) {
    if (writer_flush_part(w)) return -1;
    same = 0;
  }
  if (!same) {
    w->grp = w->len;
    w->grp_count = 0;
    w->buf[w->len] = (unsigned char)label_len;
    if (label_len)
      memcpy(w->buf + w->len + 1, label, label_len);
    w->len += 2 + label_len;
  }
  put_be16(w->buf + w->len, (uint16_t)id);
  w->len += 2 + put_varint(w->buf + w->len + 2, value);
  w->grp_count++;
  w->buf[w->grp + 1 + label_len] = (unsigned char)w->grp_count;
  return 0;
}

size_t metrics_writer_finish(metrics_writer_t *w) {
  if (w->len == 0)
    return 0;
  w->buf[2] = (unsigned char)w->part;
  w->buf[3] = METRICS_FLAG_LAST;
  return w->len;
}

void metrics_reader_init_body(metrics_reader_t *r, unsigned version, const unsigned char *body, size_t len) {
  r->buf = body;
  r->len = len;
  r->pos = 0;
  r->version = version;
  r->label_off = 0;
  r->label_len = 0;
  r->grp_left = 0;
}

int metrics_reader_init(metrics_reader_t *r, const unsigned char *buf, size_t len, metrics_hdr_t *hdr) {
  if (len < METRICS_HDR_LEN) return -1;
  hdr->proto_version = buf[0];
  if (hdr->proto_version != METRICS_PROTO_V1 && hdr->proto_version != METRICS_PROTO_VERSION) return -1;
  hdr->component = (metrics_component_t)buf[1];
  if (hdr->component < METRICS_COMPONENT_TVHEAD || hdr->component > METRICS_COMPONENT_XY) return -1;
  memcpy(hdr->metrics_id, buf + 4, METRICS_ID_MAX);
  hdr->metrics_id[METRICS_ID_MAX - 1] = '\0';
  if (!hdr->metrics_id[0]) return -1;
  hdr->process_start_time = get_be64(buf + 4 + METRICS_ID_MAX);
  hdr->sequence = get_be64(buf + 4 + METRICS_ID_MAX + 8);
  hdr->snapshot_time = get_be64(buf + 4 + METRICS_ID_MAX + 16);
  hdr->part = hdr->proto_version == METRICS_PROTO_VERSION ? buf[2] : 0;
  hdr->flags = hdr->proto_version == METRICS_PROTO_VERSION ? buf[3] : METRICS_FLAG_LAST;
  metrics_reader_init_body(r, hdr->proto_version, buf + METRICS_HDR_LEN, len - METRICS_HDR_LEN);
  return 0;
}

static int next_v1(metrics_reader_t *r, metrics_id_t *id, const char **label, size_t *label_len, uint64_t *value) {
  size_t ll;
  if (r->pos == r->len) return 0;
  if (r->len - r->pos < 3) return -1;
  ll = r->buf[r->pos + 2];
  if (r->len - (r->pos + 3) < ll + 8) return -1;
  *id = (metrics_id_t)get_be16(r->buf + r->pos);
  *label = (const char *)r->buf + r->pos + 3;
  *label_len = ll;
  *value = get_be64(r->buf + r->pos + 3 + ll);
  r->pos += 3 + ll + 8;
  return 1;
}

static int next_v2(metrics_reader_t *r, metrics_id_t *id, const char **label, size_t *label_len, uint64_t *value) {
  size_t vn;

  if (r->grp_left == 0) {
    size_t ll;
    if (r->pos == r->len) return 0;
    ll = r->buf[r->pos];
    if (ll > METRICS_LABEL_MAX || r->len - r->pos < ll + 2) return -1;
    r->grp_left = r->buf[r->pos + 1 + ll];
    if (r->grp_left == 0) return -1;
    r->label_off = r->pos + 1;
    r->label_len = (unsigned)ll;
    r->pos += ll + 2;
  }
  if (r->len - r->pos < 3) return -1;
  vn = get_varint(r->buf + r->pos + 2, r->len - r->pos - 2, value);
  if (!vn) return -1;
  *id = (metrics_id_t)get_be16(r->buf + r->pos);
  *label = (const char *)r->buf + r->label_off;
  *label_len = r->label_len;
  r->pos += 2 + vn;
  r->grp_left--;
  return 1;
}

int metrics_reader_next_ref(metrics_reader_t *r, metrics_id_t *id, const char **label, size_t *label_len, uint64_t *value) {
  return r->version == METRICS_PROTO_V1 ? next_v1(r, id, label, label_len, value) : next_v2(r, id, label, label_len, value);
}

int metrics_reader_next(metrics_reader_t *r, metrics_id_t *id, char *label_out, size_t label_cap, uint64_t *value) {
  const char *label;
  size_t label_len;
  int rc = metrics_reader_next_ref(r, id, &label, &label_len, value);
  if (rc == 1 && label_out && label_cap) {
    size_t n = label_len < label_cap - 1 ? label_len : label_cap - 1;
    memcpy(label_out, label, n);
    label_out[n] = '\0';
  }
  return rc;
}
