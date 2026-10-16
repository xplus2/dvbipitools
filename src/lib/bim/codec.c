/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sys/ioutil.h"
#include "codec.h"

int dvb_string_encode(strrepo_writer_t *sw, const char *s) {
  return strrepo_writer_put(sw, s);
}

int dvb_string_decode(strrepo_reader_t *sr, char *out, size_t outcap) {
  return strrepo_reader_next(sr, out, outcap);
}

int dvb_string_decode_dup(strrepo_reader_t *sr, char **out) {
  char *s;
  if (strrepo_reader_next_dup(sr, &s)) return -1;
  if (!s[0]) {
    free(s);
    s = NULL;
  }
  free(*out);
  *out = s;
  return 0;
}

int dvb_locator_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *uri) {
  if (bitwriter_put(bw, 0, 1)) return -1; /* optimized_codec_flag = 0, always string fallback */
  return dvb_string_encode(sw, uri);
}

static int sbuf_hex(char *out, size_t cap, size_t *n, const char *pre, unsigned v, const char *post) {
  int w = snprintf(out + *n, *n < cap ? cap - *n : 0, "%s%x%s", pre, v, post);
  if (w < 0 || *n + (size_t)w >= cap) return -1;
  *n += (size_t)w;
  return 0;
}

/* OptimizedDVBLocator() -> dvb:// text. prefix reuse via st */
static int locator_decode_optimized(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, char *out, size_t outcap) {
  uint64_t v;
  size_t n = 0;
  unsigned ctags[32];
  int nctag = 0;
  uint64_t event_or_tva = 0;
  uint64_t event_id = 0;
  char path[256];

  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    uint64_t ts;
    uint64_t on;
    uint64_t sid;
    if (bitreader_get(br, 16, &ts) || bitreader_get(br, 16, &on) || bitreader_get(br, 16, &sid)) return -1;
    st->have_prefix = 1;
    st->tsid = (unsigned)ts;
    st->onid = (unsigned)on;
    st->sid = (unsigned)sid;
  } else if (!st->have_prefix) {
    return -1;
  }
  if (outcap < 8) return -1;
  memcpy(out, "dvb://", 7);
  n = 6;
  if (sbuf_hex(out, outcap, &n, "", st->onid, ".")) return -1;
  if (st->tsid && sbuf_hex(out, outcap, &n, "", st->tsid, "")) return -1;
  if (sbuf_hex(out, outcap, &n, ".", st->sid, "")) return -1;
  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    do {
      uint64_t tag;
      uint64_t cont;
      if (bitreader_get(br, 8, &tag) || bitreader_get(br, 1, &cont)) return -1;
      if (nctag < 32) ctags[nctag++] = (unsigned)tag;
      v = cont;
    } while (v);
  }
  for (int i = 0; i < nctag; i++)
    if (sbuf_hex(out, outcap, &n, ".", ctags[i], "")) return -1;
  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    uint64_t cid;
    if (bitreader_get(br, 32, &cid)) return -1;
    if (sbuf_hex(out, outcap, &n, ";cid=", (unsigned)cid, "")) return -1;
  }
  if (bitreader_get(br, 2, &event_or_tva)) return -1;
  if (event_or_tva == 3) return -1;
  if (event_or_tva) {
    if (bitreader_get(br, 16, &event_id)) return -1;
    if (sbuf_hex(out, outcap, &n, event_or_tva == 1 ? ";" : ";tva=", (unsigned)event_id, "")) return -1;
  }
  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    uint64_t day;
    uint64_t tm;
    uint64_t dur;
    if (bitreader_get(br, 1, &v)) return -1;
    if (v) {
      if (bitreader_get(br, 16, &day)) return -1;
      st->have_day = 1;
      st->day = (unsigned)day;
    } else if (!st->have_day) {
      return -1;
    }
    if (bitreader_get(br, 17, &tm) || bitreader_get(br, 17, &dur)) return -1;
    if (sbuf_hex(out, outcap, &n, "@", st->day, "")) return -1;
    if (sbuf_hex(out, outcap, &n, ":", (unsigned)tm, "")) return -1;
    if (sbuf_hex(out, outcap, &n, "--", (unsigned)dur, "")) return -1;
  }
  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    if (dvb_string_decode(sr, path, sizeof path)) return -1;
    if (n + 1 + strlen(path) >= outcap) return -1;
    out[n++] = '/';
    memcpy(out + n, path, strlen(path) + 1);
    return 0;
  }
  out[n] = '\0';
  return 0;
}

