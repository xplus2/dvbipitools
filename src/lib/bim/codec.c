/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"

int dvb_string_encode(strrepo_writer_t *sw, const char *s) {
  return strrepo_writer_put(sw, s);
}

int dvb_string_decode(strrepo_reader_t *sr, char *out, size_t outcap) {
  return strrepo_reader_next(sr, out, outcap);
}

int dvb_locator_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *uri) {
  if (bitwriter_put(bw, 0, 1)) /* optimized_codec_flag = 0, always string fallback */
    return -1;
  return dvb_string_encode(sw, uri);
}

int dvb_locator_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap) {
  uint64_t flag;
  if (bitreader_get(br, 1, &flag))
    return -1;
  if (flag) /* optimized DVB locator branch, not supported */
    return -1;
  return dvb_string_decode(sr, out, outcap);
}

/* EN 300 468 annex C */
static long date_to_mjd(int y, int mo, int d) {
  int yy = mo <= 2 ? y - 1 : y;
  int mm = mo <= 2 ? mo + 12 : mo;
  return 14956L + d + (long)((yy - 1900) * 365.25) + (long)((mm + 1) * 30.6001);
}

static void mjd_to_date(long mjd, int *y, int *mo, int *d) {
  int yp = (int)((mjd - 15078.2) / 365.25);
  int mp = (int)((mjd - 14956.1 - (int)(yp * 365.25)) / 30.6001);
  int dd = (int)(mjd - 14956 - (int)(yp * 365.25) - (int)(mp * 30.6001));
  int k = (mp == 14 || mp == 15) ? 1 : 0;
  *y = yp + k + 1900;
  *mo = mp - 1 - k * 12;
  *d = dd;
}

