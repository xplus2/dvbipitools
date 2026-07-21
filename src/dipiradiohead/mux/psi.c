/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/demux/crc32.h"

#include "psi.h"

static void put16(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

/* DVB text: 0x15 (UTF-8) prefix + bytes, truncated to fit cap */
static size_t put_text(unsigned char *out, size_t cap, const char *s) {
  size_t len;
  if (cap < 1)
    return 0;
  len = strlen(s);
  if (len > cap - 1)
    len = cap - 1;
  out[0] = 0x15;
  memcpy(out + 1, s, len);
  return len + 1;
}

static size_t finish_section(unsigned char *out, size_t len, size_t cap, unsigned char flags_nibble) {
  uint32_t crc;
  unsigned seclen;

  if (len + 4 > cap)
    return 0;
  crc = crc32_mpeg(out, len);
  out[len] = (unsigned char)(crc >> 24);
  out[len + 1] = (unsigned char)(crc >> 16);
  out[len + 2] = (unsigned char)(crc >> 8);
  out[len + 3] = (unsigned char)crc;
  len += 4;
  seclen = (unsigned)(len - 3);
  out[1] = (unsigned char)(flags_nibble | ((seclen >> 8) & 0x0F));
  out[2] = (unsigned char)seclen;
  return len;
}

size_t psi_build_pat(unsigned tsid, unsigned version, unsigned program_number, unsigned pmt_pid, unsigned char *out, size_t cap) {
  size_t n = 0;

  if (cap < 16)
    return 0;
  out[n++] = 0x00;
  n += 2;
  put16(out + n, tsid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  put16(out + n, program_number);
  n += 2;
  put16(out + n, 0xE000 | (pmt_pid & 0x1FFF));
  n += 2;

  return finish_section(out, n, cap, 0xB0);
}

size_t psi_build_pmt(unsigned version, unsigned program_number, unsigned pcr_pid, unsigned stream_type, unsigned es_pid, unsigned char *out, size_t cap) {
  static const unsigned char lang[3] = {'u', 'n', 'd'};
  size_t n = 0, es_info_pos;
  unsigned esinfo;

  if (cap < 32)
    return 0;
  out[n++] = 0x02;
  n += 2;
  put16(out + n, program_number);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  put16(out + n, 0xE000 | (pcr_pid & 0x1FFF));
  n += 2;
  put16(out + n, 0xF000); /* program_info_length = 0 */
  n += 2;

  out[n++] = (unsigned char)stream_type;
  put16(out + n, 0xE000 | (es_pid & 0x1FFF));
  n += 2;
  es_info_pos = n;
  n += 2;
  out[n++] = 0x0A; /* ISO_639_language_descriptor */
  out[n++] = 4;
  memcpy(out + n, lang, 3);
  n += 3;
  out[n++] = 0x00; /* audio_type: undefined */
  esinfo = (unsigned)(n - (es_info_pos + 2));
  out[es_info_pos] = (unsigned char)(0xF0 | ((esinfo >> 8) & 0x0F));
  out[es_info_pos + 1] = (unsigned char)esinfo;
  return finish_section(out, n, cap, 0xB0);
}

size_t psi_build_sdt(unsigned version, unsigned tsid, unsigned onid, unsigned service_id, const char *provider, const char *service, unsigned char *out, size_t cap) {
  size_t n = 0, f16_pos, desc_start, dlen_pos, plen_pos, slen_pos, plen, slen;
  unsigned dll, field16;

  if (cap < 24)
    return 0;
  out[n++] = 0x42;
  n += 2;
  put16(out + n, tsid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  put16(out + n, onid);
  n += 2;
  out[n++] = 0xFF;

  put16(out + n, service_id);
  n += 2;
  out[n++] = 0xFD; /* reserved(6)=111111, EIT_schedule=0, EIT_present_following=1 */

  f16_pos = n;
  n += 2;
  desc_start = n;
  out[n++] = 0x48; /* service_descriptor */
  dlen_pos = n;
  n++;
  out[n++] = 0x02; /* service_type: digital radio sound service */
  plen_pos = n;
  n++;
  plen = put_text(out + n, cap - n, provider);
  n += plen;
  out[plen_pos] = (unsigned char)plen;
  slen_pos = n;
  n++;
  slen = put_text(out + n, cap - n, service);
  n += slen;
  out[slen_pos] = (unsigned char)slen;
  out[dlen_pos] = (unsigned char)(n - (dlen_pos + 1));

  dll = (unsigned)(n - desc_start);
  field16 = ((4u & 0x7) << 13) | (0u << 12) | (dll & 0x0FFF); /* running_status=running, free_CA=0 */
  out[f16_pos] = (unsigned char)(field16 >> 8);
  out[f16_pos + 1] = (unsigned char)field16;
  return finish_section(out, n, cap, 0xF0);
}

size_t psi_build_nit(unsigned version, unsigned onid, unsigned tsid, const char *network_name, unsigned char *out, size_t cap) {
  size_t n = 0, ndl_pos, nd_start, dlen_pos, nlen, tsl_pos, ts_start;
  unsigned ndl, tsl;
  if (cap < 32)
    return 0;
  out[n++] = 0x40;
  n += 2;
  put16(out + n, onid); /* network_id */
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;

  ndl_pos = n;
  n += 2;
  nd_start = n;
  out[n++] = 0x40; /* network_name_descriptor */
  dlen_pos = n;
  n++;
  nlen = put_text(out + n, cap - n, network_name);
  n += nlen;
  out[dlen_pos] = (unsigned char)nlen;
  ndl = (unsigned)(n - nd_start);
  out[ndl_pos] = (unsigned char)(0xF0 | ((ndl >> 8) & 0x0F));
  out[ndl_pos + 1] = (unsigned char)ndl;

  tsl_pos = n;
  n += 2;
  ts_start = n;
  put16(out + n, tsid);
  n += 2;
  put16(out + n, onid);
  n += 2;
  put16(out + n, 0xF000); /* transport_descriptors_length = 0 */
  n += 2;
  tsl = (unsigned)(n - ts_start);
  out[tsl_pos] = (unsigned char)(0xF0 | ((tsl >> 8) & 0x0F));
  out[tsl_pos + 1] = (unsigned char)tsl;

  return finish_section(out, n, cap, 0xF0);
}

static unsigned mjd_from_tm(const struct tm *t) {
  int year = t->tm_year, month = t->tm_mon + 1, day = t->tm_mday;
  int l = (month <= 2) ? 1 : 0;
  return (unsigned)(14956 + day + (int)((year - l) * 365.25) + (int)((month + 1 + l * 12) * 30.6001));
}

static unsigned char bcd(unsigned v) { return (unsigned char)((((v / 10) % 10) << 4) | (v % 10)); }

size_t psi_build_eit(unsigned version, unsigned service_id, unsigned tsid, unsigned onid, const char *artist, const char *title, unsigned duration_s, unsigned char *out, size_t cap) {
  size_t n = 0, dll_pos, desc_start, dlen_pos, enl_pos, enl;
  unsigned dll, h, m, sec;
  char combined[512];
  time_t now;
  struct tm tmv;

  if (cap < 40)
    return 0;
  out[n++] = 0x4E;
  n += 2;
  put16(out + n, service_id);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00; /* section_number: present event only */
  out[n++] = 0x00; /* last_section_number */
  put16(out + n, tsid);
  n += 2;
  put16(out + n, onid);
  n += 2;
  out[n++] = 0x00; /* segment_last_section_number */
  out[n++] = 0x4E; /* last_table_id */

  put16(out + n, 1); /* event_id */
  n += 2;
  now = time(NULL);
  gmtime_r(&now, &tmv);
  put16(out + n, mjd_from_tm(&tmv));
  n += 2;
  out[n++] = bcd((unsigned)tmv.tm_hour);
  out[n++] = bcd((unsigned)tmv.tm_min);
  out[n++] = bcd((unsigned)tmv.tm_sec);
  h = duration_s / 3600;
  m = (duration_s % 3600) / 60;
  sec = duration_s % 60;
  out[n++] = bcd(h);
  out[n++] = bcd(m);
  out[n++] = bcd(sec);
  out[n++] = 0xF8; /* reserved(4)=1111, running_status=running(4), free_CA=0 */

  dll_pos = n;
  n += 2;
  desc_start = n;
  out[n++] = 0x4D; /* short_event_descriptor */
  dlen_pos = n;
  n++;
  out[n++] = 'u';
  out[n++] = 'n';
  out[n++] = 'd';
  if (artist[0] && title[0])
    snprintf(combined, sizeof combined, "%s %s", artist, title);
  else if (artist[0])
    snprintf(combined, sizeof combined, "%s", artist);
  else
    snprintf(combined, sizeof combined, "%s", title);
  enl_pos = n;
  n++;
  enl = put_text(out + n, cap - n, combined);
  n += enl;
  out[enl_pos] = (unsigned char)enl;
  out[n++] = 0x00; /* text_length: no extended text */
  out[dlen_pos] = (unsigned char)(n - (dlen_pos + 1));
  dll = (unsigned)(n - desc_start);
  out[dll_pos] = (unsigned char)(0xF0 | ((dll >> 8) & 0x0F));
  out[dll_pos + 1] = (unsigned char)dll;
  return finish_section(out, n, cap, 0xF0);
}