int dvb_locator_decode_st(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, char *out, size_t outcap) {
  uint64_t flag;
  if (bitreader_get(br, 1, &flag)) return -1;
  if (flag) return locator_decode_optimized(br, sr, st, out, outcap);
  return dvb_string_decode(sr, out, outcap);
}

int dvb_locator_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap) {
  dvb_codec_state_t st;
  memset(&st, 0, sizeof st);
  return dvb_locator_decode_st(br, sr, &st, out, outcap);
}

#define MJD_MIN 15079L /* 1900-03-01 */
#define MJD_MAX 88127L /* 2100-02-28 */
#define MS_PER_DAY 86400000ULL
#define PUBLISHED_MJD_BASE 40587L /* 1970-01-01, 16 bit MJD window start */

static void mjd_to_date(long mjd, int *y, int *mo, int *d) {
  int yp = (int)((mjd - 15078.2) / 365.25);
  int mp = (int)((mjd - 14956.1 - (int)(yp * 365.25)) / 30.6001);
  int dd = (int)(mjd - 14956 - (int)(yp * 365.25) - (int)(mp * 30.6001));
  int k = (mp == 14 || mp == 15) ? 1 : 0;
  *y = yp + k + 1900;
  *mo = mp - 1 - k * 12;
  *d = dd;
}

static long floor_div(long a, long b) {
  long q = a / b;
  long r = a % b;
  return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

int dvb_datetime_encode(bitwriter_t *bw, const char *iso8601) {
  iso8601_t f;
  long mjd_local;
  long total_minutes;
  long mjd_utc;
  long minute_of_day;
  int hour, min;
  uint64_t ms;

  if (iso8601_split(iso8601, &f)) return -1;
  /* no offset: UTC (off_min 0) */
  mjd_local = date_to_mjd(f.y, f.mo, f.d);
  total_minutes = mjd_local * 1440L + f.h * 60L + f.mi - f.off_min;
  mjd_utc = floor_div(total_minutes, 1440L);
  minute_of_day = total_minutes - mjd_utc * 1440L;
  if (mjd_utc < MJD_MIN || mjd_utc > MJD_MAX) return -1;
  hour = (int)(minute_of_day / 60);
  min = (int)(minute_of_day % 60);
  ms = (uint64_t)(hour * 3600 + min * 60 + f.s) * 1000ULL;
  if (bitwriter_put(bw, 0, 2)) return -1; /* dateTime_flag = 00, full precision */
  if (bitwriter_put(bw, (uint64_t)mjd_utc, 32)) return -1;
  return bitwriter_put(bw, ms, 32);
}

static int fmt_iso(long mjd, long minute_of_day, int sec, char *out, size_t outcap) {
  int y;
  int mo;
  int d;
  mjd_to_date(mjd, &y, &mo, &d);
  if ((size_t)snprintf(out, outcap, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, (int)(minute_of_day / 60), (int)(minute_of_day % 60), sec) >= outcap) return -1;
  return 0;
}

int dvb_datetime_decode_st(bitreader_t *br, dvb_codec_state_t *st, char *out, size_t outcap) {
  uint64_t flag;
  uint64_t mjd_v;
  uint64_t ms_v;

  if (bitreader_get(br, 2, &flag)) return -1;
  if (flag == 1) {
    uint64_t date_flag;
    uint64_t date;
    uint64_t minutes;
    if (bitreader_get(br, 1, &date_flag)) return -1;
    if (date_flag) {
      long full;
      if (bitreader_get(br, 16, &date)) return -1;
      full = (long)date;
      while (full < PUBLISHED_MJD_BASE) full += 65536L;
      st->have_date = 1;
      st->mjd = full;
    } else if (!st->have_date) {
      return -1;
    }
    if (bitreader_get(br, 11, &minutes) || minutes >= 1440) return -1;
    return fmt_iso(st->mjd, (long)minutes, 0, out, outcap);
  }
  if (flag != 0) return -1;
  if (bitreader_get(br, 32, &mjd_v)) return -1;
  if (bitreader_get(br, 32, &ms_v)) return -1;
  if (mjd_v < (uint64_t)MJD_MIN || mjd_v > (uint64_t)MJD_MAX || ms_v >= MS_PER_DAY) return -1;
  return fmt_iso((long)mjd_v, (long)(ms_v / 60000ULL), (int)((ms_v / 1000ULL) % 60ULL), out, outcap);
}

int dvb_datetime_decode(bitreader_t *br, char *out, size_t outcap) {
  dvb_codec_state_t st;
  memset(&st, 0, sizeof st);
  return dvb_datetime_decode_st(br, &st, out, outcap);
}

int dvb_controlledterm_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *href) {
  if (bitwriter_put(bw, 0, 1)) return -1; /* encoding_flag = 0, string form */
  return dvb_string_encode(sw, href);
}

