/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/mux/psi_build.h"

#include "psi.h"

size_t psi_build_pmt(unsigned version, unsigned program_number, unsigned pcr_pid, unsigned stream_type, unsigned es_pid, unsigned char *out, size_t cap) {
  static const unsigned char lang[3] = {'u', 'n', 'd'};
  size_t n = 0, es_info_pos;
  unsigned esinfo;

  if (cap < 32)
    return 0;
  out[n++] = 0x02;
  n += 2;
  psi_put16(out + n, program_number);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  psi_put16(out + n, 0xE000 | (pcr_pid & 0x1FFF));
  n += 2;
  psi_put16(out + n, 0xF000); /* program_info_length = 0 */
  n += 2;

  out[n++] = (unsigned char)stream_type;
  psi_put16(out + n, 0xE000 | (es_pid & 0x1FFF));
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
  return psi_finish_section(out, n, cap, 0xB0);
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
  psi_put16(out + n, service_id);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00; /* section_number: present event only */
  out[n++] = 0x00; /* last_section_number */
  psi_put16(out + n, tsid);
  n += 2;
  psi_put16(out + n, onid);
  n += 2;
  out[n++] = 0x00; /* segment_last_section_number */
  out[n++] = 0x4E; /* last_table_id */

  psi_put16(out + n, 1); /* event_id */
  n += 2;
  now = time(NULL);
  gmtime_r(&now, &tmv);
  psi_put16(out + n, mjd_from_tm(&tmv));
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
  enl = psi_put_text(out + n, cap - n, combined);
  n += enl;
  out[enl_pos] = (unsigned char)enl;
  out[n++] = 0x00; /* text_length: no extended text */
  out[dlen_pos] = (unsigned char)(n - (dlen_pos + 1));
  dll = (unsigned)(n - desc_start);
  out[dll_pos] = (unsigned char)(0xF0 | ((dll >> 8) & 0x0F));
  out[dll_pos + 1] = (unsigned char)dll;
  return psi_finish_section(out, n, cap, 0xF0);
}