static int all_digits(const char *s, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (!isdigit((unsigned char)s[i]))
      return 0;
  return 1;
}

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]", missing offset treated as UTC */
static int parse_iso8601(const char *in, int *y, int *mo, int *d, int *h, int *mi, int *s, int *off_min) {
  size_t l = strlen(in);
  const char *tail;
  if (l < 19 || in[4] != '-' || in[7] != '-' || in[10] != 'T' || in[13] != ':' || in[16] != ':')
    return -1;
  if (!all_digits(in, 4) || !all_digits(in + 5, 2) || !all_digits(in + 8, 2) ||
      !all_digits(in + 11, 2) || !all_digits(in + 14, 2) || !all_digits(in + 17, 2))
    return -1;
  *y = (in[0] - '0') * 1000 + (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
  *mo = (in[5] - '0') * 10 + (in[6] - '0');
  *d = (in[8] - '0') * 10 + (in[9] - '0');
  *h = (in[11] - '0') * 10 + (in[12] - '0');
  *mi = (in[14] - '0') * 10 + (in[15] - '0');
  *s = (in[17] - '0') * 10 + (in[18] - '0');
  tail = in + 19;
  *off_min = 0;
  if (*tail == 'Z' || *tail == '\0')
    return 0;
  if ((*tail == '+' || *tail == '-') && strlen(tail) >= 6 && tail[3] == ':' &&
      all_digits(tail + 1, 2) && all_digits(tail + 4, 2)) {
    int oh = (tail[1] - '0') * 10 + (tail[2] - '0');
    int om = (tail[4] - '0') * 10 + (tail[5] - '0');
    *off_min = (oh * 60 + om) * (tail[0] == '-' ? -1 : 1);
    return 0;
  }
  return -1;
}

static long floor_div(long a, long b) {
  long q = a / b, r = a % b;
  return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

int dvb_datetime_encode(bitwriter_t *bw, const char *iso8601) {
  int y, mo, d, h, mi, s, off_min;
  long mjd_local, total_minutes, mjd_utc, minute_of_day;
  int hour, min;
  uint64_t ms;

  if (parse_iso8601(iso8601, &y, &mo, &d, &h, &mi, &s, &off_min))
    return -1;

  mjd_local = date_to_mjd(y, mo, d);
  total_minutes = mjd_local * 1440L + h * 60L + mi - off_min;
  mjd_utc = floor_div(total_minutes, 1440L);
  minute_of_day = total_minutes - mjd_utc * 1440L;
  hour = (int)(minute_of_day / 60);
  min = (int)(minute_of_day % 60);
  ms = (uint64_t)(hour * 3600 + min * 60 + s) * 1000ULL;

  if (bitwriter_put(bw, 0, 2)) /* dateTime_flag = 00, full precision */
    return -1;
  if (bitwriter_put(bw, (uint64_t)mjd_utc, 32))
    return -1;
  return bitwriter_put(bw, ms, 32);
}

int dvb_datetime_decode(bitreader_t *br, char *out, size_t outcap) {
  uint64_t flag, mjd_v, ms_v;
  int y, mo, d, hour, min, sec;
  long minute_of_day;

  if (bitreader_get(br, 2, &flag))
    return -1;
  if (flag != 0) /* PublishedTime() 11-bit-minutes mode, not supported */
    return -1;
  if (bitreader_get(br, 32, &mjd_v))
    return -1;
  if (bitreader_get(br, 32, &ms_v))
    return -1;

  mjd_to_date((long)mjd_v, &y, &mo, &d);
  minute_of_day = (long)(ms_v / 60000ULL);
  hour = (int)(minute_of_day / 60);
  min = (int)(minute_of_day % 60);
  sec = (int)((ms_v / 1000ULL) % 60ULL);

  if ((size_t)snprintf(out, outcap, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, hour, min, sec) >= outcap)
    return -1;
  return 0;
}

typedef struct {
  const char *uri;
  unsigned id;
} cs_entry_t;

/* TS 102 323 table 69 */
static const cs_entry_t cs_table[] = {
    {"urn:tva:metadata:cs:ActionTypeCS:2004", 0x01},
    {"urn:tva:metadata:cs:AtmosphereCS:2004", 0x02},
    {"urn:tva:metadata:cs:ContentAlertCS:2004", 0x03},
    {"urn:tva:metadata:cs:ContentCommercialCS:2004", 0x04},
    {"urn:tva:metadata:cs:ContentCS:2004", 0x05},
    {"urn:tva:metadata:cs:FormatCS:2004", 0x06},
    {"urn:tva:metadata:cs:HowRelatedCS:2004", 0x07},
    {"urn:tva:metadata:cs:IntendedAudienceCS:2004", 0x08},
    {"urn:tva:metadata:cs:IntentionCS:2004", 0x09},
    {"urn:tva:metadata:cs:LanguageCS:2004", 0x0A},
    {"urn:tva:metadata:cs:MediaTypeCS:2004", 0x0B},
    {"urn:tva:metadata:cs:OriginationCS:2004", 0x0C},
    {"urn:mpeg:mpeg7:cs:RoleCS:2001", 0x0D},
    {"urn:tva:metadata:cs:TVARoleCS:2004", 0x0E},
    {"urn:tva:metadata:cs:AudioPurposeCS:2004", 0x0F},
    {"urn:tva:metadata:cs:PurchaseTypeCS:2004", 0x10},
    {"urn:tva:metadata:cs:UnitTypeCS:2004", 0x11},
};
#define CS_TABLE_N (sizeof cs_table / sizeof cs_table[0])

static int split_href(const char *href, char *scheme, size_t scheme_cap, char *term, size_t term_cap) {
  const char *last_colon = strrchr(href, ':');
  size_t scheme_len;
  if (!last_colon || last_colon == href)
    return -1;
  scheme_len = (size_t)(last_colon - href);
  if (scheme_len + 1 > scheme_cap || strlen(last_colon + 1) + 1 > term_cap)
    return -1;
  memcpy(scheme, href, scheme_len);
  scheme[scheme_len] = '\0';
  strcpy(term, last_colon + 1);
  return 0;
}

int dvb_controlledterm_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *href) {
  char scheme[256], term[64];
  size_t i;

  if (split_href(href, scheme, sizeof scheme, term, sizeof term) == 0) {
    for (i = 0; i < CS_TABLE_N; i++) {
      if (!strcmp(scheme, cs_table[i].uri)) {
        char *endp;
        unsigned long term_id = strtoul(term, &endp, 10);
        if (*endp == '\0') {
          if (bitwriter_put(bw, 1, 1))       /* encoding_flag = 1 */
            return -1;
          if (bitwriter_put(bw, 0, 1))       /* grouping_flag = 0 */
            return -1;
          if (bitwriter_put(bw, cs_table[i].id, 7))
            return -1;
          return bitwriter_put_vluimsbf8(bw, term_id);
        }
      }
    }
  }
  if (bitwriter_put(bw, 0, 1)) /* encoding_flag = 0, string fallback */
    return -1;
  return dvb_string_encode(sw, href);
}

int dvb_controlledterm_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap) {
  uint64_t encoding_flag;
  if (bitreader_get(br, 1, &encoding_flag))
    return -1;
  if (encoding_flag == 0)
    return dvb_string_decode(sr, out, outcap);
  {
    uint64_t grouping_flag, scheme_id, term_id;
    size_t i;
    if (bitreader_get(br, 1, &grouping_flag))
      return -1;
    if (grouping_flag) /* ClassificationSchemeGroupID/-Index form, not supported */
      return -1;
    if (bitreader_get(br, 7, &scheme_id))
      return -1;
    if (bitreader_get_vluimsbf8(br, &term_id))
      return -1;
    for (i = 0; i < CS_TABLE_N; i++)
      if (cs_table[i].id == scheme_id) {
        if ((size_t)snprintf(out, outcap, "%s:%lu", cs_table[i].uri, (unsigned long)term_id) >= outcap)
          return -1;
        return 0;
      }
    return -1;
  }
}