int dvb_controlledterm_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap) {
  uint64_t encoding_flag;
  uint64_t grouping;
  uint64_t group = 0;
  uint64_t cs;
  uint64_t term;
  if (bitreader_get(br, 1, &encoding_flag)) return -1;
  if (!encoding_flag) return dvb_string_decode(sr, out, outcap);
  if (bitreader_get(br, 1, &grouping)) return -1;
  if (!grouping) {
    if (bitreader_get(br, 7, &cs)) return -1;
  } else if (bitreader_get_vluimsbf4(br, &group) || bitreader_get_vluimsbf8(br, &cs)) {
    return -1;
  }
  if (bitreader_get_vluimsbf8(br, &term)) return -1;
  if ((size_t)snprintf(out, outcap, "dvbcs:%llu:%llu:%llu", (unsigned long long)group, (unsigned long long)cs, (unsigned long long)term) >= outcap) return -1;
  return 0;
}

#define DECODING_MODES 0x0F /* lengthCodingMode 00, hasDeferredNodes 0, hasTypeCasting 0, reserved 1111 */
#define MAX_NODES 100000
#define VALUE_MAX 4096

bim_node_t *bim_node_new(const char *name) {
  bim_node_t *n = calloc(1, sizeof *n);
  if (n) n->name = name;
  return n;
}

void bim_node_free(bim_node_t *n) {
  if (!n) return;
  free(n->text);
  for (int i = 0; i < n->nattrs; i++) free(n->attrs[i].value);
  free(n->attrs);
  for (int i = 0; i < n->nkids; i++) bim_node_free(n->kids[i]);
  free(n->kids);
  free(n);
}

/* Takes ownership of v, also on failure */
static int node_put_attr(bim_node_t *n, const char *name, char *v) {
  void *np;
  for (int i = 0; i < n->nattrs; i++) {
    if (strcmp(n->attrs[i].name, name)) continue;
    free(n->attrs[i].value);
    n->attrs[i].value = v;
    return 0;
  }
  np = realloc(n->attrs, sizeof *n->attrs * (size_t)(n->nattrs + 1));
  if (!np) {
    free(v);
    return -1;
  }
  n->attrs = np;
  n->attrs[n->nattrs].name = name;
  n->attrs[n->nattrs].value = v;
  n->nattrs++;
  return 0;
}

int bim_node_set_attr(bim_node_t *n, const char *name, const char *value) {
  char *v = strdup(value);
  if (!v) return -1;
  return node_put_attr(n, name, v);
}

const char *bim_node_attr(const bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nattrs; i++)
    if (!strcmp(n->attrs[i].name, name)) return n->attrs[i].value;
  return NULL;
}

int bim_node_set_text(bim_node_t *n, const char *text) {
  char *t = NULL;
  if (text) {
    t = strdup(text);
    if (!t) return -1;
  }
  free(n->text);
  n->text = t;
  return 0;
}

int bim_node_add(bim_node_t *n, bim_node_t *kid) {
  if (n->nkids >= n->kcap) {
    void *np = array_grow(n->kids, &n->kcap, n->nkids + 1, sizeof *n->kids);
    if (!np) return -1;
    n->kids = np;
  }
  n->kids[n->nkids++] = kid;
  return 0;
}

bim_node_t *bim_node_add_new(bim_node_t *n, const char *name, const char *text) {
  bim_node_t *k = bim_node_new(name);
  if (!k) return NULL;
  if ((text && bim_node_set_text(k, text)) || bim_node_add(n, k)) {
    bim_node_free(k);
    return NULL;
  }
  return k;
}

const bim_node_t *bim_node_child(const bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nkids; i++)
    if (!strcmp(n->kids[i]->name, name)) return n->kids[i];
  return NULL;
}

static int enum_bits(int n) {
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
    int pick = -1, empty = -1;
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
  int cur = *ci, count = 0;
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
