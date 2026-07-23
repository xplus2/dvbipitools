/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/mux/psi_build.h"

#include "aitbuild.h"

static void put32(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

size_t aitbuild_pmt_entry(unsigned version, unsigned char *out, size_t cap) {
  size_t n = 0;
  (void)version;
  if (cap < 9)
    return 0;
  out[n++] = 0x05; /* stream_type: private sections */
  psi_put16(out + n, 0xE000 | (OUT_PID_AIT & 0x1FFF));
  n += 2;
  psi_put16(out + n, 0xF000 | 5); /* ES_info_length = 5 */
  n += 2;
  out[n++] = 0x6F; /* application_signalling_descriptor */
  out[n++] = 3;
  psi_put16(out + n, 0x0010); /* application_type: HbbTV */
  n += 2;
  out[n++] = 0xE0; /* reserved(3)=111, AIT_version_number(5)=0 */
  return n;
}

size_t aitbuild_ait(unsigned version, unsigned org_id, unsigned app_id, const char *url, unsigned char *out, size_t cap) {
  size_t n = 0, app_loop_len_pos, app_loop_start, adl_pos, ad_start;
  size_t app_desc_len_pos, app_desc_start, tp_desc_len_pos, tp_desc_start;
  size_t url_len = strlen(url);
  unsigned adl, all;

  if (url_len > 200 || cap < 60 + url_len)
    return 0;

  out[n++] = 0x74; /* table_id: AIT */
  n += 2;          /* section_syntax_indicator/reserved/section_length, patched below */
  out[n++] = 0x00;  /* test_application_flag=0, application_type high 7 bits */
  out[n++] = 0x10;  /* application_type low 8 bits: 0x0010 = HbbTV */
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00; /* section_number */
  out[n++] = 0x00; /* last_section_number */
  out[n++] = 0xF0; /* reserved(4)=1111, common_descriptors_length=0 */
  out[n++] = 0x00;

  app_loop_len_pos = n;
  n += 2;
  app_loop_start = n;

  put32(out + n, org_id);
  n += 4;
  psi_put16(out + n, app_id);
  n += 2;
  out[n++] = 0x01; /* application_control_code: AUTOSTART */

  adl_pos = n;
  n += 2;
  ad_start = n;

  /* application_descriptor (tag 0x00) */
  out[n++] = 0x00;
  app_desc_len_pos = n;
  n++;
  app_desc_start = n;
  out[n++] = 5; /* application_profile_length: one profile */
  psi_put16(out + n, 0x0000); /* application_profile */
  n += 2;
  out[n++] = 1; /* version.major */
  out[n++] = 0; /* version.minor */
  out[n++] = 0; /* version.micro */
  out[n++] = 0xFF; /* service_bound_flag=1, visibility=11 (visible), reserved=11111 */
  out[n++] = 1;    /* application_priority */
  out[n++] = 0x01; /* transport_protocol_label (matches the descriptor below) */
  out[app_desc_len_pos] = (unsigned char)(n - app_desc_start);

  /* transport_protocol_descriptor (tag 0x02), protocol_id 0x0003: interaction channel */
  out[n++] = 0x02;
  tp_desc_len_pos = n;
  n++;
  tp_desc_start = n;
  psi_put16(out + n, 0x0003);
  n += 2;
  out[n++] = 0x01; /* transport_protocol_label */
  out[n++] = (unsigned char)url_len;
  memcpy(out + n, url, url_len);
  n += url_len;
  out[tp_desc_len_pos] = (unsigned char)(n - tp_desc_start);

  adl = (unsigned)(n - ad_start);
  out[adl_pos] = (unsigned char)(0xF0 | ((adl >> 8) & 0x0F));
  out[adl_pos + 1] = (unsigned char)adl;

  all = (unsigned)(n - app_loop_start);
  out[app_loop_len_pos] = (unsigned char)(0xF0 | ((all >> 8) & 0x0F));
  out[app_loop_len_pos + 1] = (unsigned char)all;

  return psi_finish_section(out, n, cap, 0xF0);
}
