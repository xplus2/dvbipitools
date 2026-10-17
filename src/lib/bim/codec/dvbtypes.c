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
  int hour;
  int min;
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
